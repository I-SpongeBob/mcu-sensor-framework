#include "app/gui/temperature_view.hpp"

#include <stdio.h>

namespace sensorfw {
namespace app {

namespace {

/** Bar range shown on the gauge. */
const Real    kBarMinC   = static_cast<Real>(10);
const Real    kBarMaxC   = static_cast<Real>(35);
const uint8_t kBarCells  = 20u;

int32_t toDeciDegrees(Real value) {
    const Real scaled = value * static_cast<Real>(10);
    return static_cast<int32_t>(scaled >= static_cast<Real>(0)
                                    ? scaled + static_cast<Real>(0.5)
                                    : scaled - static_cast<Real>(0.5));
}

/**
 * @brief Format deci-degrees as "-3.4" without pulling in floating point printf.
 *
 * newlib-nano, the libc shipped with most MCU toolchains, drops %f from printf
 * unless you link an extra 10 kB of code. Formatting from a scaled integer is
 * the standard way around it and keeps the GUI cheap.
 */
void formatDeciDegrees(int32_t deci, char* out, size_t size) {
    const char* sign = "";
    if (deci < 0) { sign = "-"; deci = -deci; }
    snprintf(out, size, "%s%ld.%ld", sign,
             static_cast<long>(deci / 10), static_cast<long>(deci % 10));
}

} // namespace

TemperatureView::TemperatureView(IDisplay& display, uint32_t minRefreshIntervalMs)
    : display_(display),
      minRefreshIntervalMs_(minRefreshIntervalMs),
      lastRedrawTick_(0u),
      lastShownDeciDegrees_(0x7FFFFFFF),   // impossible value forces the first draw
      lastStatus_(Status::NotInitialised),
      redrawCount_(0u) {
    valueText_[0] = '\0';
    barText_[0]   = '\0';
}

Status TemperatureView::attachTo(MeasurementPublisher& publisher) {
    return publisher.subscribe(
        MeasurementPublisher::Subscriber::bind<TemperatureView,
                                              &TemperatureView::onMeasurement>(this));
}

void TemperatureView::onMeasurement(const Measurement& measurement) {
    const int32_t deci = toDeciDegrees(measurement.filtered);

    const bool valueChanged  = (deci != lastShownDeciDegrees_);
    const bool statusChanged = (measurement.status != lastStatus_);
    if (!valueChanged && !statusChanged) {
        return;   // nothing a human could see - skip the redraw entirely
    }

    /* Rate limit even when the value does change: a 20 Hz sensor must not drive
     * a 20 Hz panel refresh. */
    const uint32_t sinceRedraw =
        static_cast<uint32_t>(measurement.timestamp - lastRedrawTick_);
    if (!statusChanged && sinceRedraw < minRefreshIntervalMs_) {
        return;
    }

    lastShownDeciDegrees_ = deci;
    lastStatus_           = measurement.status;
    lastRedrawTick_       = measurement.timestamp;
    ++redrawCount_;

    render(measurement);
}

void TemperatureView::render(const Measurement& measurement) {
    char line[40];

    formatDeciDegrees(lastShownDeciDegrees_, valueText_, sizeof(valueText_));

    /* Horizontal gauge, clamped to the displayable range. */
    Real ratio = (measurement.filtered - kBarMinC) / (kBarMaxC - kBarMinC);
    if (ratio < static_cast<Real>(0)) { ratio = static_cast<Real>(0); }
    if (ratio > static_cast<Real>(1)) { ratio = static_cast<Real>(1); }

    const uint8_t filled = static_cast<uint8_t>(ratio * static_cast<Real>(kBarCells));
    uint8_t i = 0u;
    for (; i < kBarCells; ++i) { barText_[i] = (i < filled) ? '#' : '.'; }
    barText_[i] = '\0';

    display_.clear();

    snprintf(line, sizeof(line), "Temperature   %s C", valueText_);
    display_.drawText(0u, 0u, line);

    snprintf(line, sizeof(line), "[%s]", barText_);
    display_.drawText(0u, 1u, line);

    if (isOk(measurement.status)) {
        snprintf(line, sizeof(line), "src=%s  t=%lus",
                 measurement.source,
                 static_cast<unsigned long>(measurement.timestamp / 1000u));
    } else {
        /* Keep showing the last good value, flag it as stale. */
        snprintf(line, sizeof(line), "src=%s  SENSOR %s",
                 measurement.source, toString(measurement.status));
    }
    display_.drawText(0u, 2u, line);

    display_.flush();
}

} // namespace app
} // namespace sensorfw
