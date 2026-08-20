/**
 * @file adc.hpp
 * @brief Single ADC channel abstraction (used by the NTC thermistor driver).
 */
#ifndef SENSORFW_HAL_ADC_HPP
#define SENSORFW_HAL_ADC_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {
namespace hal {

/**
 * @brief One analog input.
 *
 * Deliberately raw-counts based: the driver, not the HAL, knows how counts map
 * to a physical quantity. That keeps the port layer trivial to write for a new
 * MCU (read one register) and keeps the conversion maths unit-testable on the
 * host.
 */
class IAdcChannel {
public:
    /** @brief Start + read one conversion. Blocking, but bounded (~us). */
    virtual Status readRaw(uint16_t& rawCounts) = 0;

    /** @brief Full-scale count, e.g. 4095 for a 12-bit ADC. */
    virtual uint16_t fullScale() const = 0;

    /** @brief Reference voltage in millivolts, e.g. 3300. */
    virtual uint16_t referenceMv() const = 0;

protected:
    ~IAdcChannel() {}
};

} // namespace hal
} // namespace sensorfw

#endif // SENSORFW_HAL_ADC_HPP
