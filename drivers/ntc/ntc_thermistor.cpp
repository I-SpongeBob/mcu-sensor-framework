#include "ntc_thermistor.hpp"

#include <math.h>

namespace sensorfw {
namespace drivers {

namespace {
const Real kKelvinOffset = static_cast<Real>(273.15);
}

NtcThermistorSensor::NtcThermistorSensor(hal::IAdcChannel& adc,
                                         hal::IClock& clock,
                                         const NtcConfig& config,
                                         const char* name)
    : adc_(adc), clock_(clock), config_(config), initialised_(false) {
    info_.name        = name;
    info_.quantity    = Quantity::Temperature;
    /* Range of a 10k/B3950 bead: outside this the divider resolution collapses
     * and the reading means nothing, so the service will reject it. */
    info_.minValue    = static_cast<Real>(-40);
    info_.maxValue    = static_cast<Real>(125);
    info_.minPeriodMs = 10u;   // one ADC conversion is a few microseconds
}

Status NtcThermistorSensor::init() {
    /* Nothing to program on the sensor itself; validate the configuration
     * instead, so a wrong board file fails loudly at boot rather than silently
     * reporting nonsense temperatures in the field. */
    if (config_.nominalResistance <= static_cast<Real>(0) ||
        config_.seriesResistance  <= static_cast<Real>(0) ||
        config_.betaCoefficient   <= static_cast<Real>(0)) {
        return Status::InvalidArgument;
    }
    if (adc_.fullScale() == 0u) {
        return Status::BusError;
    }
    initialised_ = true;
    return Status::Ok;
}

Status NtcThermistorSensor::read(Sample& out) {
    if (!initialised_) { return Status::NotInitialised; }

    uint16_t counts = 0u;
    const Status status = adc_.readRaw(counts);
    if (!isOk(status)) { return status; }

    Real temperature = static_cast<Real>(0);
    const Status conv = convert(counts, adc_.fullScale(), config_, temperature);
    if (!isOk(conv)) { return conv; }

    out.value     = temperature;
    out.timestamp = clock_.nowMs();
    return Status::Ok;
}

Status NtcThermistorSensor::convert(uint16_t rawCounts,
                                    uint16_t fullScale,
                                    const NtcConfig& config,
                                    Real& temperatureC) {
    if (fullScale == 0u) { return Status::InvalidArgument; }

    /* Work with the count ratio rather than volts: the reference voltage
     * cancels out in the divider, which also removes the Vref tolerance from
     * the error budget. */
    const Real ratio = static_cast<Real>(rawCounts) / static_cast<Real>(fullScale);

    /* An open circuit or a short pins the ADC at a rail; the division below
     * would blow up, so bail out with a diagnosable status instead. */
    if (ratio <= static_cast<Real>(0.001) || ratio >= static_cast<Real>(0.999)) {
        return Status::OutOfRange;
    }

    Real resistance;
    if (config.ntcToGround) {
        /* Vcc -- Rseries -- ADC -- Rntc -- GND  =>  Rntc = Rs * ratio/(1-ratio) */
        resistance = config.seriesResistance * ratio / (static_cast<Real>(1) - ratio);
    } else {
        /* Vcc -- Rntc -- ADC -- Rseries -- GND  =>  Rntc = Rs * (1-ratio)/ratio */
        resistance = config.seriesResistance * (static_cast<Real>(1) - ratio) / ratio;
    }

    /* Beta (Steinhart-Hart, one-parameter form):
     *   1/T = 1/T0 + (1/B) * ln(R/R0)                              [T in kelvin]
     * Accurate to ~0.5 degC over 0..70 degC, which is what a household
     * appliance needs. Swapping in the full 3-coefficient Steinhart-Hart is a
     * change to this function only. */
    const Real t0Kelvin = config.nominalTemperature + kKelvinOffset;
    const Real logRatio = static_cast<Real>(logf(
        static_cast<float>(resistance / config.nominalResistance)));

    const Real invT = (static_cast<Real>(1) / t0Kelvin) +
                      (logRatio / config.betaCoefficient);
    if (invT <= static_cast<Real>(0)) { return Status::OutOfRange; }

    temperatureC = (static_cast<Real>(1) / invT) - kKelvinOffset;
    return Status::Ok;
}

} // namespace drivers
} // namespace sensorfw
