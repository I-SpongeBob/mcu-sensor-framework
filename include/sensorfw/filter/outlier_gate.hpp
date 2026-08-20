/**
 * @file outlier_gate.hpp
 * @brief Rejects samples that jump further than a threshold from the current
 *        estimate, with a bounded rejection streak.
 *
 * The bounded streak is the important part. A naive gate that rejects forever
 * would latch on a stale value if the sensor is replaced, recalibrated, or the
 * device is carried into a different room. After @p maxConsecutiveRejects
 * refusals the gate re-synchronises to reality, so a real step is followed with
 * a bounded delay while a one-off glitch is still removed.
 */
#ifndef SENSORFW_FILTER_OUTLIER_GATE_HPP
#define SENSORFW_FILTER_OUTLIER_GATE_HPP

#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class OutlierGate : public IFilter {
public:
    OutlierGate(Real maxDeviation = static_cast<Real>(5),
                uint8_t maxConsecutiveRejects = 3u)
        : maxDeviation_(maxDeviation),
          maxRejects_(maxConsecutiveRejects),
          state_(static_cast<Real>(0)),
          rejectStreak_(0u),
          rejectedTotal_(0u),
          seeded_(false) {}

    Real update(Real input, TimestampMs) {
        if (!seeded_) {
            state_  = input;
            seeded_ = true;
            return state_;
        }

        const Real delta = (input > state_) ? (input - state_) : (state_ - input);

        if (delta > maxDeviation_ && rejectStreak_ < maxRejects_) {
            ++rejectStreak_;
            ++rejectedTotal_;
            return state_;               // hold the last trusted value
        }

        rejectStreak_ = 0u;              // accepted, or streak limit reached
        state_        = input;
        return state_;
    }

    void reset() { seeded_ = false; rejectStreak_ = 0u; }
    const char* name() const { return "outlier-gate"; }

    /** @brief Diagnostic counter - a rising count means bad wiring or a noisy
     *         supply and is worth reporting over MQTT. */
    uint32_t rejectedCount() const { return rejectedTotal_; }

private:
    Real     maxDeviation_;
    uint8_t  maxRejects_;
    Real     state_;
    uint8_t  rejectStreak_;
    uint32_t rejectedTotal_;
    bool     seeded_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_OUTLIER_GATE_HPP
