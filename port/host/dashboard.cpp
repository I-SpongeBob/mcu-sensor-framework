#include "dashboard.hpp"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace sensorfw {
namespace port {
namespace host {

/* ------------------------------------------------------------- terminal -- */

AnsiTerminal::AnsiTerminal() : started_(false), savedMode_(0u) {}

AnsiTerminal::~AnsiTerminal() { end(); }

void AnsiTerminal::begin() {
    if (started_) { return; }
    started_ = true;

#if defined(_WIN32)
    /* Windows consoles do not interpret ANSI escapes until virtual terminal
     * processing is switched on explicitly. Without this the dashboard would
     * print raw escape codes. */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    if (GetConsoleMode(out, &mode)) {
        savedMode_ = static_cast<uint32_t>(mode);
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif

    printf("\033[?25l");        // hide the cursor
    clearScreen();
    fflush(stdout);
}

void AnsiTerminal::end() {
    if (!started_) { return; }
    started_ = false;

    printf("\033[?25h%s\n", reset());   // show the cursor again
    fflush(stdout);

#if defined(_WIN32)
    if (savedMode_ != 0u) {
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), static_cast<DWORD>(savedMode_));
    }
#endif
}

void AnsiTerminal::clearScreen() { printf("\033[2J\033[H"); }
void AnsiTerminal::home()        { printf("\033[H"); }

void AnsiTerminal::line(const char* text) {
    /* \033[K erases to end of line, so a shorter frame never leaves debris from
     * the previous one behind. Redrawing in place also avoids the flicker a
     * full clear-and-repaint would produce. */
    printf("%s\033[K\n", text);
}

/* ------------------------------------------------------------- keyboard -- */

#if defined(_WIN32)

KeyPoller::KeyPoller() : rawMode_(false) { memset(savedState_, 0, sizeof(savedState_)); }
KeyPoller::~KeyPoller() {}

char KeyPoller::poll() {
    if (!_kbhit()) { return 0; }
    const int key = _getch();
    return (key > 0 && key < 128) ? static_cast<char>(key) : 0;
}

#else

KeyPoller::KeyPoller() : rawMode_(false) {
    memset(savedState_, 0, sizeof(savedState_));

    struct termios current;
    if (tcgetattr(STDIN_FILENO, &current) != 0) { return; }
    memcpy(savedState_, &current, sizeof(current) <= sizeof(savedState_)
                                      ? sizeof(current) : sizeof(savedState_));

    struct termios raw = current;
    raw.c_lflag = raw.c_lflag & ~(static_cast<tcflag_t>(ICANON | ECHO));
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        rawMode_ = true;
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    }
}

KeyPoller::~KeyPoller() {
    if (!rawMode_) { return; }
    struct termios saved;
    memcpy(&saved, savedState_, sizeof(saved));
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

char KeyPoller::poll() {
    char key = 0;
    const ssize_t count = read(STDIN_FILENO, &key, 1u);
    return (count == 1) ? key : 0;
}

#endif

/* ---------------------------------------------------------------- chart -- */

ChartRecorder::ChartRecorder()
    : min_(static_cast<Real>(0)), max_(static_cast<Real>(0)) {
    rowBuffer_[0] = '\0';
}

Status ChartRecorder::attachTo(MeasurementPublisher& publisher) {
    return publisher.subscribe(
        MeasurementPublisher::Subscriber::bind<ChartRecorder,
                                               &ChartRecorder::onMeasurement>(this));
}

void ChartRecorder::onMeasurement(const Measurement& measurement) {
    if (!isOk(measurement.status)) {
        /* Hold the trace during a fault instead of drawing a cliff to zero. */
        return;
    }
    raw_.push(measurement.raw);
    filtered_.push(measurement.filtered);

    /* Scale the axis to the FILTERED series, not to the raw one.
     *
     * A single +9 degC spike in the raw trace would otherwise stretch the axis
     * over 20 degC and squash the signal everybody actually wants to look at
     * into one row. Raw samples outside the resulting window are clamped to the
     * edge by renderRow(), so a spike still shows as a mark on the ceiling or
     * the floor - visible, but no longer in charge of the scale. */
    min_ = filtered_.at(0);
    max_ = filtered_.at(0);
    for (uint8_t i = 0u; i < filtered_.size(); ++i) {
        const Real value = filtered_.at(i);
        if (value < min_) { min_ = value; }
        if (value > max_) { max_ = value; }
    }

    /* Head room, and a floor on the span so a flat signal does not turn the
     * chart into a random walk through the last three decimals. */
    Real margin = (max_ - min_) * static_cast<Real>(0.25);
    if (margin < static_cast<Real>(0.4)) { margin = static_cast<Real>(0.4); }
    min_ -= margin;
    max_ += margin;
}

void ChartRecorder::clear() {
    raw_.clear();
    filtered_.clear();
    min_ = static_cast<Real>(0);
    max_ = static_cast<Real>(0);
}

const char* ChartRecorder::renderRow(uint8_t row) const {
    rowBuffer_[0] = '\0';
    if (row >= kHeight || raw_.empty()) { return rowBuffer_; }

    /* Row 0 is the top of the chart, so the value it represents is the highest. */
    const Real span      = max_ - min_;
    const Real rowTop    = max_ - (span * static_cast<Real>(row)) / static_cast<Real>(kHeight);
    const Real rowBottom = max_ - (span * static_cast<Real>(row + 1u)) / static_cast<Real>(kHeight);

    const bool isTopRow    = (row == 0u);
    const bool isBottomRow = (row == kHeight - 1u);

    size_t offset = 0u;
    for (uint8_t column = 0u; column < raw_.size(); ++column) {
        const Real rawValue      = raw_.at(column);
        const Real filteredValue = filtered_.at(column);

        /* Out-of-window raw samples (the spikes) are pinned to the edge row. */
        const bool rawHere = ((rawValue <= rowTop) && (rawValue > rowBottom)) ||
                             (isTopRow && rawValue > max_) ||
                             (isBottomRow && rawValue <= min_);
        const bool filteredHere = (filteredValue <= rowTop) && (filteredValue > rowBottom);

        const char* cell;
        if (filteredHere) {
            cell = "\033[1;36m#\033[0m";      // filtered: bright cyan, drawn on top
        } else if (rawHere) {
            cell = "\033[90m.\033[0m";        // raw: dim grey
        } else {
            cell = " ";
        }

        const size_t length = strlen(cell);
        if (offset + length + 1u >= sizeof(rowBuffer_)) { break; }
        memcpy(&rowBuffer_[offset], cell, length);
        offset += length;
    }

    rowBuffer_[offset] = '\0';
    return rowBuffer_;
}

} // namespace host
} // namespace port
} // namespace sensorfw
