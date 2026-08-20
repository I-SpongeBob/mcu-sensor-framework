/**
 * @file moving_average.hpp
 * @brief Sliding window arithmetic mean (FIR).
 *
 * Best for zero-mean white noise: attenuates it by sqrt(N) while keeping the
 * DC gain exactly 1. Cost: a group delay of (N-1)/2 samples.
 * Window size is a template parameter -> the buffer is a plain member array,
 * no allocation, and the compiler can unroll the loop.
 */
#ifndef SENSORFW_FILTER_MOVING_AVERAGE_HPP
#define SENSORFW_FILTER_MOVING_AVERAGE_HPP

#include "sensorfw/core/ring_buffer.hpp"
#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

template <uint8_t WindowSize>
class MovingAverageFilter : public IFilter {
public:
    MovingAverageFilter() {}

    Real update(Real input, TimestampMs) {
        window_.push(input);
        /* The sum is recomputed instead of kept incrementally: with N <= 32 the
         * cost is negligible and it avoids the slow drift a running float sum
         * accumulates over millions of samples in a device that never reboots. */
        Real sum = static_cast<Real>(0);
        for (uint8_t i = 0; i < window_.size(); ++i) {
            sum += window_.at(i);
        }
        return sum / static_cast<Real>(window_.size());
    }

    void reset() { window_.clear(); }
    const char* name() const { return "moving-average"; }

    /** @brief True once the window is fully populated (output is trustworthy). */
    bool settled() const { return window_.full(); }

private:
    RingBuffer<Real, WindowSize> window_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_MOVING_AVERAGE_HPP
