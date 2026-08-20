/**
 * @file simulated_adc.hpp
 * @brief hal::IAdcChannel that models a 12-bit ADC reading an NTC divider.
 *
 * It converts the scenario temperature back into ADC counts (the inverse of the
 * driver maths) and then adds what a real board adds:
 *   - white noise (supply ripple, ADC LSB noise),
 *   - occasional single-sample spikes (a compressor or a fan motor switching),
 *   - an optional hard fault to exercise the service error path.
 *
 * That is what makes the filter comparison in the demo meaningful rather than
 * decorative.
 */
#ifndef SENSORFW_PORT_HOST_SIMULATED_ADC_HPP
#define SENSORFW_PORT_HOST_SIMULATED_ADC_HPP

#include "drivers/ntc/ntc_thermistor.hpp"
#include "sensorfw/hal/adc.hpp"
#include "sensorfw/hal/clock.hpp"
#include "port/host/scenario.hpp"

namespace sensorfw {
namespace port {
namespace host {

struct SimulatedAdcConfig {
    Real     noiseAmplitudeC;   ///< peak white noise in degC
    Real     spikeAmplitudeC;   ///< amplitude of the injected spikes
    uint32_t spikeEveryNth;     ///< inject a spike once every N reads (0 = never)

    SimulatedAdcConfig()
        : noiseAmplitudeC(static_cast<Real>(0.45)),
          spikeAmplitudeC(static_cast<Real>(9.0)),
          spikeEveryNth(17u) {}
};

class SimulatedNtcAdc : public hal::IAdcChannel {
public:
    SimulatedNtcAdc(hal::IClock& clock,
                    const drivers::NtcConfig& ntc = drivers::NtcConfig(),
                    const SimulatedAdcConfig& config = SimulatedAdcConfig());

    Status readRaw(uint16_t& rawCounts);
    uint16_t fullScale() const { return 4095u; }     // 12-bit, like STM32/ESP32
    uint16_t referenceMv() const { return 3300u; }

    /** @brief Make every subsequent conversion fail, to exercise the fault and
     *         recovery path of SensorService from the demo and the tests. */
    void injectFault(bool faulty) { faulty_ = faulty; }

    /** @brief The undisturbed value, so the demo can print the tracking error
     *         of each filter against ground truth. */
    Real lastTrueValue() const { return lastTrue_; }

private:
    hal::IClock&       clock_;
    drivers::NtcConfig ntc_;
    SimulatedAdcConfig config_;
    Xorshift32         rng_;
    uint32_t           readCount_;
    Real               lastTrue_;
    bool               faulty_;
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_SIMULATED_ADC_HPP
