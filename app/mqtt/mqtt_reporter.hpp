/**
 * @file mqtt_reporter.hpp
 * @brief Telemetry consumer: turns Measurements into MQTT payloads.
 *
 * The second subscriber on the same channel as the GUI. It never talks to the
 * sensor and never blocks the sampling loop.
 *
 * Publishing policy (report by exception, the standard IoT pattern):
 *   - publish when the value moved by more than a dead band, or
 *   - publish at least once per heartbeat interval so the cloud can tell
 *     "unchanged" from "device is dead", or
 *   - publish immediately when the sensor status changes.
 * On a 30 s heartbeat with a 0.2 degC dead band a 1 Hz sensor drops from
 * 86400 to a few hundred messages a day, which is the difference between a
 * usable and an unusable NB-IoT bill.
 */
#ifndef SENSORFW_APP_MQTT_REPORTER_HPP
#define SENSORFW_APP_MQTT_REPORTER_HPP

#include "app/mqtt/mqtt_client.hpp"
#include "sensorfw/core/publisher.hpp"

namespace sensorfw {
namespace app {

struct MqttReporterConfig {
    const char* topic;
    Real        deadBandC;        ///< minimum change worth a message
    uint32_t    heartbeatMs;      ///< publish at least this often
    uint8_t     qos;
    bool        retain;

    MqttReporterConfig()
        : topic("device/+/sensor/temperature"),
          deadBandC(static_cast<Real>(0.2)),
          heartbeatMs(30000u),
          qos(0u),
          retain(true) {}
};

class MqttReporter {
public:
    MqttReporter(IMqttClient& client, const MqttReporterConfig& config = MqttReporterConfig());

    Status attachTo(MeasurementPublisher& publisher);
    void onMeasurement(const Measurement& measurement);

    uint32_t publishedCount() const { return published_; }
    uint32_t droppedCount() const { return dropped_; }
    uint32_t suppressedCount() const { return suppressed_; }
    const char* lastPayload() const { return payload_; }

private:
    bool shouldPublish(const Measurement& measurement) const;
    void buildPayload(const Measurement& measurement);

    IMqttClient&       client_;
    MqttReporterConfig config_;

    Real        lastPublishedValue_;
    Status      lastPublishedStatus_;
    TimestampMs lastPublishTick_;
    bool        everPublished_;
    bool        pending_;          ///< a sample is waiting for the link to come back

    uint32_t published_;
    uint32_t dropped_;
    uint32_t suppressed_;

    char payload_[160];
};

} // namespace app
} // namespace sensorfw

#endif // SENSORFW_APP_MQTT_REPORTER_HPP
