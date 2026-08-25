/**
 * @file main.cpp
 * @brief Host demo - the composition root of the whole framework.
 *
 * Everything that knows how the parts fit together lives here and nowhere else.
 * The framework, the drivers, the filters and the three application components
 * contain no wiring at all, which is what makes them reusable across products.
 *
 * The demo runs on a virtual clock: 70 s of scenario replay in a few
 * milliseconds, deterministically.
 *
 * Note: printf("%f") is used for the report tables. That is a host-side
 * convenience - the framework itself never formats a float (see the integer
 * formatting in temperature_view.cpp and mqtt_reporter.cpp).
 */
#include <stdio.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "app/gui/temperature_view.hpp"
#include "app/logic/thermostat.hpp"
#include "app/mqtt/mqtt_reporter.hpp"
#include "drivers/lm75/lm75.hpp"
#include "drivers/ntc/ntc_thermistor.hpp"
#include "port/host/host_clock.hpp"
#include "port/host/host_peripherals.hpp"
#include "port/host/scenario.hpp"
#include "port/host/simulated_adc.hpp"
#include "port/host/simulated_i2c.hpp"
#include "sensorfw/filter/ewma.hpp"
#include "sensorfw/filter/filter_chain.hpp"
#include "sensorfw/filter/kalman1d.hpp"
#include "sensorfw/filter/median.hpp"
#include "sensorfw/filter/moving_average.hpp"
#include "sensorfw/filter/outlier_gate.hpp"
#include "sensorfw/filter/pass_through.hpp"
#include "sensorfw/filter/slew_rate_limiter.hpp"
#include "sensorfw/service/sensor_service.hpp"

using namespace sensorfw;
using namespace sensorfw::app;
using namespace sensorfw::port::host;

namespace {

const uint32_t kSamplePeriodMs = 100u;   // 10 Hz acquisition
const uint32_t kRunDurationMs  = 70000u; // 70 s of scenario

#if defined(_WIN32)
/**
 * @brief True when this process is the only one attached to its console.
 *
 * That is what distinguishes a double click in Explorer - which creates a
 * console owned by this process alone and tears it down the moment main()
 * returns - from being started inside an existing cmd or PowerShell, where the
 * shell shares the console and the output survives.
 *
 * Without this check the demo looks broken when double clicked: it prints its
 * four scenes correctly and the window vanishes before anyone can read them.
 */
bool ownsItsConsole() {
    DWORD processIds[4];
    return GetConsoleProcessList(processIds, 4) == 1u;
}
#endif
void banner(const char* title) {
    printf("\n============================================================\n");
    printf(" %s\n", title);
    printf("============================================================\n");
}

/**
 * @brief Test subscriber that scores a filter against the ground truth the
 *        simulator knows but the firmware does not.
 */
class ErrorScorer {
public:
    explicit ErrorScorer(const SimulatedNtcAdc& adc)
        : adc_(adc), sumSq_(0.0), maxAbs_(0.0), count_(0u), warmupUntilMs_(3000u) {}

    Status attachTo(MeasurementPublisher& publisher) {
        return publisher.subscribe(
            MeasurementPublisher::Subscriber::bind<ErrorScorer,
                                                   &ErrorScorer::onMeasurement>(this));
    }

    void onMeasurement(const Measurement& measurement) {
        if (!isOk(measurement.status)) { return; }
        if (measurement.timestamp < warmupUntilMs_) { return; }  // ignore settling

        const double error = static_cast<double>(measurement.filtered) -
                             static_cast<double>(adc_.lastTrueValue());
        sumSq_ += error * error;
        if (fabs(error) > maxAbs_) { maxAbs_ = fabs(error); }
        ++count_;
    }

    double rmse() const { return (count_ == 0u) ? 0.0 : sqrt(sumSq_ / count_); }
    double maxAbsError() const { return maxAbs_; }
    uint32_t count() const { return count_; }

private:
    const SimulatedNtcAdc& adc_;
    double   sumSq_;
    double   maxAbs_;
    uint32_t count_;
    uint32_t warmupUntilMs_;
};

/** @brief Replay the whole scenario through one filter and score the result. */
void evaluateFilter(const char* label, IFilter& filter) {
    VirtualClock       clock;
    SimulatedNtcAdc    adc(clock);
    drivers::NtcThermistorSensor sensor(adc, clock);

    MeasurementPublisher publisher;
    ErrorScorer          scorer(adc);
    scorer.attachTo(publisher);

    SensorServiceConfig config;
    config.samplePeriodMs = kSamplePeriodMs;
    SensorService service(sensor, filter, clock, publisher, config);

    filter.reset();
    service.begin();

    while (clock.nowMs() < kRunDurationMs) {
        service.poll();
        clock.advance(10u);          // the main loop runs faster than the sensor
    }

    printf("  %-26s  RMSE %6.3f degC   worst %6.3f degC   n=%lu\n",
           label, scorer.rmse(), scorer.maxAbsError(),
           static_cast<unsigned long>(scorer.count()));
}

} // namespace

/* ------------------------------------------------------------------------- */
/* 1. Filter comparison                                                       */
/* ------------------------------------------------------------------------- */
static void demoFilterComparison() {
    banner("1. Filter comparison - identical input, seven configurations");
    printf("  Signal: 22 degC with a 0.6 degC/60s drift, a -3 degC step at t=40s,\n"
           "          +/-0.45 degC white noise and a +/-9 degC spike every 17 samples.\n"
           "  Scored against the ground truth the firmware cannot see.\n\n");

    PassThroughFilter raw;
    evaluateFilter("raw (no filter)", raw);

    MovingAverageFilter<8> movingAverage;
    evaluateFilter("moving-average(8)", movingAverage);

    MedianFilter<5> median;
    evaluateFilter("median(5)", median);

    EwmaFilter ewma = EwmaFilter::withTimeConstant(1500u);
    evaluateFilter("ewma(tau=1.5s)", ewma);

    Kalman1dFilter kalman(static_cast<Real>(0.05), static_cast<Real>(0.09));
    evaluateFilter("kalman(Q=0.05,R=0.09)", kalman);

    /* The chain is the point of the exercise: a median kills the spikes, then
     * an EWMA smooths the residual white noise, then a slew limiter caps how
     * fast the published value may move. Each stage does one thing. */
    MedianFilter<5> chainMedian;
    EwmaFilter      chainEwma = EwmaFilter::withTimeConstant(1200u);
    SlewRateLimiter chainSlew(static_cast<Real>(3));
    FilterChain     chain;
    chain.append(&chainMedian);
    chain.append(&chainEwma);
    chain.append(&chainSlew);
    evaluateFilter("median(5)+ewma+slew", chain);

    OutlierGate     gate(static_cast<Real>(2), 3u);
    Kalman1dFilter  gatedKalman(static_cast<Real>(0.05), static_cast<Real>(0.09));
    FilterChain     gatedChain;
    gatedChain.append(&gate);
    gatedChain.append(&gatedKalman);
    evaluateFilter("outlier-gate+kalman", gatedChain);

    printf("\n  Swapping any of these into a running system is one call to\n"
           "  SensorService::setFilter(). No driver or application code changes.\n");
}

/* ------------------------------------------------------------------------- */
/* 2. Three decoupled consumers on one channel                                */
/* ------------------------------------------------------------------------- */
static void demoApplicationLayer() {
    banner("2. One measurement channel, three independent consumers");

    /* ---- platform ---- */
    VirtualClock    clock;
    SimulatedNtcAdc adc(clock);

    /* ---- driver ---- */
    drivers::NtcThermistorSensor sensor(adc, clock, drivers::NtcConfig(), "ntc-10k@adc1");

    /* ---- signal conditioning ---- */
    MedianFilter<5> median;
    EwmaFilter      smooth = EwmaFilter::withTimeConstant(1200u);
    FilterChain     chain;
    chain.append(&median);
    chain.append(&smooth);

    /* ---- the channel ---- */
    MeasurementPublisher publisher;

    /* ---- acquisition ---- */
    SensorServiceConfig config;
    config.samplePeriodMs = kSamplePeriodMs;
    SensorService service(sensor, chain, clock, publisher, config);

    /* ---- consumers: none of them knows the others exist ---- */
    ConsoleDisplay   panel;
    TemperatureView  gui(panel, 10000u);
    gui.attachTo(publisher);

    ConsoleMqttClient  mqtt;
    MqttReporterConfig mqttConfig;
    mqttConfig.topic       = "blueair/dev-0001/sensor/temperature";
    mqttConfig.deadBandC   = static_cast<Real>(0.25);
    mqttConfig.heartbeatMs = 20000u;
    MqttReporter reporter(mqtt, mqttConfig);
    reporter.attachTo(publisher);

    ConsoleSwitchOutput      heater("heater-relay");
    ThermostatEventPublisher events;
    ThermostatConfig         thermostatConfig;
    thermostatConfig.setpointC       = static_cast<Real>(21.0);
    thermostatConfig.hysteresisC     = static_cast<Real>(0.6);
    thermostatConfig.minOnTimeMs     = 3000u;
    thermostatConfig.minOffTimeMs    = 5000u;
    thermostatConfig.sensorTimeoutMs = 3000u;
    Thermostat thermostat(heater, events, thermostatConfig);
    thermostat.attachTo(publisher);

    printf("  publisher: %u/%u subscribers registered (GUI, MQTT, thermostat)\n\n",
           static_cast<unsigned>(publisher.subscriberCount()),
           static_cast<unsigned>(MeasurementPublisher::capacity()));

    service.begin();

    while (clock.nowMs() < kRunDurationMs) {
        service.poll();
        clock.advance(10u);
    }

    printf("\n  --- counters after %lu s of run time -------------------\n",
           static_cast<unsigned long>(kRunDurationMs / 1000u));
    printf("  samples accepted : %lu\n",
           static_cast<unsigned long>(service.stats().samplesAccepted));
    printf("  GUI redraws      : %lu   (rate limited + change detected)\n",
           static_cast<unsigned long>(gui.redrawCount()));
    printf("  MQTT messages    : %lu   (dead band + heartbeat, %lu suppressed)\n",
           static_cast<unsigned long>(reporter.publishedCount()),
           static_cast<unsigned long>(reporter.suppressedCount()));
    printf("  relay switches   : %lu   (hysteresis + dwell times)\n",
           static_cast<unsigned long>(heater.transitions()));
    printf("  Same data, three consumption policies, zero coupling.\n");
}

/* ------------------------------------------------------------------------- */
/* 3. Fault injection: sensor failure, fail-safe, recovery, offline MQTT       */
/* ------------------------------------------------------------------------- */
static void demoFaultHandling() {
    banner("3. Fault handling - broken sensor, fail-safe output, offline link");

    VirtualClock    clock;
    SimulatedNtcAdc adc(clock);
    drivers::NtcThermistorSensor sensor(adc, clock, drivers::NtcConfig(), "ntc-10k@adc1");

    EwmaFilter filter = EwmaFilter::withTimeConstant(800u);
    MeasurementPublisher publisher;

    SensorServiceConfig config;
    config.samplePeriodMs  = kSamplePeriodMs;
    config.faultThreshold  = 3u;
    config.reinitBackoffMs = 2000u;
    SensorService service(sensor, filter, clock, publisher, config);

    ConsoleDisplay  panel;
    panel.setQuiet(true);
    TemperatureView gui(panel, 1000u);
    gui.attachTo(publisher);

    ConsoleMqttClient mqtt;
    mqtt.setQuiet(true);
    MqttReporterConfig mqttConfig;
    mqttConfig.topic       = "blueair/dev-0001/sensor/temperature";
    mqttConfig.heartbeatMs = 60000u;
    MqttReporter reporter(mqtt, mqttConfig);
    reporter.attachTo(publisher);

    ConsoleSwitchOutput      heater("heater-relay");
    ThermostatEventPublisher events;
    ThermostatConfig         thermostatConfig;
    thermostatConfig.setpointC       = static_cast<Real>(25.0);  // force the heater on
    thermostatConfig.minOnTimeMs     = 0u;
    thermostatConfig.minOffTimeMs    = 0u;
    thermostatConfig.sensorTimeoutMs = 1000u;
    Thermostat thermostat(heater, events, thermostatConfig);
    thermostat.attachTo(publisher);

    service.begin();

    printf("  t=0s     healthy operation, heater should start\n");
    while (clock.nowMs() < 3000u) { service.poll(); clock.advance(10u); }
    printf("           heater=%s  service.healthy=%s\n\n",
           heater.state() ? "ON" : "OFF", service.healthy() ? "yes" : "no");

    printf("  t=3s     ADC starts returning BusError (connector pulled out)\n");
    adc.injectFault(true);
    mqtt.setConnected(false);
    printf("  t=3s     MQTT link also goes down\n");
    while (clock.nowMs() < 8000u) { service.poll(); clock.advance(10u); }
    printf("           service.healthy=%s  read errors=%lu  reinit attempts=%lu\n",
           service.healthy() ? "yes" : "no",
           static_cast<unsigned long>(service.stats().readErrors),
           static_cast<unsigned long>(service.stats().reinitAttempts));
    printf("           thermostat.sensorLost=%s  heater=%s  <-- fail-safe\n",
           thermostat.sensorLost() ? "yes" : "no", heater.state() ? "ON" : "OFF");
    printf("           MQTT dropped=%lu (kept as pending, not queued per sample)\n\n",
           static_cast<unsigned long>(reporter.droppedCount()));

    printf("  t=8s     connector plugged back in, link restored\n");
    adc.injectFault(false);
    mqtt.setConnected(true);
    while (clock.nowMs() < 14000u) { service.poll(); clock.advance(10u); }
    printf("           service.healthy=%s  heater=%s  MQTT sent=%lu\n",
           service.healthy() ? "yes" : "no",
           heater.state() ? "ON" : "OFF",
           static_cast<unsigned long>(mqtt.sentCount()));
    printf("           last payload: %s\n", reporter.lastPayload());
}

/* ------------------------------------------------------------------------- */
/* 4. Swapping the physical sensor without touching anything above            */
/* ------------------------------------------------------------------------- */
static void demoDriverSwap() {
    banner("4. Analog NTC -> digital LM75B: one line changes");

    VirtualClock     clock;
    SimulatedLm75Bus bus(clock);

    /* The only difference from demo 2: this object. Everything below is
     * character-for-character the same code. */
    drivers::Lm75Sensor sensor(bus, clock, drivers::Lm75Sensor::kDefaultAddress,
                               "lm75b@i2c1-0x48");

    EwmaFilter           filter = EwmaFilter::withTimeConstant(1000u);
    MeasurementPublisher publisher;

    SensorServiceConfig config;
    config.samplePeriodMs = 200u;   // above the 100 ms floor the driver declares
    SensorService service(sensor, filter, clock, publisher, config);

    ConsoleDisplay  panel;
    TemperatureView gui(panel, 5000u);
    gui.attachTo(publisher);

    ConsoleMqttClient mqtt;
    mqtt.setQuiet(true);
    MqttReporter reporter(mqtt);
    reporter.attachTo(publisher);

    const Status status = service.begin();
    printf("  init: %s   device probed over I2C, %lu transfers so far\n",
           toString(status), static_cast<unsigned long>(bus.transferCount()));

    while (clock.nowMs() < 12000u) { service.poll(); clock.advance(10u); }

    printf("  samples=%lu  last payload: %s\n",
           static_cast<unsigned long>(service.stats().samplesAccepted),
           reporter.lastPayload());

    printf("\n  Device missing on the bus:\n");
    bus.setPresent(false);
    SensorService probe(sensor, filter, clock, publisher, config);
    printf("  init: %s   <-- detected at boot, not in the field\n",
           toString(probe.begin()));
}

int main() {
    printf("\n  MCU sensor framework - host demo\n");
    printf("  Layers: HAL | driver | filter | service | publisher | application\n");

    demoFilterComparison();
    demoApplicationLayer();
    demoFaultHandling();
    demoDriverSwap();

    printf("\n  Done. Run the unit tests with: ctest --test-dir build --output-on-failure\n\n");

#if defined(_WIN32)
    /* Keep a double-clicked window open long enough to read. Started from a
     * shell, or from CI, the console is shared and this is skipped - so this
     * never blocks a script. */
    if (ownsItsConsole()) {
        printf("  Press Enter to close this window.\n");
        (void)getchar();
    }
#endif
    return 0;
}
