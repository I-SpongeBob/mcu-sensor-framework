/**
 * @file test_app.cpp
 * @brief The three application consumers, each tested through the publisher
 *        alone - which is the proof that they are decoupled: not one of these
 *        tests needs a sensor, a driver or an ADC.
 */
#include "test_support.hpp"

#include "app/gui/temperature_view.hpp"
#include "app/logic/thermostat.hpp"
#include "app/mqtt/mqtt_reporter.hpp"
#include "port/host/host_peripherals.hpp"

using namespace sensorfw;
using namespace sensorfw::app;
using namespace sensorfw::port::host;

namespace {

Measurement measurementOf(Real value, TimestampMs timestamp,
                          Status status = Status::Ok) {
    Measurement measurement;
    measurement.quantity  = Quantity::Temperature;
    measurement.raw       = value;
    measurement.filtered  = value;
    measurement.timestamp = timestamp;
    measurement.status    = status;
    measurement.source    = "unit-test";
    return measurement;
}

/* ---------------------------------------------------------------- GUI ---- */

void guiSkipsRedrawsWhenNothingVisibleChanged() {
    ConsoleDisplay display;
    display.setQuiet(true);
    MeasurementPublisher publisher;
    TemperatureView view(display, 0u);      // no rate limit, isolate the dedup
    view.attachTo(publisher);

    publisher.publish(measurementOf(21.44f, 0u));
    CHECK_EQ(view.redrawCount(), 1u);

    /* Noise in the third decimal must not move a display that shows 0.1 degC. */
    publisher.publish(measurementOf(21.441f, 100u));
    publisher.publish(measurementOf(21.437f, 200u));
    CHECK_EQ(view.redrawCount(), 1u);

    publisher.publish(measurementOf(21.55f, 300u));
    CHECK_EQ(view.redrawCount(), 2u);
}

void guiRateLimitsThePanel() {
    ConsoleDisplay display;
    display.setQuiet(true);
    MeasurementPublisher publisher;
    TemperatureView view(display, 1000u);    // at most one redraw per second
    view.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));
    for (uint32_t t = 100u; t < 1000u; t += 100u) {
        publisher.publish(measurementOf(20.0f + static_cast<Real>(t) / 100.0f, t));
    }
    CHECK_EQ(view.redrawCount(), 1u);

    publisher.publish(measurementOf(30.0f, 1200u));
    CHECK_EQ(view.redrawCount(), 2u);
}

void guiShowsAStatusChangeImmediately() {
    ConsoleDisplay display;
    display.setQuiet(true);
    MeasurementPublisher publisher;
    TemperatureView view(display, 60000u);   // a very slow panel
    view.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));
    CHECK_EQ(view.redrawCount(), 1u);

    /* A sensor fault bypasses the rate limit - the user must see it now. */
    publisher.publish(measurementOf(20.0f, 500u, Status::BusError));
    CHECK_EQ(view.redrawCount(), 2u);
}

void guiFormatsNegativeAndFractionalValuesWithoutFloatPrintf() {
    ConsoleDisplay display;
    display.setQuiet(true);
    MeasurementPublisher publisher;
    TemperatureView view(display, 0u);
    view.attachTo(publisher);

    publisher.publish(measurementOf(-3.45f, 0u));
    CHECK_STREQ(view.lastRenderedValue(), "-3.5");

    publisher.publish(measurementOf(0.04f, 100u));
    CHECK_STREQ(view.lastRenderedValue(), "0.0");

    publisher.publish(measurementOf(21.96f, 200u));
    CHECK_STREQ(view.lastRenderedValue(), "22.0");
}

/* --------------------------------------------------------------- MQTT ---- */

void mqttPublishesOnlyOutsideTheDeadBand() {
    ConsoleMqttClient client;
    client.setQuiet(true);
    MeasurementPublisher publisher;

    MqttReporterConfig config;
    config.deadBandC   = 0.5f;
    config.heartbeatMs = 1000000u;          // effectively disabled
    MqttReporter reporter(client, config);
    reporter.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));      // the first one always goes
    CHECK_EQ(reporter.publishedCount(), 1u);

    publisher.publish(measurementOf(20.3f, 1000u));   // inside the dead band
    publisher.publish(measurementOf(19.7f, 2000u));
    CHECK_EQ(reporter.publishedCount(), 1u);
    CHECK_EQ(reporter.suppressedCount(), 2u);

    publisher.publish(measurementOf(20.6f, 3000u));   // outside
    CHECK_EQ(reporter.publishedCount(), 2u);
}

void mqttSendsAHeartbeatWhenNothingChanges() {
    ConsoleMqttClient client;
    client.setQuiet(true);
    MeasurementPublisher publisher;

    MqttReporterConfig config;
    config.deadBandC   = 5.0f;
    config.heartbeatMs = 10000u;
    MqttReporter reporter(client, config);
    reporter.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));
    for (TimestampMs t = 1000u; t < 10000u; t += 1000u) {
        publisher.publish(measurementOf(20.0f, t));
    }
    CHECK_EQ(reporter.publishedCount(), 1u);

    /* The cloud has to be able to tell "unchanged" from "device is dead". */
    publisher.publish(measurementOf(20.0f, 10000u));
    CHECK_EQ(reporter.publishedCount(), 2u);
}

void mqttKeepsOneSamplePendingWhileOffline() {
    ConsoleMqttClient client;
    client.setQuiet(true);
    MeasurementPublisher publisher;

    MqttReporterConfig config;
    config.deadBandC   = 0.5f;
    config.heartbeatMs = 1000000u;
    MqttReporter reporter(client, config);
    reporter.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));
    CHECK_EQ(client.sentCount(), 1u);

    client.setConnected(false);
    for (TimestampMs t = 1000u; t <= 5000u; t += 1000u) {
        publisher.publish(measurementOf(25.0f, t));
    }
    CHECK_EQ(client.sentCount(), 1u);
    CHECK_EQ(reporter.droppedCount(), 5u);

    /* Back online: exactly one message, carrying the freshest value - not five
     * stale ones. */
    client.setConnected(true);
    publisher.publish(measurementOf(25.2f, 6000u));
    CHECK_EQ(client.sentCount(), 2u);
    CHECK_EQ(reporter.publishedCount(), 2u);
}

void mqttPayloadIsValidJsonWithBothValues() {
    ConsoleMqttClient client;
    client.setQuiet(true);
    MeasurementPublisher publisher;
    MqttReporter reporter(client);
    reporter.attachTo(publisher);

    Measurement measurement = measurementOf(21.5f, 4200u);
    measurement.raw = 30.125f;                        // a spike the filter removed
    publisher.publish(measurement);

    CHECK_STREQ(client.lastPayload(),
                "{\"src\":\"unit-test\",\"unit\":\"degC\",\"value\":21.500,"
                "\"raw\":30.125,\"ts\":4200,\"status\":\"Ok\"}");
}

void mqttReportsSensorFaultsImmediately() {
    ConsoleMqttClient client;
    client.setQuiet(true);
    MeasurementPublisher publisher;

    MqttReporterConfig config;
    config.deadBandC   = 100.0f;            // nothing would ever trigger on value
    config.heartbeatMs = 1000000u;
    MqttReporter reporter(client, config);
    reporter.attachTo(publisher);

    publisher.publish(measurementOf(20.0f, 0u));
    CHECK_EQ(reporter.publishedCount(), 1u);

    publisher.publish(measurementOf(20.0f, 1000u, Status::BusError));
    CHECK_EQ(reporter.publishedCount(), 2u);
}

/* ---------------------------------------------------------- thermostat --- */

struct ThermostatFixture {
    ConsoleSwitchOutput      heater;
    ThermostatEventPublisher events;
    MeasurementPublisher     publisher;

    ThermostatFixture() : heater("test-heater") { heater.setQuiet(true); }
};

void thermostatUsesHysteresisInsteadOfABareComparison() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC    = 22.0f;
    config.hysteresisC  = 1.0f;             // on at 21.5, off at 22.5
    config.minOnTimeMs  = 0u;
    config.minOffTimeMs = 0u;
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    fixture.publisher.publish(measurementOf(21.4f, 0u));
    CHECK(thermostat.heating());

    /* Inside the dead band nothing may move, however noisy the signal is. */
    fixture.publisher.publish(measurementOf(22.0f, 1000u));
    fixture.publisher.publish(measurementOf(22.4f, 2000u));
    CHECK(thermostat.heating());
    CHECK_EQ(fixture.heater.transitions(), 1u);

    fixture.publisher.publish(measurementOf(22.6f, 3000u));
    CHECK(!thermostat.heating());
    CHECK_EQ(fixture.heater.transitions(), 2u);
}

void thermostatRespectsTheMinimumOffTime() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC    = 22.0f;
    config.hysteresisC  = 0.4f;
    config.minOnTimeMs  = 0u;
    config.minOffTimeMs = 60000u;           // compressor protection
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    fixture.publisher.publish(measurementOf(21.0f, 0u));      // ON (first start)
    fixture.publisher.publish(measurementOf(23.0f, 1000u));   // OFF
    CHECK(!thermostat.heating());

    /* A restart request 5 s later must be held back. */
    fixture.publisher.publish(measurementOf(21.0f, 6000u));
    CHECK(!thermostat.heating());

    fixture.publisher.publish(measurementOf(21.0f, 62000u));
    CHECK(thermostat.heating());
}

void thermostatFailsSafeWhenMeasurementsStop() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC       = 25.0f;         // demand heat
    config.minOnTimeMs     = 0u;
    config.minOffTimeMs    = 0u;
    config.sensorTimeoutMs = 2000u;
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    fixture.publisher.publish(measurementOf(20.0f, 0u));
    CHECK(thermostat.heating());

    /* A single bad sample is normal and must not trip anything. */
    fixture.publisher.publish(measurementOf(20.0f, 500u, Status::BusError));
    CHECK(thermostat.heating());
    CHECK(!thermostat.sensorLost());

    /* Sustained loss does: a stuck sensor must never leave a heater running. */
    fixture.publisher.publish(measurementOf(20.0f, 2500u, Status::BusError));
    CHECK(thermostat.sensorLost());
    CHECK(!thermostat.heating());

    fixture.publisher.publish(measurementOf(20.0f, 3000u));
    CHECK(!thermostat.sensorLost());
    CHECK(thermostat.heating());
}

void thermostatLatchesAnOverTemperatureAlarm() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC    = 22.0f;
    config.hysteresisC  = 1.0f;
    config.alarmHighC   = 45.0f;
    config.minOnTimeMs  = 0u;
    config.minOffTimeMs = 0u;
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    fixture.publisher.publish(measurementOf(20.0f, 0u));
    CHECK(thermostat.heating());

    fixture.publisher.publish(measurementOf(46.0f, 1000u));
    CHECK(thermostat.alarmActive());
    CHECK(!thermostat.heating());

    /* Still latched just below the trip point - a safety limit may not chatter. */
    fixture.publisher.publish(measurementOf(44.5f, 2000u));
    CHECK(thermostat.alarmActive());
    CHECK(!thermostat.heating());

    fixture.publisher.publish(measurementOf(43.0f, 3000u));
    CHECK(!thermostat.alarmActive());
}

void thermostatEventsReachTheirOwnSubscribers() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC    = 22.0f;
    config.minOnTimeMs  = 0u;
    config.minOffTimeMs = 0u;
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    struct EventSink {
        uint32_t            count;
        ThermostatEventType last;
        EventSink() : count(0), last(ThermostatEventType::OutputOff) {}
        void onEvent(const ThermostatEvent& event) { ++count; last = event.type; }
    } sink;

    fixture.events.subscribe(
        ThermostatEventPublisher::Subscriber::bind<EventSink, &EventSink::onEvent>(&sink));

    fixture.publisher.publish(measurementOf(20.0f, 0u));
    CHECK_EQ(sink.count, 1u);
    CHECK(sink.last == ThermostatEventType::OutputOn);
}

void setpointCanBeChangedAtRuntime() {
    ThermostatFixture fixture;
    ThermostatConfig config;
    config.setpointC    = 18.0f;
    config.hysteresisC  = 0.4f;
    config.minOnTimeMs  = 0u;
    config.minOffTimeMs = 0u;
    Thermostat thermostat(fixture.heater, fixture.events, config);
    thermostat.attachTo(fixture.publisher);

    fixture.publisher.publish(measurementOf(20.0f, 0u));
    CHECK(!thermostat.heating());

    thermostat.setSetpoint(24.0f);            // the user turns the dial up
    fixture.publisher.publish(measurementOf(20.0f, 1000u));
    CHECK(thermostat.heating());
    CHECK_NEAR(thermostat.setpoint(), 24.0, 1e-4);
}

} // namespace

int main() {
    printf("test_app\n");
    RUN_TEST(guiSkipsRedrawsWhenNothingVisibleChanged);
    RUN_TEST(guiRateLimitsThePanel);
    RUN_TEST(guiShowsAStatusChangeImmediately);
    RUN_TEST(guiFormatsNegativeAndFractionalValuesWithoutFloatPrintf);
    RUN_TEST(mqttPublishesOnlyOutsideTheDeadBand);
    RUN_TEST(mqttSendsAHeartbeatWhenNothingChanges);
    RUN_TEST(mqttKeepsOneSamplePendingWhileOffline);
    RUN_TEST(mqttPayloadIsValidJsonWithBothValues);
    RUN_TEST(mqttReportsSensorFaultsImmediately);
    RUN_TEST(thermostatUsesHysteresisInsteadOfABareComparison);
    RUN_TEST(thermostatRespectsTheMinimumOffTime);
    RUN_TEST(thermostatFailsSafeWhenMeasurementsStop);
    RUN_TEST(thermostatLatchesAnOverTemperatureAlarm);
    RUN_TEST(thermostatEventsReachTheirOwnSubscribers);
    RUN_TEST(setpointCanBeChangedAtRuntime);
    TEST_SUMMARY("test_app");
}
