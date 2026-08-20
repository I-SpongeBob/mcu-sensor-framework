/**
 * @file simulated_i2c.hpp
 * @brief hal::II2cBus that emulates an LM75B, so the digital driver can be
 *        exercised (and unit tested) without hardware.
 */
#ifndef SENSORFW_PORT_HOST_SIMULATED_I2C_HPP
#define SENSORFW_PORT_HOST_SIMULATED_I2C_HPP

#include "sensorfw/hal/clock.hpp"
#include "sensorfw/hal/i2c.hpp"

namespace sensorfw {
namespace port {
namespace host {

class SimulatedLm75Bus : public hal::II2cBus {
public:
    explicit SimulatedLm75Bus(hal::IClock& clock, uint8_t address = 0x48u);

    Status write(uint8_t deviceAddr, const uint8_t* data, uint8_t length);
    Status writeRead(uint8_t deviceAddr,
                     const uint8_t* txData, uint8_t txLength,
                     uint8_t* rxData, uint8_t rxLength);

    /** @brief Simulate the device being absent (every transfer NACKs). */
    void setPresent(bool present) { present_ = present; }

    uint32_t transferCount() const { return transferCount_; }
    bool configured() const { return configured_; }

private:
    hal::IClock& clock_;
    uint8_t      address_;
    uint8_t      pointer_;
    bool         present_;
    bool         configured_;
    uint32_t     transferCount_;
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_SIMULATED_I2C_HPP
