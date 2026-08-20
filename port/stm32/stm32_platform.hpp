/**
 * @file stm32_platform.hpp
 * @brief STM32 (HAL / LL) implementation of the three HAL interfaces.
 *
 * This is the entire porting effort for a new STM32 board: three thin classes,
 * about 80 lines, no change anywhere above. The file compiles only when the
 * build selects the STM32 target, so the host build ignores it completely.
 *
 * Build with: cmake -DSENSORFW_TARGET=stm32
 */
#ifndef SENSORFW_PORT_STM32_PLATFORM_HPP
#define SENSORFW_PORT_STM32_PLATFORM_HPP

#if defined(SENSORFW_TARGET_STM32)

#include "sensorfw/hal/adc.hpp"
#include "sensorfw/hal/clock.hpp"
#include "sensorfw/hal/i2c.hpp"

#include "stm32f4xx_hal.h"   // replace with the series header of your MCU

namespace sensorfw {
namespace port {
namespace stm32 {

/** @brief Backed by the SysTick counter maintained by the CubeMX HAL. */
class Stm32Clock : public hal::IClock {
public:
    TimestampMs nowMs() const { return static_cast<TimestampMs>(HAL_GetTick()); }
};

/**
 * @brief One regular ADC channel, polled.
 *
 * A production build would move this to DMA with a hardware oversampling ratio
 * of 16 and only expose the DMA buffer here; the interface would not change,
 * which is the point of keeping the conversion maths in the driver.
 */
class Stm32AdcChannel : public hal::IAdcChannel {
public:
    Stm32AdcChannel(ADC_HandleTypeDef* adc, uint32_t channel,
                    uint16_t fullScale = 4095u, uint16_t referenceMv = 3300u)
        : adc_(adc), channel_(channel), fullScale_(fullScale), referenceMv_(referenceMv) {}

    Status readRaw(uint16_t& rawCounts) {
        ADC_ChannelConfTypeDef config;
        config.Channel      = channel_;
        config.Rank         = 1u;
        config.SamplingTime = ADC_SAMPLETIME_480CYCLES;  // long: the NTC divider is high impedance
        if (HAL_ADC_ConfigChannel(adc_, &config) != HAL_OK) { return Status::BusError; }

        if (HAL_ADC_Start(adc_) != HAL_OK) { return Status::BusError; }
        const HAL_StatusTypeDef status = HAL_ADC_PollForConversion(adc_, 10u);
        if (status == HAL_TIMEOUT) { HAL_ADC_Stop(adc_); return Status::Timeout; }
        if (status != HAL_OK)      { HAL_ADC_Stop(adc_); return Status::BusError; }

        rawCounts = static_cast<uint16_t>(HAL_ADC_GetValue(adc_));
        HAL_ADC_Stop(adc_);
        return Status::Ok;
    }

    uint16_t fullScale() const { return fullScale_; }
    uint16_t referenceMv() const { return referenceMv_; }

private:
    ADC_HandleTypeDef* adc_;
    uint32_t           channel_;
    uint16_t           fullScale_;
    uint16_t           referenceMv_;
};

/** @brief I2C master built on the blocking CubeMX API. */
class Stm32I2cBus : public hal::II2cBus {
public:
    explicit Stm32I2cBus(I2C_HandleTypeDef* i2c, uint32_t timeoutMs = 50u)
        : i2c_(i2c), timeoutMs_(timeoutMs) {}

    Status write(uint8_t deviceAddr, const uint8_t* data, uint8_t length) {
        const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
            i2c_, static_cast<uint16_t>(deviceAddr << 1), const_cast<uint8_t*>(data),
            length, timeoutMs_);
        return translate(status);
    }

    Status writeRead(uint8_t deviceAddr,
                     const uint8_t* txData, uint8_t txLength,
                     uint8_t* rxData, uint8_t rxLength) {
        /* HAL_I2C_Mem_Read issues the repeated START the sensors expect. */
        if (txLength != 1u) { return Status::InvalidArgument; }
        const HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
            i2c_, static_cast<uint16_t>(deviceAddr << 1), txData[0],
            I2C_MEMADD_SIZE_8BIT, rxData, rxLength, timeoutMs_);
        return translate(status);
    }

private:
    static Status translate(HAL_StatusTypeDef status) {
        switch (status) {
            case HAL_OK:      return Status::Ok;
            case HAL_TIMEOUT: return Status::Timeout;
            case HAL_BUSY:    return Status::NotReady;
            default:          return Status::BusError;
        }
    }

    I2C_HandleTypeDef* i2c_;
    uint32_t           timeoutMs_;
};

} // namespace stm32
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_TARGET_STM32
#endif // SENSORFW_PORT_STM32_PLATFORM_HPP
