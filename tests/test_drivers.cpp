/**
 * @file test_drivers.cpp
 * @brief Driver maths and error paths, verified without hardware.
 */
#include "test_support.hpp"

#include "drivers/lm75/lm75.hpp"
#include "drivers/ntc/ntc_thermistor.hpp"
#include "port/host/host_clock.hpp"
#include "port/host/simulated_adc.hpp"
#include "port/host/simulated_i2c.hpp"

using namespace sensorfw;
using namespace sensorfw::port::host;

namespace {

/** ADC stub that always returns the same programmed count. */
class FixedAdc : public hal::IAdcChannel {
public:
    explicit FixedAdc(uint16_t counts) : counts_(counts), status_(Status::Ok) {}

    Status readRaw(uint16_t& rawCounts) {
        rawCounts = counts_;
        return status_;
    }
    uint16_t fullScale() const { return 4095u; }
    uint16_t referenceMv() const { return 3300u; }

    void setCounts(uint16_t counts) { counts_ = counts; }
    void setStatus(Status status) { status_ = status; }

private:
    uint16_t counts_;
    Status   status_;
};

void ntcReadsExactlyR0AtTheNominalTemperature() {
    /* With R0 = Rseries the divider sits at mid-scale, which must decode to
     * exactly the datasheet nominal temperature of 25 degC. */
    drivers::NtcConfig config;
    Real temperature = 0;
    CHECK(isOk(drivers::NtcThermistorSensor::convert(2048u, 4095u, config, temperature)));
    CHECK_NEAR(temperature, 25.0, 0.05);
}

void ntcIsMonotonicAndSignedCorrectly() {
    drivers::NtcConfig config;
    Real cold = 0;
    Real warm = 0;
    /* NTC to ground: a hotter thermistor has a lower resistance, so fewer
     * counts. Getting this backwards is the classic NTC wiring bug. */
    drivers::NtcThermistorSensor::convert(1200u, 4095u, config, warm);
    drivers::NtcThermistorSensor::convert(2800u, 4095u, config, cold);
    CHECK(warm > cold);
    CHECK(warm > 40.0f);
    CHECK(cold < 10.0f);
}

void ntcHonoursTheDividerTopology() {
    drivers::NtcConfig toGround;
    drivers::NtcConfig toRail;
    toRail.ntcToGround = false;

    Real a = 0;
    Real b = 0;
    drivers::NtcThermistorSensor::convert(1500u, 4095u, toGround, a);
    drivers::NtcThermistorSensor::convert(1500u, 4095u, toRail, b);
    CHECK(fabs(static_cast<double>(a - b)) > 1.0);   // same counts, different board
}

void ntcRejectsRailedReadings() {
    drivers::NtcConfig config;
    Real temperature = 0;
    /* Open circuit and short circuit both pin the ADC at a rail. Returning a
     * status instead of +/-inf is what lets the service raise a sensor fault. */
    CHECK(drivers::NtcThermistorSensor::convert(0u, 4095u, config, temperature)
          == Status::OutOfRange);
    CHECK(drivers::NtcThermistorSensor::convert(4095u, 4095u, config, temperature)
          == Status::OutOfRange);
}

void ntcInitValidatesItsConfiguration() {
    VirtualClock clock;
    FixedAdc     adc(2048u);

    drivers::NtcConfig broken;
    broken.seriesResistance = 0.0f;                 // a wrong board file
    drivers::NtcThermistorSensor bad(adc, clock, broken);
    CHECK(bad.init() == Status::InvalidArgument);

    drivers::NtcThermistorSensor good(adc, clock);
    CHECK(isOk(good.init()));
}

void ntcPropagatesAdcErrors() {
    VirtualClock clock;
    FixedAdc     adc(2048u);
    drivers::NtcThermistorSensor sensor(adc, clock);

    Sample sample;
    CHECK(sensor.read(sample) == Status::NotInitialised);   // before init()

    CHECK(isOk(sensor.init()));
    adc.setStatus(Status::Timeout);
    CHECK(sensor.read(sample) == Status::Timeout);
}

void ntcStampsSamplesWithTheInjectedClock() {
    VirtualClock clock;
    FixedAdc     adc(2048u);
    drivers::NtcThermistorSensor sensor(adc, clock);
    sensor.init();

    clock.setNow(123456u);
    Sample sample;
    CHECK(isOk(sensor.read(sample)));
    CHECK_EQ(sample.timestamp, 123456u);
}

void lm75DecodesDatasheetValues() {
    /* Datasheet table, 11-bit mode, 0.125 degC/LSB, left aligned. */
    CHECK_NEAR(drivers::Lm75Sensor::convert(0x19u, 0x00u),  25.0,   1e-3);
    CHECK_NEAR(drivers::Lm75Sensor::convert(0x00u, 0x00u),   0.0,   1e-3);
    CHECK_NEAR(drivers::Lm75Sensor::convert(0x7Fu, 0xE0u), 127.875, 1e-3);
    CHECK_NEAR(drivers::Lm75Sensor::convert(0x80u, 0x00u), -128.0,  1e-3);
    CHECK_NEAR(drivers::Lm75Sensor::convert(0xFFu, 0xE0u),  -0.125, 1e-3);
    CHECK_NEAR(drivers::Lm75Sensor::convert(0xE7u, 0x00u), -25.0,   1e-3);
}

void lm75IgnoresTheUndefinedLowBits() {
    /* The lowest five bits are undefined in 11-bit mode; whatever the part
     * leaves there must not shift the decoded value. */
    CHECK_NEAR(drivers::Lm75Sensor::convert(0x19u, 0x1Fu), 25.0, 1e-3);
}

void lm75ProbesTheDeviceAtInit() {
    VirtualClock     clock;
    SimulatedLm75Bus bus(clock);
    drivers::Lm75Sensor sensor(bus, clock);

    CHECK(isOk(sensor.init()));
    CHECK(bus.configured());          // the configuration register was written

    Sample sample;
    CHECK(isOk(sensor.read(sample)));
    CHECK_NEAR(sample.value, 22.0, 0.2);
}

void lm75ReportsAnAbsentDevice() {
    VirtualClock     clock;
    SimulatedLm75Bus bus(clock);
    bus.setPresent(false);

    drivers::Lm75Sensor sensor(bus, clock);
    CHECK(sensor.init() == Status::BusError);

    Sample sample;
    CHECK(sensor.read(sample) == Status::NotInitialised);
}

void lm75RefusesTheWrongAddress() {
    VirtualClock     clock;
    SimulatedLm75Bus bus(clock, 0x48u);
    drivers::Lm75Sensor sensor(bus, clock, 0x49u);   // strap mismatch
    CHECK(sensor.init() == Status::BusError);
}

void simulatedAdcRoundTripsThroughTheDriverMaths() {
    /* The simulator inverts the driver equation, so with the noise disabled the
     * decoded temperature must come back as the scenario value. This validates
     * both directions at once. */
    VirtualClock clock;
    SimulatedAdcConfig quiet;
    quiet.noiseAmplitudeC = 0.0f;
    quiet.spikeEveryNth   = 0u;

    SimulatedNtcAdc adc(clock, drivers::NtcConfig(), quiet);
    drivers::NtcThermistorSensor sensor(adc, clock);
    sensor.init();

    clock.setNow(5000u);
    Sample sample;
    CHECK(isOk(sensor.read(sample)));
    CHECK_NEAR(sample.value, adc.lastTrueValue(), 0.05);
}

} // namespace

int main() {
    printf("test_drivers\n");
    RUN_TEST(ntcReadsExactlyR0AtTheNominalTemperature);
    RUN_TEST(ntcIsMonotonicAndSignedCorrectly);
    RUN_TEST(ntcHonoursTheDividerTopology);
    RUN_TEST(ntcRejectsRailedReadings);
    RUN_TEST(ntcInitValidatesItsConfiguration);
    RUN_TEST(ntcPropagatesAdcErrors);
    RUN_TEST(ntcStampsSamplesWithTheInjectedClock);
    RUN_TEST(lm75DecodesDatasheetValues);
    RUN_TEST(lm75IgnoresTheUndefinedLowBits);
    RUN_TEST(lm75ProbesTheDeviceAtInit);
    RUN_TEST(lm75ReportsAnAbsentDevice);
    RUN_TEST(lm75RefusesTheWrongAddress);
    RUN_TEST(simulatedAdcRoundTripsThroughTheDriverMaths);
    TEST_SUMMARY("test_drivers");
}
