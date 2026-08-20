/**
 * @file host_peripherals.hpp
 * @brief Host implementations of the three application-side interfaces.
 *
 * On target these become an SPI OLED, the ESP-MQTT client and a relay GPIO.
 * Here they print to the terminal, which is all the demo and the tests need.
 */
#ifndef SENSORFW_PORT_HOST_PERIPHERALS_HPP
#define SENSORFW_PORT_HOST_PERIPHERALS_HPP

#include "app/gui/display.hpp"
#include "app/logic/thermostat.hpp"
#include "app/mqtt/mqtt_client.hpp"

namespace sensorfw {
namespace port {
namespace host {

/** @brief 3-row character panel rendered on stdout. */
class ConsoleDisplay : public app::IDisplay {
public:
    static const uint8_t kRows = 3u;
    static const uint8_t kCols = 48u;

    ConsoleDisplay();

    void clear();
    void drawText(uint8_t column, uint8_t row, const char* text);
    void flush();

    /** @brief Silence the panel output (used while the demo prints a table). */
    void setQuiet(bool quiet) { quiet_ = quiet; }
    uint32_t flushCount() const { return flushCount_; }
    const char* row(uint8_t index) const { return buffer_[index]; }

private:
    char     buffer_[kRows][kCols + 1u];
    uint32_t flushCount_;
    bool     quiet_;
};

/** @brief MQTT stub with a switchable link, to exercise the offline path. */
class ConsoleMqttClient : public app::IMqttClient {
public:
    ConsoleMqttClient();

    bool connected() const { return connected_; }
    Status publish(const char* topic, const char* payload, uint8_t qos, bool retain);

    void setConnected(bool connected) { connected_ = connected; }
    void setQuiet(bool quiet) { quiet_ = quiet; }
    uint32_t sentCount() const { return sent_; }
    const char* lastTopic() const { return lastTopic_; }
    const char* lastPayload() const { return lastPayload_; }

private:
    bool     connected_;
    bool     quiet_;
    uint32_t sent_;
    char     lastTopic_[64];
    char     lastPayload_[192];
};

/** @brief Relay stub. */
class ConsoleSwitchOutput : public app::ISwitchOutput {
public:
    explicit ConsoleSwitchOutput(const char* name = "heater");

    Status set(bool on);
    bool state() const { return on_; }

    void setQuiet(bool quiet) { quiet_ = quiet; }
    uint32_t transitions() const { return transitions_; }

private:
    const char* name_;
    bool        on_;
    bool        quiet_;
    uint32_t    transitions_;
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_PERIPHERALS_HPP
