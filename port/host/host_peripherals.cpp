#include "host_peripherals.hpp"

#include <stdio.h>
#include <string.h>

namespace sensorfw {
namespace port {
namespace host {

ConsoleDisplay::ConsoleDisplay() : flushCount_(0u), quiet_(false) { clear(); }

void ConsoleDisplay::clear() {
    for (uint8_t r = 0u; r < kRows; ++r) {
        memset(buffer_[r], ' ', kCols);
        buffer_[r][kCols] = '\0';
    }
}

void ConsoleDisplay::drawText(uint8_t column, uint8_t row, const char* text) {
    if (row >= kRows || column >= kCols || text == 0) { return; }

    /* Clip instead of overflowing - a display driver that trusts the caller is
     * a buffer overflow waiting for a long sensor name. */
    for (uint8_t i = 0u; text[i] != '\0' && (column + i) < kCols; ++i) {
        buffer_[row][column + i] = text[i];
    }
}

void ConsoleDisplay::flush() {
    ++flushCount_;
    if (quiet_) { return; }

    printf("  +----------------------------------------------+\n");
    for (uint8_t r = 0u; r < kRows; ++r) {
        printf("  | %-44.44s |\n", buffer_[r]);
    }
    printf("  +----------------------------------------------+\n");
}

ConsoleMqttClient::ConsoleMqttClient()
    : connected_(true), quiet_(false), sent_(0u) {
    lastTopic_[0]   = '\0';
    lastPayload_[0] = '\0';
}

Status ConsoleMqttClient::publish(const char* topic, const char* payload,
                                  uint8_t qos, bool retain) {
    if (!connected_) { return Status::BusError; }
    if (topic == 0 || payload == 0) { return Status::InvalidArgument; }

    snprintf(lastTopic_, sizeof(lastTopic_), "%s", topic);
    snprintf(lastPayload_, sizeof(lastPayload_), "%s", payload);
    ++sent_;

    if (!quiet_) {
        printf("  [MQTT] %s (qos=%u retain=%u)\n         %s\n",
               topic, static_cast<unsigned>(qos), retain ? 1u : 0u, payload);
    }
    return Status::Ok;
}

ConsoleSwitchOutput::ConsoleSwitchOutput(const char* name)
    : name_(name), on_(false), quiet_(false), transitions_(0u) {}

Status ConsoleSwitchOutput::set(bool on) {
    if (on_ == on) { return Status::Ok; }
    on_ = on;
    ++transitions_;
    if (!quiet_) {
        printf("  [CTRL] %s -> %s\n", name_, on ? "ON" : "OFF");
    }
    return Status::Ok;
}

} // namespace host
} // namespace port
} // namespace sensorfw
