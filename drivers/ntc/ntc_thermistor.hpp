/**
 * @file ntc_thermistor.hpp
 * @brief Analog NTC thermistor driver (10k/B3950 by default).
 *
 * The cheapest temperature sensor in an appliance, and the one that needs the
 * most care: the conversion is non-linear and the divider topology differs
 * between boards. Both are configuration, not code.
 *
 * The driver depends only on hal::IAdcChannel, so the same object runs on an
 * STM32 ADC, an ESP32 ADC1 channel or the host simulator used by the unit tests.
 */
#ifndef SENSORFW_DRIVERS_NTC_THERMISTOR_HPP
#define SENSORFW_DRIVERS_NTC_THERMISTOR_HPP

#include "sensorfw/hal/adc.hpp"
#include "sensorfw/hal/clock.hpp"
#include "sensorfw/sensor/sensor.hpp"

namespace sensorfw {
namespace drivers {

/** Board level wiring and thermistor datasheet parameters. */
struct NtcConfig {
    Real     nominalResistance;   ///< R0, resistance at nominalTemperature (ohm)
    Real     nominalTemperature;  ///< T0 in degC, virtually always 25
    Real     betaCoefficient;     ///< B constant from the datasheet (K)
    Real     seriesResistance;    ///< the fixed resistor of the divider (ohm)
    bool     ntcToGround;         ///< true: Vcc - Rseries - [ADC] - Rntc - GND

    NtcConfig()
        : nominalResistance(static_cast<Real>(10000)),
          nominalTemperature(static_cast<Real>(25)),
          betaCoefficient(static_cast<Real>(3950)),
          seriesResistance(static_cast<Real>(10000)),
          ntcToGround(true) {}
};

class NtcThermistorSensor : public ITemperatureSensor {
public:
    NtcThermistorSensor(hal::IAdcChannel& adc,
                        hal::IClock& clock,
                        const NtcConfig& config = NtcConfig(),
                        const char* name = "ntc-10k");

    Status init();
    Status read(Sample& out);
    const SensorInfo& info() const { return info_; }

    /**
     * @brief Counts -> degC conversion, exposed for unit testing.
     *
     * Kept static and free of any hardware access so the maths can be verified
     * on the host against datasheet values without an ADC.
     */
    static Status convert(uint16_t rawCounts,
                          uint16_t fullScale,
                          const NtcConfig& config,
                          Real& temperatureC);

private:
    hal::IAdcChannel& adc_;
    hal::IClock&      clock_;
    NtcConfig         config_;
    SensorInfo        info_;
    bool              initialised_;
};

} // namespace drivers
} // namespace sensorfw

#endif // SENSORFW_DRIVERS_NTC_THERMISTOR_HPP
