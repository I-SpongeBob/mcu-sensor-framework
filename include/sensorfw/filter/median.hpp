/**
 * @file median.hpp
 * @brief Sliding window median - the tool for impulsive noise.
 *
 * A single 40 degC spike caused by an ADC glitch or an I2C bit error moves a
 * moving average by 40/N degC but does not move a median at all. Typical use is
 * a short median (3..5 taps) in front of a smoothing filter; see FilterChain.
 */
#ifndef SENSORFW_FILTER_MEDIAN_HPP
#define SENSORFW_FILTER_MEDIAN_HPP

#include "sensorfw/core/ring_buffer.hpp"
#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

template <uint8_t WindowSize>
class MedianFilter : public IFilter {
public:
    MedianFilter() {}

    Real update(Real input, TimestampMs) {
        window_.push(input);

        const uint8_t n = window_.size();
        Real scratch[WindowSize];
        for (uint8_t i = 0; i < n; ++i) { scratch[i] = window_.at(i); }

        /* Insertion sort: O(N^2) but N is tiny, it needs no extra memory and it
         * is branch-predictable, which beats qsort on a Cortex-M for N < 16. */
        for (uint8_t i = 1; i < n; ++i) {
            const Real key = scratch[i];
            int8_t j = static_cast<int8_t>(i) - 1;
            while (j >= 0 && scratch[j] > key) {
                scratch[j + 1] = scratch[j];
                --j;
            }
            scratch[j + 1] = key;
        }

        /* Even window sizes average the two central samples so the DC gain
         * stays 1 whatever WindowSize the integrator picks. */
        if ((n & 1u) != 0u) {
            return scratch[n / 2u];
        }
        return (scratch[n / 2u - 1u] + scratch[n / 2u]) / static_cast<Real>(2);
    }

    void reset() { window_.clear(); }
    const char* name() const { return "median"; }

private:
    RingBuffer<Real, WindowSize> window_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_MEDIAN_HPP
