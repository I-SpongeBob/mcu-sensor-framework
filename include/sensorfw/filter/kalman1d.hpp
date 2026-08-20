/**
 * @file kalman1d.hpp
 * @brief Scalar Kalman filter (random-walk model).
 *
 * Model:  x[k] = x[k-1] + w,   w ~ N(0, Q)      (temperature drifts slowly)
 *         z[k] = x[k]   + v,   v ~ N(0, R)      (sensor noise)
 *
 * Unlike a fixed EWMA the gain adapts: it is large while the estimate is still
 * uncertain (fast settling after boot or after a step) and shrinks once the
 * estimate is confident (strong smoothing in steady state). That is exactly the
 * behaviour you want on a thermostat, at the cost of one division per sample.
 *
 * Tuning rule of thumb: R = (sensor sigma)^2, then raise Q until the estimate
 * tracks a real step fast enough.
 */
#ifndef SENSORFW_FILTER_KALMAN1D_HPP
#define SENSORFW_FILTER_KALMAN1D_HPP

#include "sensorfw/core/time.hpp"
#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class Kalman1dFilter : public IFilter {
public:
    /**
     * @param processVariancePerSecond  Q: how fast the true value may drift
     * @param measurementVariance       R: sensor noise power
     */
    Kalman1dFilter(Real processVariancePerSecond = static_cast<Real>(0.01),
                   Real measurementVariance      = static_cast<Real>(0.25))
        : q_(processVariancePerSecond),
          r_(measurementVariance),
          x_(static_cast<Real>(0)),
          p_(static_cast<Real>(1)),
          lastTimestamp_(0u),
          seeded_(false) {}

    Real update(Real input, TimestampMs timestamp) {
        if (!seeded_) {
            x_             = input;
            p_             = r_;      // initial uncertainty = measurement noise
            lastTimestamp_ = timestamp;
            seeded_        = true;
            return x_;
        }

        const uint32_t dtMs = elapsedMs(timestamp, lastTimestamp_);
        lastTimestamp_ = timestamp;
        const Real dt = static_cast<Real>(dtMs) / static_cast<Real>(1000);

        /* Predict: the state stays put, only the uncertainty grows with time. */
        p_ += q_ * dt;

        /* Update. */
        const Real k = p_ / (p_ + r_);        // Kalman gain, always in (0,1)
        x_ += k * (input - x_);
        p_ = (static_cast<Real>(1) - k) * p_;

        return x_;
    }

    void reset() { seeded_ = false; p_ = static_cast<Real>(1); x_ = static_cast<Real>(0); }
    const char* name() const { return "kalman-1d"; }

    /** @brief Current estimate variance - exposed so the app can report a
     *         confidence, and so unit tests can assert convergence. */
    Real variance() const { return p_; }

private:
    Real        q_;
    Real        r_;
    Real        x_;   ///< state estimate
    Real        p_;   ///< estimate variance
    TimestampMs lastTimestamp_;
    bool        seeded_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_KALMAN1D_HPP
