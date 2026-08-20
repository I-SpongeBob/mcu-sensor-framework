/**
 * @file mqtt_client.hpp
 * @brief Transport abstraction for the telemetry consumer.
 *
 * Behind it: Paho/ESP-MQTT/lwIP on target, a printing stub on the host. The
 * reporter above only needs "am I online" and "send this payload".
 */
#ifndef SENSORFW_APP_MQTT_CLIENT_HPP
#define SENSORFW_APP_MQTT_CLIENT_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {
namespace app {

class IMqttClient {
public:
    virtual bool connected() const = 0;

    /**
     * @brief Hand a payload to the MQTT stack.
     *
     * Must not block: on target this enqueues into the network task. Returning
     * Status::NoSpace when the queue is full is expected behaviour, not a bug -
     * the reporter reacts by keeping the sample pending.
     */
    virtual Status publish(const char* topic,
                           const char* payload,
                           uint8_t qos,
                           bool retain) = 0;

protected:
    ~IMqttClient() {}
};

} // namespace app
} // namespace sensorfw

#endif // SENSORFW_APP_MQTT_CLIENT_HPP
