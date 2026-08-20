/**
 * @file i2c.hpp
 * @brief I2C master abstraction (used by the LM75 digital sensor driver).
 */
#ifndef SENSORFW_HAL_I2C_HPP
#define SENSORFW_HAL_I2C_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {
namespace hal {

/**
 * @brief Minimal I2C master interface.
 *
 * Only the two transactions every digital sensor needs are exposed. A port can
 * implement them with HAL_I2C_Mem_Read (STM32), i2c_master_transmit_receive
 * (ESP-IDF 5.x) or ioctl(I2C_RDWR) (Linux).
 */
class II2cBus {
public:
    /** @brief Write @p length bytes to @p deviceAddr (7-bit). */
    virtual Status write(uint8_t deviceAddr, const uint8_t* data, uint8_t length) = 0;

    /**
     * @brief Write a register address then read @p length bytes back.
     *        Emits a repeated START, as required by most sensors.
     */
    virtual Status writeRead(uint8_t deviceAddr,
                             const uint8_t* txData, uint8_t txLength,
                             uint8_t* rxData, uint8_t rxLength) = 0;

protected:
    ~II2cBus() {}
};

} // namespace hal
} // namespace sensorfw

#endif // SENSORFW_HAL_I2C_HPP
