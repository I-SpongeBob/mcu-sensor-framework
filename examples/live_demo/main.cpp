/**
 * @file main.cpp
 * @brief Live terminal dashboard - the same stack as examples/host_demo, but
 *        running in real time and driveable from the keyboard.
 *
 * What this demo is really showing: every component below the presentation
 * layer is the production one, unmodified. Switching the filter with a keypress
 * is a single call to SensorService::setFilter(); pulling the sensor or the
 * network link out from under the stack exercises the real fault paths.
 *
 * Keys:  1..5 filter profile   f sensor fault   m MQTT link
 *        + -  setpoint         r reset chart    q quit
 */
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "app/gui/temperature_view.hpp"
#include "app/logic/thermostat.hpp"
#include "app/mqtt/mqtt_reporter.hpp"
#include "drivers/ntc/ntc_thermistor.hpp"
#include "port/host/dashboard.hpp"
#include "port/host/host_clock.hpp"
#include "port/host/host_peripherals.hpp"
#include "port/host/simulated_adc.hpp"
#include "sensorfw/filter/ewma.hpp"
#include "sensorfw/filter/filter_chain.hpp"
#include "sensorfw/filter/kalman1d.hpp"
#include "sensorfw/filter/median.hpp"
#include "sensorfw/filter/pass_through.hpp"
#include "sensorfw/filter/slew_rate_limiter.hpp"
#include "sensorfw/service/sensor_service.hpp"

using namespace sensorfw;
using namespace sensorfw::app;
using namespace sensorfw::port::host;

namespace {

void sleepMs(uint32_t milliseconds) {
#if defined(_WIN32)
    Sleep(static_cast<DWORD>(milliseconds));
#else
    usleep(static_cast<useconds_t>(milliseconds) * 1000u);
#endif
}

/**
 * @brief Runs the scenario faster than wall clock so the 70 s profile - drift,
 *        noise, spikes and the step at t=40 s - plays out in about 20 s.
 *
 * It is also a reminder of why the framework takes its time from an injected
 * IClock: nothing below this class knows or cares that time is being scaled.
 */
class SpeedClock : public hal::IClock {
public:
    SpeedClock(hal::IClock& base, uint32_t factor) : base_(base), factor_(factor) {}
    TimestampMs nowMs() const { return base_.nowMs() * factor_; }

private:
    hal::IClock& base_;
    uint32_t     factor_;
};

struct FilterProfile {
    const char* name;
    IFilter*    filter;
};

const char* statusColour(bool good) {
    return good ? AnsiTerminal::green() : AnsiTerminal::red();
}

} // namespace

int main() {
    /* ---------------- platform ---------------- */
    HostClock       wallClock;
    SpeedClock      clock(wallClock, 3u);          // 3x so the demo is watchable
    SimulatedNtcAdc adc(clock);

    /* ---------------- driver ------------------ */
    drivers::NtcThermistorSensor sensor(adc, clock, drivers::NtcConfig(), "ntc-10k@adc1");

    /* ---------------- filter profiles --------- */
    PassThroughFilter passthrough;
    MedianFilter<5>   median;
    EwmaFilter        ewma   = EwmaFilter::withTimeConstant(1200u);
    Kalman1dFilter    kalman(static_cast<Real>(0.05), static_cast<Real>(0.09));

    MedianFilter<5>   chainMedian;
    EwmaFilter        chainEwma = EwmaFilter::withTimeConstant(1000u);
    SlewRateLimiter   chainSlew(static_cast<Real>(3));
    FilterChain       chain;
    chain.append(&chainMedian);
    chain.append(&chainEwma);
    chain.append(&chainSlew);

    const FilterProfile profiles[5] = {
        { "passthrough (raw)",   &passthrough },
        { "median(5)",           &median      },
        { "ewma(tau=1.2s)",      &ewma        },
        { "kalman(Q.05,R.09)",   &kalman      },
        { "median+ewma+slew",    &chain       }
    };
    uint8_t activeProfile = 4u;

    /* ---------------- channel + service ------- */
    MeasurementPublisher publisher;
    SensorServiceConfig  config;
    config.samplePeriodMs  = 100u;
    config.reinitBackoffMs = 2000u;
    SensorService service(sensor, *profiles[activeProfile].filter, clock, publisher, config);

    /* ---------------- consumers --------------- */
    ConsoleDisplay panel;
    panel.setQuiet(true);                 // the dashboard composes the frame itself
    TemperatureView gui(panel, 200u);
    gui.attachTo(publisher);

    ConsoleMqttClient mqtt;
    mqtt.setQuiet(true);
    MqttReporterConfig mqttConfig;
    mqttConfig.topic       = "blueair/dev-0001/sensor/temperature";
    mqttConfig.deadBandC   = static_cast<Real>(0.25);
    mqttConfig.heartbeatMs = 15000u;
    MqttReporter reporter(mqtt, mqttConfig);
    reporter.attachTo(publisher);

    ConsoleSwitchOutput heater("heater-relay");
    heater.setQuiet(true);
    ThermostatEventPublisher events;
    ThermostatConfig thermostatConfig;
    thermostatConfig.setpointC       = static_cast<Real>(21.0);
    thermostatConfig.hysteresisC     = static_cast<Real>(0.6);
    thermostatConfig.minOnTimeMs     = 3000u;
    thermostatConfig.minOffTimeMs    = 3000u;
    thermostatConfig.sensorTimeoutMs = 2000u;
    Thermostat thermostat(heater, events, thermostatConfig);
    thermostat.attachTo(publisher);

    /* The fourth subscriber, added without touching any of the other three. */
    ChartRecorder chart;
    chart.attachTo(publisher);

    /* ---------------- run --------------------- */
    AnsiTerminal terminal;
    KeyPoller    keyboard;
    terminal.begin();

    service.begin();

    bool     running       = true;
    bool     faultInjected = false;
    uint32_t frames        = 0u;

    while (running) {
        service.poll();

        const char key = keyboard.poll();
        switch (key) {
            case 'q': case 'Q': case 3: running = false; break;
            case '1': case '2': case '3': case '4': case '5':
                activeProfile = static_cast<uint8_t>(key - '1');
                service.setFilter(*profiles[activeProfile].filter);
                chart.clear();
                break;
            case 'f': case 'F':
                faultInjected = !faultInjected;
                adc.injectFault(faultInjected);
                break;
            case 'm': case 'M':
                mqtt.setConnected(!mqtt.connected());
                break;
            case '+': case '=':
                thermostat.setSetpoint(thermostat.setpoint() + static_cast<Real>(0.5));
                break;
            case '-': case '_':
                thermostat.setSetpoint(thermostat.setpoint() - static_cast<Real>(0.5));
                break;
            case 'r': case 'R':
                chart.clear();
                break;
            default: break;
        }

        /* ------------- render one frame ------------- */
        ++frames;
        char lineBuffer[220];
        const Measurement& last = service.lastMeasurement();

        AnsiTerminal::home();

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "%s MCU sensor framework - live dashboard %s  t=%6.1fs  frame %lu",
                 AnsiTerminal::bold(), AnsiTerminal::reset(),
                 static_cast<double>(clock.nowMs()) / 1000.0,
                 static_cast<unsigned long>(frames));
        AnsiTerminal::line(lineBuffer);
        AnsiTerminal::line("");

        /* The GUI component drew into its IDisplay exactly as it would on an
         * OLED; the dashboard just reads the resulting character buffer. */
        AnsiTerminal::line("  GUI  (TemperatureView -> IDisplay, unmodified)");
        AnsiTerminal::line("  +----------------------------------------------+");
        for (uint8_t row = 0u; row < ConsoleDisplay::kRows; ++row) {
            snprintf(lineBuffer, sizeof(lineBuffer), "  | %-44.44s |", panel.row(row));
            AnsiTerminal::line(lineBuffer);
        }
        AnsiTerminal::line("  +----------------------------------------------+");
        AnsiTerminal::line("");

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  CHART  %s. raw%s  %s# filtered%s   filter: %s%s%s",
                 AnsiTerminal::dim(), AnsiTerminal::reset(),
                 "\033[1;36m", AnsiTerminal::reset(),
                 AnsiTerminal::yellow(), profiles[activeProfile].name,
                 AnsiTerminal::reset());
        AnsiTerminal::line(lineBuffer);

        for (uint8_t row = 0u; row < ChartRecorder::kHeight; ++row) {
            const char* axis = "      ";
            char axisBuffer[16];
            if (row == 0u) {
                snprintf(axisBuffer, sizeof(axisBuffer), "%5.1f ",
                         static_cast<double>(chart.maxValue()));
                axis = axisBuffer;
            } else if (row == ChartRecorder::kHeight - 1u) {
                snprintf(axisBuffer, sizeof(axisBuffer), "%5.1f ",
                         static_cast<double>(chart.minValue()));
                axis = axisBuffer;
            }
            /* Chart rows carry a colour escape per cell, so they need far more
             * room than a plain 80-column line. */
            char chartLine[1024];
            snprintf(chartLine, sizeof(chartLine), "  %s|%s", axis, chart.renderRow(row));
            AnsiTerminal::line(chartLine);
        }
        snprintf(lineBuffer, sizeof(lineBuffer), "        +%.*s",
                 static_cast<int>(ChartRecorder::kWidth),
                 "--------------------------------------------------------------");
        AnsiTerminal::line(lineBuffer);
        AnsiTerminal::line("");

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  SENSOR  %s%-9s%s  accepted %-5lu  rejected %-3lu  errors %-3lu  retries %lu",
                 statusColour(service.healthy()),
                 service.healthy() ? "healthy" : "FAULT",
                 AnsiTerminal::reset(),
                 static_cast<unsigned long>(service.stats().samplesAccepted),
                 static_cast<unsigned long>(service.stats().samplesRejected),
                 static_cast<unsigned long>(service.stats().readErrors),
                 static_cast<unsigned long>(service.stats().reinitAttempts));
        AnsiTerminal::line(lineBuffer);

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  MQTT    %s%-9s%s  sent %-5lu  suppressed %-5lu  dropped %lu",
                 statusColour(mqtt.connected()),
                 mqtt.connected() ? "link UP" : "link DOWN",
                 AnsiTerminal::reset(),
                 static_cast<unsigned long>(reporter.publishedCount()),
                 static_cast<unsigned long>(reporter.suppressedCount()),
                 static_cast<unsigned long>(reporter.droppedCount()));
        AnsiTerminal::line(lineBuffer);

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  CTRL    heater %s%-9s%s  setpoint %4.1f C  switches %-3lu %s%s%s",
                 heater.state() ? AnsiTerminal::red() : AnsiTerminal::dim(),
                 heater.state() ? "ON" : "off",
                 AnsiTerminal::reset(),
                 static_cast<double>(thermostat.setpoint()),
                 static_cast<unsigned long>(heater.transitions()),
                 AnsiTerminal::red(),
                 thermostat.sensorLost() ? "SENSOR LOST - failsafe" :
                     (thermostat.alarmActive() ? "OVER TEMPERATURE" : ""),
                 AnsiTerminal::reset());
        AnsiTerminal::line(lineBuffer);

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  LAST    raw %6.2f C   filtered %6.2f C   status %s",
                 static_cast<double>(last.raw),
                 static_cast<double>(last.filtered),
                 toString(last.status));
        AnsiTerminal::line(lineBuffer);
        AnsiTerminal::line("");

        snprintf(lineBuffer, sizeof(lineBuffer),
                 "  %s[1-5]%s filter   %s[f]%s sensor fault %s  %s[m]%s mqtt link   "
                 "%s[+/-]%s setpoint   %s[r]%s reset   %s[q]%s quit",
                 AnsiTerminal::cyan(), AnsiTerminal::reset(),
                 AnsiTerminal::cyan(), AnsiTerminal::reset(),
                 faultInjected ? "(injected)" : "          ",
                 AnsiTerminal::cyan(), AnsiTerminal::reset(),
                 AnsiTerminal::cyan(), AnsiTerminal::reset(),
                 AnsiTerminal::cyan(), AnsiTerminal::reset(),
                 AnsiTerminal::cyan(), AnsiTerminal::reset());
        AnsiTerminal::line(lineBuffer);

        fflush(stdout);
        sleepMs(40u);
    }

    terminal.end();
    printf("\n  Filter profile at exit: %s\n", profiles[activeProfile].name);
    printf("  samples=%lu  mqtt=%lu  relay switches=%lu\n\n",
           static_cast<unsigned long>(service.stats().samplesAccepted),
           static_cast<unsigned long>(reporter.publishedCount()),
           static_cast<unsigned long>(heater.transitions()));
    return 0;
}
