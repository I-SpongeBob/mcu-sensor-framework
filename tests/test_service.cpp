/**
 * @file test_service.cpp
 * @brief Scheduling, plausibility checks, fault handling and recovery.
 */
#include "test_support.hpp"

#include "port/host/host_clock.hpp"
#include "sensorfw/filter/ewma.hpp"
#include "sensorfw/filter/pass_through.hpp"
#include "sensorfw/service/sensor_service.hpp"

using namespace sensorfw;
using namespace sensorfw::port::host;

namespace {

/** Fully scripted sensor: the service is tested, not a device. */
class FakeSensor : public ITemperatureSensor {
public:
    FakeSensor()
        : value(20.0f), readStatus(Status::Ok), initStatus(Status::Ok),
          readCount(0u), initCount(0u), clock(0) {
        info_.name        = "fake";
        info_.quantity    = Quantity::Temperature;
        info_.minValue    = -40.0f;
        info_.maxValue    = 125.0f;
        info_.minPeriodMs = 50u;
    }

    Status init() { ++initCount; return initStatus; }

    Status read(Sample& out) {
        ++readCount;
        if (!isOk(readStatus)) { return readStatus; }
        out.value     = value;
        out.timestamp = (clock != 0) ? clock->nowMs() : 0u;
        return Status::Ok;
    }

    const SensorInfo& info() const { return info_; }

    Real          value;
    Status        readStatus;
    Status        initStatus;
    uint32_t      readCount;
    uint32_t      initCount;
    VirtualClock* clock;

private:
    SensorInfo info_;
};

class Counter {
public:
    Counter() : count(0), last() {}

    void onMeasurement(const Measurement& measurement) { ++count; last = measurement; }

    Status attachTo(MeasurementPublisher& publisher) {
        return publisher.subscribe(
            MeasurementPublisher::Subscriber::bind<Counter, &Counter::onMeasurement>(this));
    }

    uint32_t    count;
    Measurement last;
};

struct Fixture {
    VirtualClock         clock;
    FakeSensor           sensor;
    PassThroughFilter    filter;
    MeasurementPublisher publisher;
    Counter              counter;

    Fixture() { sensor.clock = &clock; counter.attachTo(publisher); }
};

void samplesAtTheConfiguredPeriodAndNotFaster() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 200u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    CHECK(isOk(service.begin()));

    /* One second of a 10 ms main loop = 100 poll() calls, 5 samples. */
    for (uint32_t i = 0; i < 100u; ++i) {
        service.poll();
        fixture.clock.advance(10u);
    }
    CHECK_EQ(fixture.counter.count, 5u);
    CHECK_EQ(fixture.sensor.readCount, 5u);
}

void firstSampleIsTakenImmediatelyAfterBegin() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 5000u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    /* The display must not stay blank for the first full period after boot. */
    CHECK(service.poll());
    CHECK_EQ(fixture.counter.count, 1u);
}

void samplePeriodIsClampedToWhatTheDriverAllows() {
    Fixture fixture;                            // FakeSensor declares 50 ms
    SensorServiceConfig config;
    config.samplePeriodMs = 1u;                 // an unrealistic request
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    for (uint32_t i = 0; i < 100u; ++i) {       // 1 s at a 10 ms loop
        service.poll();
        fixture.clock.advance(10u);
    }
    CHECK_EQ(fixture.counter.count, 20u);       // 50 ms, not 1 ms
}

void implausibleReadingsAreRejectedBeforeTheFilter() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 100u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    service.poll();                             // a good sample at 20 degC
    fixture.clock.advance(100u);

    fixture.sensor.value = 900.0f;              // impossible
    service.poll();

    CHECK_EQ(service.stats().samplesRejected, 1u);
    CHECK_EQ(service.stats().samplesAccepted, 1u);
    CHECK(fixture.counter.last.status == Status::OutOfRange);
    /* The last good value is still carried, so the GUI can show it greyed out
     * rather than flashing a meaningless zero. */
    CHECK_NEAR(fixture.counter.last.filtered, 20.0, 1e-3);
}

void notReadyIsNotAnError() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 100u;
    config.faultThreshold = 2u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    fixture.sensor.readStatus = Status::NotReady;   // slow conversion
    for (uint32_t i = 0; i < 20u; ++i) {
        service.poll();
        fixture.clock.advance(100u);
    }
    CHECK(service.healthy());
    CHECK_EQ(service.stats().readErrors, 0u);
    CHECK_EQ(fixture.counter.count, 0u);
}

void consecutiveErrorsRaiseAFaultAndRecoveryClearsIt() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs  = 100u;
    config.faultThreshold  = 3u;
    config.reinitBackoffMs = 1000u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    fixture.sensor.readStatus = Status::BusError;
    for (uint32_t i = 0; i < 5u; ++i) {
        service.poll();
        fixture.clock.advance(100u);
    }
    CHECK(!service.healthy());
    CHECK_EQ(service.stats().readErrors, 3u);       // it stopped hammering the bus

    /* Back-off: no re-init before the configured delay. */
    service.poll();
    CHECK_EQ(service.stats().reinitAttempts, 0u);

    fixture.clock.advance(1000u);
    fixture.sensor.readStatus = Status::Ok;         // the cable is back
    service.poll();
    CHECK_EQ(service.stats().reinitAttempts, 1u);
    CHECK(service.healthy());

    service.poll();
    CHECK(fixture.counter.last.status == Status::Ok);
}

void errorsCanBeKeptOffTheChannel() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 100u;
    config.publishOnError = false;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();

    fixture.sensor.readStatus = Status::BusError;
    service.poll();
    CHECK_EQ(fixture.counter.count, 0u);            // silent, as configured
    CHECK_EQ(service.stats().readErrors, 1u);
}

void filterCanBeSwappedWhileRunning() {
    Fixture fixture;
    SensorServiceConfig config;
    config.samplePeriodMs = 100u;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher, config);
    service.begin();
    service.poll();
    CHECK_NEAR(fixture.counter.last.filtered, 20.0, 1e-3);

    EwmaFilter smooth(0.5f);
    service.setFilter(smooth);

    fixture.sensor.value = 30.0f;
    fixture.clock.advance(100u);
    service.poll();
    /* The new filter was reset on the swap, so it seeds on the first sample
     * instead of dragging the old state along. */
    CHECK_NEAR(fixture.counter.last.filtered, 30.0, 1e-3);
    CHECK_NEAR(fixture.counter.last.raw, 30.0, 1e-3);
}

void aFailingInitLeavesTheServiceUnhealthy() {
    Fixture fixture;
    fixture.sensor.initStatus = Status::BusError;
    SensorService service(fixture.sensor, fixture.filter, fixture.clock,
                          fixture.publisher);
    CHECK(service.begin() == Status::BusError);
    CHECK(!service.healthy());
    CHECK(!service.poll());
    CHECK_EQ(fixture.sensor.readCount, 0u);   // never read a device that is not there
}

} // namespace

int main() {
    printf("test_service\n");
    RUN_TEST(samplesAtTheConfiguredPeriodAndNotFaster);
    RUN_TEST(firstSampleIsTakenImmediatelyAfterBegin);
    RUN_TEST(samplePeriodIsClampedToWhatTheDriverAllows);
    RUN_TEST(implausibleReadingsAreRejectedBeforeTheFilter);
    RUN_TEST(notReadyIsNotAnError);
    RUN_TEST(consecutiveErrorsRaiseAFaultAndRecoveryClearsIt);
    RUN_TEST(errorsCanBeKeptOffTheChannel);
    RUN_TEST(filterCanBeSwappedWhileRunning);
    RUN_TEST(aFailingInitLeavesTheServiceUnhealthy);
    TEST_SUMMARY("test_service");
}
