#include "scenario.hpp"

#include <math.h>

namespace sensorfw {
namespace port {
namespace host {

Real TemperatureScenario::trueValueAt(TimestampMs timeMs) {
    const Real seconds = static_cast<Real>(timeMs) / static_cast<Real>(1000);

    Real value = static_cast<Real>(22.0);

    /* Slow drift, period 60 s, amplitude 0.6 degC. */
    value += static_cast<Real>(0.6) *
             static_cast<Real>(sinf(static_cast<float>(seconds) * 6.2831853f / 60.0f));

    /* Step: someone opens a window at t = 40 s. */
    if (seconds >= static_cast<Real>(40)) {
        value -= static_cast<Real>(3.0);
    }

    return value;
}

} // namespace host
} // namespace port
} // namespace sensorfw
