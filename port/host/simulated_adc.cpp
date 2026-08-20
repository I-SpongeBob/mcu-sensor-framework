#include "simulated_adc.hpp"

#include <math.h>

namespace sensorfw {
namespace port {
namespace host {

namespace {
const Real kKelvinOffset = static_cast<Real>(273.15);

/** Inverse of the beta equation: degC -> thermistor resistance. */
Real temperatureToResistance(Real temperatureC, const drivers::NtcConfig& ntc) {
    const Real tKelvin  = temperatureC + kKelvinOffset;
    const Real t0Kelvin = ntc.nominalTemperature + kKelvinOffset;
    const Real exponent = ntc.betaCoefficient *
                          ((static_cast<Real>(1) / tKelvin) -
                           (static_cast<Real>(1) / t0Kelvin));
    return ntc.nominalResistance * static_cast<Real>(expf(static_cast<float>(exponent)));
}
}

SimulatedNtcAdc::SimulatedNtcAdc(hal::IClock& clock,
                                 const drivers::NtcConfig& ntc,
                                 const SimulatedAdcConfig& config)
    : clock_(clock),
      ntc_(ntc),
      config_(config),
      rng_(0xC0FFEEu),
      readCount_(0u),
      lastTrue_(static_cast<Real>(0)),
      faulty_(false) {}

Status SimulatedNtcAdc::readRaw(uint16_t& rawCounts) {
    if (faulty_) { return Status::BusError; }

    ++readCount_;
    lastTrue_ = TemperatureScenario::trueValueAt(clock_.nowMs());

    Real disturbed = lastTrue_ + config_.noiseAmplitudeC * rng_.bipolar();

    if (config_.spikeEveryNth != 0u && (readCount_ % config_.spikeEveryNth) == 0u) {
        /* A single-sample outlier, alternating sign. This is what ruins a plain
         * moving average and what a median in front of it removes completely. */
        disturbed += ((readCount_ % (config_.spikeEveryNth * 2u)) == 0u)
                         ? config_.spikeAmplitudeC
                         : -config_.spikeAmplitudeC;
    }

    const Real resistance = temperatureToResistance(disturbed, ntc_);

    Real ratio;
    if (ntc_.ntcToGround) {
        ratio = resistance / (ntc_.seriesResistance + resistance);
    } else {
        ratio = ntc_.seriesResistance / (ntc_.seriesResistance + resistance);
    }

    Real counts = ratio * static_cast<Real>(fullScale());
    if (counts < static_cast<Real>(0)) { counts = static_cast<Real>(0); }
    if (counts > static_cast<Real>(fullScale())) { counts = static_cast<Real>(fullScale()); }

    rawCounts = static_cast<uint16_t>(counts + static_cast<Real>(0.5));
    return Status::Ok;
}

} // namespace host
} // namespace port
} // namespace sensorfw
