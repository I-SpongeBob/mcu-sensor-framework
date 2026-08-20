/**
 * @file filter_chain.hpp
 * @brief Composite filter: a chain is itself an IFilter.
 *
 * This is what makes the filtering "flexible" in practice - stages are combined
 * and reordered at composition time (or at runtime, from a config value) with
 * no change to the sensor, the service or the application:
 *
 *   median(5) -> ewma(tau=2s) -> slew-limit(2 degC/s)
 *   outlier-gate(5 degC) -> kalman(Q,R)
 *   passthrough                                   (raw, for factory test mode)
 *
 * Because FilterChain derives from IFilter, a chain can also be nested inside
 * another chain.
 */
#ifndef SENSORFW_FILTER_FILTER_CHAIN_HPP
#define SENSORFW_FILTER_FILTER_CHAIN_HPP

#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class FilterChain : public IFilter {
public:
    FilterChain() : count_(0u) {}

    /**
     * @brief Append a stage. The chain does not own the filter - stages are
     *        normally static or members of the owning component, so there is
     *        no allocation and no lifetime question.
     * @return Status::NoSpace if SENSORFW_MAX_FILTERS_PER_CHAIN is exceeded.
     */
    Status append(IFilter* filter) {
        if (filter == 0) { return Status::InvalidArgument; }
        if (count_ >= SENSORFW_MAX_FILTERS_PER_CHAIN) { return Status::NoSpace; }
        stages_[count_++] = filter;
        return Status::Ok;
    }

    /** @brief Drop all stages (used when switching filter profile at runtime). */
    void clear() { count_ = 0u; }

    Real update(Real input, TimestampMs timestamp) {
        Real value = input;
        for (uint8_t i = 0; i < count_; ++i) {
            value = stages_[i]->update(value, timestamp);
        }
        return value;
    }

    void reset() {
        for (uint8_t i = 0; i < count_; ++i) { stages_[i]->reset(); }
    }

    const char* name() const { return "chain"; }

    uint8_t stageCount() const { return count_; }
    const IFilter* stage(uint8_t index) const { return stages_[index]; }

private:
    IFilter* stages_[SENSORFW_MAX_FILTERS_PER_CHAIN];
    uint8_t  count_;
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_FILTER_CHAIN_HPP
