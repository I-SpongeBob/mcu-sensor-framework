/**
 * @file filter.hpp
 * @brief The filter strategy interface.
 *
 * Every filter is a pure function of (previous state, new sample, dt). It knows
 * nothing about sensors, and sensors know nothing about filters - they are
 * wired together at composition time (see examples/host_demo/main.cpp).
 */
#ifndef SENSORFW_FILTER_FILTER_HPP
#define SENSORFW_FILTER_FILTER_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {

/**
 * @brief Strategy interface for signal conditioning.
 *
 * update() is called once per accepted sample, in sampling order.
 * @param input      newest raw value
 * @param timestamp  acquisition tick; filters that model time (EWMA with a
 *                   time constant, slew limiter, Kalman) use the delta so that
 *                   a jittery or reconfigured sampling period does not change
 *                   their tuning.
 * @return the conditioned value
 */
class IFilter {
public:
    virtual Real update(Real input, TimestampMs timestamp) = 0;

    /** @brief Drop all history (sensor re-init, mode change, unit test). */
    virtual void reset() = 0;

    /** @brief Human readable name, used by logs and by the GUI/MQTT payload. */
    virtual const char* name() const = 0;

protected:
    ~IFilter() {}
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_FILTER_HPP
