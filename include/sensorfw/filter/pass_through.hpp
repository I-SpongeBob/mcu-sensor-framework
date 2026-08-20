/**
 * @file pass_through.hpp
 * @brief Identity filter - the "no filtering" configuration.
 *
 * Having an explicit null object means the service never needs a
 * `if (filter != nullptr)` branch, and it gives the demo a raw reference curve.
 */
#ifndef SENSORFW_FILTER_PASS_THROUGH_HPP
#define SENSORFW_FILTER_PASS_THROUGH_HPP

#include "sensorfw/filter/filter.hpp"

namespace sensorfw {

class PassThroughFilter : public IFilter {
public:
    Real update(Real input, TimestampMs) { return input; }
    void reset() {}
    const char* name() const { return "passthrough"; }
};

} // namespace sensorfw

#endif // SENSORFW_FILTER_PASS_THROUGH_HPP
