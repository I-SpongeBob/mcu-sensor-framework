/**
 * @file esp32_platform.hpp
 * @brief ESP-IDF implementation of the same three HAL interfaces.
 *
 * Same framework, same drivers, same filters, same application code - only
 * this file differs from the STM32 port. That is the cross-MCU claim, made
 * concrete.
 *
 * Build with: cmake -DSENSORFW_TARGET=esp32 (or add the sources to an IDF
 * component; see docs/PORTING.md).
 */
#ifndef SENSORFW_PORT_ESP32_PLATFORM_HPP
#define SENSORFW_PORT_ESP32_PLATFORM_HPP

#if defined(SENSORFW_TARGET_ESP32)

#include "sensorfw/hal/adc.hpp"
#include "sensorfw/hal/clock.hpp"
#include "sensorfw/hal/i2c.hpp"

#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

namespace sensorfw {
namespace port {
namespace esp32 {

/** @brief Microsecond timer, truncated to the framework millisecond tick. */
class Esp32Clock : public hal::IClock {
public:
    TimestampMs nowMs() const {
        return static_cast<TimestampMs>(esp_timer_get_time() / 1000);
    }
};

/** @brief ADC1 one-shot channel (ESP-IDF 5.x driver). */
class Esp32AdcChannel : public hal::IAdcChannel {
public:
    Esp32AdcChannel(adc_oneshot_unit_handle_t unit, adc_channel_t channel,
                    uint16_t fullScale = 4095u, uint16_t referenceMv = 3300u)
        : unit_(unit), channel_(channel), fullScale_(fullScale), referenceMv_(referenceMv) {}

    Status readRaw(uint16_t& rawCounts) {
        int value = 0;
        const esp_err_t err = adc_oneshot_read(unit_, channel_, &value);
        if (err == ESP_ERR_TIMEOUT) { return Status::Timeout; }
        if (err != ESP_OK)          { return Status::BusError; }
        rawCounts = static_cast<uint16_t>(value);
        return Status::Ok;
    }

    uint16_t fullScale() const { return fullScale_; }
    uint16_t referenceMv() const { return referenceMv_; }

private:
    adc_oneshot_unit_handle_t unit_;
    adc_channel_t             channel_;
    uint16_t                  fullScale_;
    uint16_t                  referenceMv_;
};

/** @brief I2C master (ESP-IDF 5.x bus/device model). */
class Esp32I2cBus : public hal::II2cBus {
public:
    Esp32I2cBus(i2c_master_dev_handle_t device, uint32_t timeoutMs = 50u)
        : device_(device), timeoutMs_(timeoutMs) {}

    Status write(uint8_t, const uint8_t* data, uint8_t length) {
        /* The device handle already carries the address in this driver model. */
        return translate(i2c_master_transmit(device_, data, length,
                                             static_cast<int>(timeoutMs_)));
    }

    Status writeRead(uint8_t,
                     const uint8_t* txData, uint8_t txLength,
                     uint8_t* rxData, uint8_t rxLength) {
        return translate(i2c_master_transmit_receive(device_, txData, txLength,
                                                     rxData, rxLength,
                                                     static_cast<int>(timeoutMs_)));
    }

private:
    static Status translate(esp_err_t err) {
        switch (err) {
            case ESP_OK:            return Status::Ok;
            case ESP_ERR_TIMEOUT:   return Status::Timeout;
            case ESP_ERR_INVALID_ARG: return Status::InvalidArgument;
            default:                return Status::BusError;
        }
    }

    i2c_master_dev_handle_t device_;
    uint32_t                timeoutMs_;
};

} // namespace esp32
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_TARGET_ESP32
#endif // SENSORFW_PORT_ESP32_PLATFORM_HPP
