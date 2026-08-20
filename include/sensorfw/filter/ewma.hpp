/**
 * @file ewma.hpp
 * @brief First order IIR low-pass (exponentially weighted moving average).
 *
 *      y[k] = y[k-1] + alpha * (x[k] - y[k-1])
 *
 * Two state words and one multiply per sample - the cheapest useful filter, and
 * the default choice for a slowly varying quantity like room temperature.
 *
 * The cut-off can be given either as a raw alpha or, preferably, as a time
 * constant in milliseconds. In the latter case alpha is recomputed from the
 * measured dt:
 *
 *      alpha = dt / (tau + dt)
 *
 * so the response stays the same when the sampling period changes (power save
 * mode, a slower sensor, jitter under an RTOS).
 */
#ifndef SENSORFW_FILTER_EWMA_HPP
#define SENSORFW_FILTER_EWMA_HPP

#include "sensorfw/core/time.hpp"
#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class EwmaFilter : public IFilter {
public:
    /** @brief Fixed-alpha form. @p alpha in (0,1]; smaller = smoother/slower. */
    static EwmaFilter withAlpha(Real alpha) { return EwmaFilter(alpha, 0u); }

    /** @brief Time-constant form (recommended). 63% of a step is reached after
     *         @p timeConstantMs. */
    static EwmaFilter withTimeConstant(uint32_t timeConstantMs) {
        return EwmaFilter(static_cast<Real>(0.5), timeConstantMs);
    }

    explicit EwmaFilter(Real alpha = static_cast<Real>(0.2), uint32_t timeConstantMs = 0u)
        : alpha_(clampAlpha(alpha)),
          timeConstantMs_(timeConstantMs),
          state_(static_cast<Real>(0)),
          lastTimestamp_(0u),
          seeded_(false) {}

    Real update(Real input, TimestampMs timestamp) {
        if (!seeded_) {
            /* Seeding with the first sample instead of 0 removes the long
             * start-up ramp that would otherwise show a wrong temperature on
             * the GUI for several seconds after boot. */
            state_         = input;
            lastTimestamp_ = timestamp;
            seeded_        = true;
            return state_;
        }

        Real alpha = alpha_;
        if (timeConstantMs_ > 0u) {
            const uint32_t dtMs = elapsedMs(timestamp, lastTimestamp_);
            if (dtMs > 0u) {
                alpha = static_cast<Real>(dtMs) /
                        static_cast<Real>(timeConstantMs_ + dtMs);
            }
        }
        lastTimestamp_ = timestamp;

        state_ += alpha * (input - state_);
        return state_;
    }

    void reset() { seeded_ = false; state_ = static_cast<Real>(0); }
    const char* name() const { return "ewma"; }

    Real alpha() const { return alpha_; }

private:
    static Real clampAlpha(Real a) {
        if (a <= static_cast<Real>(0)) { return static_cast<Real>(0.001); }
        if (a >  static_cast<Real>(1)) { return static_cast<Real>(1); }
        return a;
    }

    Real        alpha_;
    uint32_t    timeConstantMs_;
    Real        state_;
    TimestampMs lastTimestamp_;
    bool        seeded_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_EWMA_HPP
