#include "app/logic/thermostat.hpp"

#include "sensorfw/core/time.hpp"

namespace sensorfw {
namespace app {

Thermostat::Thermostat(ISwitchOutput& output,
                       ThermostatEventPublisher& events,
                       const ThermostatConfig& config)
    : output_(output),
      events_(events),
      config_(config),
      lastSwitchTick_(0u),
      lastValidTick_(0u),
      everValid_(false),
      sensorLost_(false),
      overTemperature_(false),
      switchCount_(0u) {}

Status Thermostat::attachTo(MeasurementPublisher& publisher) {
    return publisher.subscribe(
        MeasurementPublisher::Subscriber::bind<Thermostat,
                                               &Thermostat::onMeasurement>(this));
}

void Thermostat::onMeasurement(const Measurement& measurement) {
    if (!isOk(measurement.status)) {
        /* One bad sample is normal (an I2C NACK happens). Only a sustained loss
         * of valid data triggers the fail-safe. */
        if (everValid_ &&
            elapsedMs(measurement.timestamp, lastValidTick_) >= config_.sensorTimeoutMs &&
            !sensorLost_) {
            sensorLost_ = true;
            applyOutput(false, measurement);          // fail-safe: heater off
            emit(ThermostatEventType::SensorLost, measurement);
        }
        return;
    }

    if (sensorLost_) {
        sensorLost_ = false;
        emit(ThermostatEventType::SensorRecovered, measurement);
    }
    lastValidTick_ = measurement.timestamp;
    everValid_     = true;

    /* Over-temperature is evaluated before the control law and latches the
     * output off - a safety limit must not be arbitrated by the dwell timers. */
    if (measurement.filtered >= config_.alarmHighC) {
        if (!overTemperature_) {
            overTemperature_ = true;
            emit(ThermostatEventType::OverTemperature, measurement);
        }
        if (output_.state()) {
            output_.set(false);
            ++switchCount_;
            lastSwitchTick_ = measurement.timestamp;
            emit(ThermostatEventType::OutputOff, measurement);
        }
        return;
    }
    /* Release the latch with the same hysteresis, so the alarm cannot chatter. */
    if (overTemperature_ &&
        measurement.filtered < (config_.alarmHighC - config_.hysteresisC)) {
        overTemperature_ = false;
    }
    if (overTemperature_) { return; }

    const Real half     = config_.hysteresisC / static_cast<Real>(2);
    const Real turnOnAt  = config_.setpointC - half;
    const Real turnOffAt = config_.setpointC + half;

    if (!output_.state() && measurement.filtered <= turnOnAt) {
        applyOutput(true, measurement);
    } else if (output_.state() && measurement.filtered >= turnOffAt) {
        applyOutput(false, measurement);
    }
}

void Thermostat::applyOutput(bool on, const Measurement& measurement) {
    if (output_.state() == on) { return; }

    /* Dwell time protection. A safety stop (sensorLost / over-temperature) also
     * goes through here but always in the "off" direction, and switching off
     * early is never harmful - so only the "on" transition is held back. */
    if (on) {
        const uint32_t restedMs = elapsedMs(measurement.timestamp, lastSwitchTick_);
        if (everSwitched() && restedMs < config_.minOffTimeMs) { return; }
    } else {
        const uint32_t ranMs = elapsedMs(measurement.timestamp, lastSwitchTick_);
        if (everSwitched() && ranMs < config_.minOnTimeMs && !sensorLost_ &&
            !overTemperature_) {
            return;
        }
    }

    if (!isOk(output_.set(on))) { return; }

    ++switchCount_;
    lastSwitchTick_ = measurement.timestamp;
    emit(on ? ThermostatEventType::OutputOn : ThermostatEventType::OutputOff,
         measurement);
}

void Thermostat::emit(ThermostatEventType type, const Measurement& measurement) {
    ThermostatEvent event;
    event.type        = type;
    event.temperature = measurement.filtered;
    event.timestamp   = measurement.timestamp;
    events_.publish(event);
}

} // namespace app
} // namespace sensorfw
