/**
 * @file dashboard.hpp
 * @brief Terminal control, keyboard polling and an ASCII chart recorder for the
 *        live demo.
 *
 * All of this is host presentation code. It is worth noting what it did NOT
 * require: TemperatureView, MqttReporter, Thermostat, SensorService and every
 * filter are used unmodified. The chart is simply a fourth subscriber on the
 * same Publisher<Measurement> - which is the claim the framework makes about
 * adding consumers, exercised rather than asserted.
 */
#ifndef SENSORFW_PORT_HOST_DASHBOARD_HPP
#define SENSORFW_PORT_HOST_DASHBOARD_HPP

#include "sensorfw/core/publisher.hpp"
#include "sensorfw/core/ring_buffer.hpp"

namespace sensorfw {
namespace port {
namespace host {

/** @brief ANSI escape sequences, and the Windows console setup they need. */
class AnsiTerminal {
public:
    AnsiTerminal();
    ~AnsiTerminal();

    /** @brief Enable virtual terminal processing (Windows) and hide the cursor. */
    void begin();

    /** @brief Restore the cursor and leave the screen in a sane state. */
    void end();

    static void clearScreen();
    static void home();
    /** @brief Print a line and erase whatever the previous frame left after it. */
    static void line(const char* text);

    static const char* reset()  { return "\033[0m"; }
    static const char* dim()    { return "\033[90m"; }
    static const char* red()    { return "\033[31m"; }
    static const char* green()  { return "\033[32m"; }
    static const char* yellow() { return "\033[33m"; }
    static const char* cyan()   { return "\033[36m"; }
    static const char* bold()   { return "\033[1m"; }

private:
    bool     started_;
    uint32_t savedMode_;      ///< Windows console mode to restore
};

/** @brief Non-blocking single-key polling, on Windows and on POSIX. */
class KeyPoller {
public:
    KeyPoller();
    ~KeyPoller();

    /** @brief Returns the pressed key, or 0 when nothing is waiting. */
    char poll();

private:
    bool rawMode_;
    /* Opaque storage for the POSIX termios backup. Oversized on purpose:
     * struct termios is 60 bytes on Linux but larger on other Unixes, and the
     * copy is guarded by a size check in the constructor either way. */
    char savedState_[128];
};

/**
 * @brief Fourth subscriber: keeps a short history and plots it in ASCII.
 *
 * Plots the raw and the filtered series on the same auto-scaled axes, which is
 * the whole point of carrying both in Measurement: on a real board this is how
 * you tell "the filter is too slow" from "the sensor is too noisy".
 */
class ChartRecorder {
public:
    static const uint8_t kWidth  = 62u;
    static const uint8_t kHeight = 11u;

    ChartRecorder();

    Status attachTo(MeasurementPublisher& publisher);
    void onMeasurement(const Measurement& measurement);

    /** @brief Render one chart row (0 = top). The returned buffer stays valid
     *         until the next call. */
    const char* renderRow(uint8_t row) const;

    Real minValue() const { return min_; }
    Real maxValue() const { return max_; }
    uint8_t sampleCount() const { return raw_.size(); }
    void clear();

private:
    RingBuffer<Real, kWidth> raw_;
    RingBuffer<Real, kWidth> filtered_;
    Real                     min_;
    Real                     max_;
    mutable char             rowBuffer_[kWidth * 12u + 32u];
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_DASHBOARD_HPP
