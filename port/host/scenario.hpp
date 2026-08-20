/**
 * @file scenario.hpp
 * @brief Synthetic but realistic temperature profile used by the host port.
 *
 * Everything is deterministic (fixed-seed xorshift), so the demo output and the
 * unit tests are reproducible on any machine and in CI.
 */
#ifndef SENSORFW_PORT_HOST_SCENARIO_HPP
#define SENSORFW_PORT_HOST_SCENARIO_HPP

#include "sensorfw/config.hpp"

namespace sensorfw {
namespace port {
namespace host {

/** @brief Deterministic 32-bit xorshift - no <random>, no allocation. */
class Xorshift32 {
public:
    explicit Xorshift32(uint32_t seed = 0x1234567u) : state_(seed ? seed : 1u) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    /** @brief Uniform value in [-1, 1]. */
    Real bipolar() {
        return (static_cast<Real>(next() % 20001u) / static_cast<Real>(10000)) -
               static_cast<Real>(1);
    }

private:
    uint32_t state_;
};

/**
 * @brief The "true" air temperature the sensor is supposed to measure.
 *
 * Contains the three things that break naive filtering in the field:
 *   - a slow sinusoidal drift (the room breathing),
 *   - a genuine step at t = 40 s (a window is opened) that the filter must
 *     eventually follow,
 *   - nothing else: noise and spikes are added later, at the ADC, which is
 *     where they physically come from.
 */
class TemperatureScenario {
public:
    static Real trueValueAt(TimestampMs timeMs);
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_SCENARIO_HPP
