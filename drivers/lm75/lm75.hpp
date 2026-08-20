/**
 * @file lm75.hpp
 * @brief LM75B / TMP75 style I2C temperature sensor driver.
 *
 * Included next to the NTC driver to make the point that the layers above do
 * not care: an analog part behind an ADC and a digital part behind I2C expose
 * the very same ITemperatureSensor. Swapping one for the other is a one-line
 * change in the composition root, and the filters, the service, the GUI, the
 * MQTT reporter and the business logic are untouched.
 */
#ifndef SENSORFW_DRIVERS_LM75_HPP
#define SENSORFW_DRIVERS_LM75_HPP

#include "sensorfw/hal/clock.hpp"
#include "sensorfw/hal/i2c.hpp"
#include "sensorfw/sensor/sensor.hpp"

namespace sensorfw {
namespace drivers {

class Lm75Sensor : public ITemperatureSensor {
public:
    /** Default 7-bit address with A2..A0 tied low. */
    static const uint8_t kDefaultAddress = 0x48u;

    Lm75Sensor(hal::II2cBus& bus,
               hal::IClock& clock,
               uint8_t address = kDefaultAddress,
               const char* name = "lm75b");

    Status init();
    Status read(Sample& out);
    const SensorInfo& info() const { return info_; }

    /** @brief Register word -> degC. Static and hardware free, so the sign
     *         extension of the 11-bit two-complement value can be unit tested
     *         against the datasheet examples. */
    static Real convert(uint8_t msb, uint8_t lsb);

private:
    hal::II2cBus& bus_;
    hal::IClock&  clock_;
    uint8_t       address_;
    SensorInfo    info_;
    bool          initialised_;
};

} // namespace drivers
} // namespace sensorfw

#endif // SENSORFW_DRIVERS_LM75_HPP
