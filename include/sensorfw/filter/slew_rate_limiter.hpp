/**
 * @file slew_rate_limiter.hpp
 * @brief Bounds how fast the output may change (degC per second).
 *
 * Physics based rather than statistics based: a room's air temperature simply
 * cannot move 20 degC in 100 ms, so anything faster is an artefact. Placed at
 * the end of a chain it guarantees the value shown on the GUI and sent to MQTT
 * is always physically plausible, even if every other filter is misconfigured.
 */
#ifndef SENSORFW_FILTER_SLEW_RATE_LIMITER_HPP
#define SENSORFW_FILTER_SLEW_RATE_LIMITER_HPP

#include "sensorfw/core/time.hpp"
#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class SlewRateLimiter : public IFilter {
public:
    explicit SlewRateLimiter(Real maxChangePerSecond = static_cast<Real>(2))
        : maxRate_(maxChangePerSecond),
          state_(static_cast<Real>(0)),
          lastTimestamp_(0u),
          seeded_(false) {}

    Real update(Real input, TimestampMs timestamp) {
        if (!seeded_) {
            state_         = input;
            lastTimestamp_ = timestamp;
            seeded_        = true;
            return state_;
        }

        const uint32_t dtMs = elapsedMs(timestamp, lastTimestamp_);
        lastTimestamp_ = timestamp;

        const Real maxStep = maxRate_ * static_cast<Real>(dtMs) / static_cast<Real>(1000);
        const Real delta   = input - state_;

        if (delta >  maxStep) { state_ += maxStep; }
        else if (delta < -maxStep) { state_ -= maxStep; }
        else { state_ = input; }

        return state_;
    }

    void reset() { seeded_ = false; }
    const char* name() const { return "slew-limit"; }

private:
    Real        maxRate_;
    Real        state_;
    TimestampMs lastTimestamp_;
    bool        seeded_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_SLEW_RATE_LIMITER_HPP
