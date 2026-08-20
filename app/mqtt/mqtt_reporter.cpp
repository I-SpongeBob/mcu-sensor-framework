#include "app/mqtt/mqtt_reporter.hpp"

#include <stdio.h>

namespace sensorfw {
namespace app {

namespace {

/** Same integer trick as the GUI: no %f, no float printf in the image. */
void appendMilli(char* out, size_t size, Real value) {
    const Real scaled = value * static_cast<Real>(1000);
    long milli = static_cast<long>(scaled >= static_cast<Real>(0)
                                       ? scaled + static_cast<Real>(0.5)
                                       : scaled - static_cast<Real>(0.5));
    const char* sign = "";
    if (milli < 0) { sign = "-"; milli = -milli; }
    snprintf(out, size, "%s%ld.%03ld", sign, milli / 1000, milli % 1000);
}

} // namespace

MqttReporter::MqttReporter(IMqttClient& client, const MqttReporterConfig& config)
    : client_(client),
      config_(config),
      lastPublishedValue_(static_cast<Real>(0)),
      lastPublishedStatus_(Status::NotInitialised),
      lastPublishTick_(0u),
      everPublished_(false),
      pending_(false),
      published_(0u),
      dropped_(0u),
      suppressed_(0u) {
    payload_[0] = '\0';
}

Status MqttReporter::attachTo(MeasurementPublisher& publisher) {
    return publisher.subscribe(
        MeasurementPublisher::Subscriber::bind<MqttReporter,
                                               &MqttReporter::onMeasurement>(this));
}

void MqttReporter::onMeasurement(const Measurement& measurement) {
    if (!shouldPublish(measurement)) {
        ++suppressed_;
        return;
    }

    if (!client_.connected()) {
        /* Offline: remember that something is waiting, but do not queue every
         * sample. On a device that may be offline for hours, the freshest value
         * is the only one worth sending when the link returns. */
        pending_ = true;
        ++dropped_;
        return;
    }

    buildPayload(measurement);

    const Status status = client_.publish(config_.topic, payload_,
                                          config_.qos, config_.retain);
    if (!isOk(status)) {
        pending_ = true;
        ++dropped_;
        return;
    }

    pending_             = false;
    everPublished_       = true;
    lastPublishedValue_  = measurement.filtered;
    lastPublishedStatus_ = measurement.status;
    lastPublishTick_     = measurement.timestamp;
    ++published_;
}

bool MqttReporter::shouldPublish(const Measurement& measurement) const {
    if (!everPublished_ || pending_) { return true; }
    if (measurement.status != lastPublishedStatus_) { return true; }

    const uint32_t sincePublish =
        static_cast<uint32_t>(measurement.timestamp - lastPublishTick_);
    if (sincePublish >= config_.heartbeatMs) { return true; }

    const Real delta = (measurement.filtered > lastPublishedValue_)
                           ? (measurement.filtered - lastPublishedValue_)
                           : (lastPublishedValue_ - measurement.filtered);
    return delta >= config_.deadBandC;
}

void MqttReporter::buildPayload(const Measurement& measurement) {
    char filtered[16];
    char raw[16];
    appendMilli(filtered, sizeof(filtered), measurement.filtered);
    appendMilli(raw, sizeof(raw), measurement.raw);

    /* Hand written JSON: no allocation, bounded size, and the compiler can put
     * the format string in flash. A cJSON tree would cost heap and ~6 kB. */
    snprintf(payload_, sizeof(payload_),
             "{\"src\":\"%s\",\"unit\":\"degC\",\"value\":%s,\"raw\":%s,"
             "\"ts\":%lu,\"status\":\"%s\"}",
             measurement.source,
             filtered,
             raw,
             static_cast<unsigned long>(measurement.timestamp),
             toString(measurement.status));
}

} // namespace app
} // namespace sensorfw
