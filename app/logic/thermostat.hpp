/**
 * @file thermostat.hpp
 * @brief Business logic consumer: closed-loop control on the same channel.
 *
 * Third subscriber, again with no knowledge of sensors, filters or transports.
 * It demonstrates that the decoupling is not just for passive consumers - the
 * control path uses exactly the same seam.
 *
 * What it implements is what an appliance actually needs:
 *   - hysteresis (never a bare comparison against the setpoint, which would
 *     chatter the relay at every LSB of noise),
 *   - minimum on/off dwell times, so a compressor is never restarted before its
 *     oil pressure has equalised,
 *   - a fail-safe: if measurements stop being valid for longer than a timeout,
 *     the output is forced off and an event is raised. A stuck sensor must not
 *     leave a heater on.
 */
#ifndef SENSORFW_APP_LOGIC_THERMOSTAT_HPP
#define SENSORFW_APP_LOGIC_THERMOSTAT_HPP

#include "sensorfw/core/publisher.hpp"

namespace sensorfw {
namespace app {

/** @brief Binary actuator (relay, triac, MOSFET, or a CAN message). */
class ISwitchOutput {
public:
    virtual Status set(bool on) = 0;
    virtual bool state() const = 0;
protected:
    ~ISwitchOutput() {}
};

enum class ThermostatEventType : uint8_t {
    OutputOn,
    OutputOff,
    OverTemperature,
    SensorLost,
    SensorRecovered
};

struct ThermostatEvent {
    ThermostatEventType type;
    Real                temperature;
    TimestampMs         timestamp;
};

typedef Publisher<ThermostatEvent, 4> ThermostatEventPublisher;

struct ThermostatConfig {
    Real     setpointC;
    Real     hysteresisC;      ///< total dead band, centred on the setpoint
    Real     alarmHighC;       ///< over-temperature trip
    uint32_t minOnTimeMs;      ///< shortest allowed run once started
    uint32_t minOffTimeMs;     ///< shortest allowed rest once stopped
    uint32_t sensorTimeoutMs;  ///< invalid data for longer than this = fail-safe

    ThermostatConfig()
        : setpointC(static_cast<Real>(22)),
          hysteresisC(static_cast<Real>(0.6)),
          alarmHighC(static_cast<Real>(45)),
          minOnTimeMs(30000u),
          minOffTimeMs(60000u),
          sensorTimeoutMs(10000u) {}
};

class Thermostat {
public:
    Thermostat(ISwitchOutput& output,
               ThermostatEventPublisher& events,
               const ThermostatConfig& config = ThermostatConfig());

    Status attachTo(MeasurementPublisher& publisher);
    void onMeasurement(const Measurement& measurement);

    /** @brief Change the target temperature (from the UI, or over MQTT). */
    void setSetpoint(Real setpointC) { config_.setpointC = setpointC; }
    Real setpoint() const { return config_.setpointC; }

    bool heating() const { return output_.state(); }
    bool sensorLost() const { return sensorLost_; }
    bool alarmActive() const { return overTemperature_; }
    uint32_t switchCount() const { return switchCount_; }

private:
    /** @brief False until the first transition, so the dwell timers do not
     *         block the very first start after boot. */
    bool everSwitched() const { return switchCount_ > 0u; }

    void applyOutput(bool on, const Measurement& measurement);
    void emit(ThermostatEventType type, const Measurement& measurement);

    ISwitchOutput&            output_;
    ThermostatEventPublisher& events_;
    ThermostatConfig          config_;

    TimestampMs lastSwitchTick_;
    TimestampMs lastValidTick_;
    bool        everValid_;
    bool        sensorLost_;
    bool        overTemperature_;
    uint32_t    switchCount_;
};

} // namespace app
} // namespace sensorfw

#endif // SENSORFW_APP_LOGIC_THERMOSTAT_HPP
