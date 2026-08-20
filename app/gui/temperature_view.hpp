/**
 * @file temperature_view.hpp
 * @brief GUI consumer: renders the measurement on a display.
 *
 * This class is a subscriber and nothing else. It has no reference to the
 * sensor, the driver, the filter or the service - it only ever sees a
 * Measurement value object. Deleting it from the build removes the GUI and
 * changes nothing else.
 *
 * Two behaviours that matter on real hardware:
 *   - the panel is only redrawn when the displayed text would actually change,
 *     or when the refresh interval elapses (a redraw is expensive and, on an
 *     e-paper panel, visible);
 *   - the value is quantised to 0.1 degC before that comparison, so noise in
 *     the third decimal cannot cause a redraw storm.
 */
#ifndef SENSORFW_APP_GUI_TEMPERATURE_VIEW_HPP
#define SENSORFW_APP_GUI_TEMPERATURE_VIEW_HPP

#include "app/gui/display.hpp"
#include "sensorfw/core/publisher.hpp"

namespace sensorfw {
namespace app {

class TemperatureView {
public:
    TemperatureView(IDisplay& display, uint32_t minRefreshIntervalMs = 250u);

    /** @brief Subscribe this view to a measurement channel. */
    Status attachTo(MeasurementPublisher& publisher);

    /** @brief Subscriber entry point. Called from the sampling context, so it
     *         must stay short - it only formats and blits. */
    void onMeasurement(const Measurement& measurement);

    uint32_t redrawCount() const { return redrawCount_; }
    const char* lastRenderedValue() const { return valueText_; }

private:
    void render(const Measurement& measurement);

    IDisplay&   display_;
    uint32_t    minRefreshIntervalMs_;
    TimestampMs lastRedrawTick_;
    int32_t     lastShownDeciDegrees_;
    Status      lastStatus_;
    uint32_t    redrawCount_;
    char        valueText_[16];
    char        barText_[24];
};

} // namespace app
} // namespace sensorfw

#endif // SENSORFW_APP_GUI_TEMPERATURE_VIEW_HPP
