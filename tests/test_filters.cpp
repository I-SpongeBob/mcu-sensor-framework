/**
 * @file test_filters.cpp
 * @brief Behavioural tests for every filter, plus the chain.
 */
#include "test_support.hpp"

#include "sensorfw/filter/ewma.hpp"
#include "sensorfw/filter/filter_chain.hpp"
#include "sensorfw/filter/kalman1d.hpp"
#include "sensorfw/filter/median.hpp"
#include "sensorfw/filter/moving_average.hpp"
#include "sensorfw/filter/outlier_gate.hpp"
#include "sensorfw/filter/pass_through.hpp"
#include "sensorfw/filter/slew_rate_limiter.hpp"

using namespace sensorfw;

namespace {

void movingAverageHasUnityDcGain() {
    MovingAverageFilter<4> filter;
    Real out = 0;
    for (uint32_t i = 0; i < 20u; ++i) { out = filter.update(10.0f, i * 100u); }
    CHECK_NEAR(out, 10.0, 1e-4);
    CHECK(filter.settled());
}

void movingAverageAveragesTheWindow() {
    MovingAverageFilter<4> filter;
    filter.update(1.0f, 0u);
    filter.update(2.0f, 100u);
    filter.update(3.0f, 200u);
    const Real out = filter.update(4.0f, 300u);
    CHECK_NEAR(out, 2.5, 1e-4);          // (1+2+3+4)/4

    /* The oldest sample must fall out of the window. */
    CHECK_NEAR(filter.update(5.0f, 400u), 3.5, 1e-4);   // (2+3+4+5)/4
}

void movingAverageIsUsableBeforeTheWindowFills() {
    MovingAverageFilter<8> filter;
    CHECK_NEAR(filter.update(20.0f, 0u), 20.0, 1e-4);   // not 20/8
    CHECK(!filter.settled());
}

void medianRemovesAnIsolatedSpike() {
    MedianFilter<5> filter;
    const Real input[] = { 20.0f, 20.1f, 99.0f, 20.2f, 20.1f };
    Real out = 0;
    for (uint8_t i = 0; i < 5u; ++i) { out = filter.update(input[i], i * 100u); }
    CHECK_NEAR(out, 20.1, 0.05);        // the 99 degC glitch is simply gone
}

void medianWithEvenWindowAveragesTheTwoMiddleSamples() {
    MedianFilter<4> filter;
    filter.update(1.0f, 0u);
    filter.update(2.0f, 100u);
    filter.update(3.0f, 200u);
    CHECK_NEAR(filter.update(4.0f, 300u), 2.5, 1e-4);
}

void ewmaSeedsOnTheFirstSampleInsteadOfRampingFromZero() {
    EwmaFilter filter(0.2f);
    CHECK_NEAR(filter.update(25.0f, 0u), 25.0, 1e-4);
}

void ewmaConvergesTowardsAStep() {
    EwmaFilter filter(0.5f);
    filter.update(0.0f, 0u);
    Real out = 0;
    for (uint32_t i = 1; i <= 20u; ++i) { out = filter.update(10.0f, i * 100u); }
    CHECK_NEAR(out, 10.0, 0.01);
}

void ewmaTimeConstantIsIndependentOfTheSamplingRate() {
    /* Same 2 s time constant, two very different sampling periods: after one
     * time constant both must sit at 63% of the step. That is the property a
     * fixed alpha does not have, and the reason the interface passes time. */
    EwmaFilter fast = EwmaFilter::withTimeConstant(2000u);
    EwmaFilter slow = EwmaFilter::withTimeConstant(2000u);

    fast.update(0.0f, 0u);
    slow.update(0.0f, 0u);

    Real fastOut = 0;
    for (uint32_t t = 50u; t <= 2000u; t += 50u) { fastOut = fast.update(10.0f, t); }

    Real slowOut = 0;
    for (uint32_t t = 400u; t <= 2000u; t += 400u) { slowOut = slow.update(10.0f, t); }

    CHECK_NEAR(fastOut, 6.3, 0.8);
    CHECK_NEAR(slowOut, 6.3, 0.8);
    CHECK_NEAR(fastOut, slowOut, 0.6);
}

void kalmanConvergesAndBecomesMoreConfident() {
    Kalman1dFilter filter(0.001f, 0.25f);
    filter.update(20.0f, 0u);
    const Real varianceAtStart = filter.variance();

    Real out = 0;
    for (uint32_t i = 1; i <= 100u; ++i) {
        const Real noise = ((i % 2u) == 0u) ? 0.3f : -0.3f;
        out = filter.update(20.0f + noise, i * 100u);
    }
    CHECK_NEAR(out, 20.0, 0.15);
    CHECK(filter.variance() < varianceAtStart);   // the gain shrank
}

void slewLimiterCapsTheRateOfChange() {
    SlewRateLimiter filter(2.0f);            // 2 degC per second
    filter.update(20.0f, 0u);
    /* 500 ms later the output may have moved at most 1 degC. */
    CHECK_NEAR(filter.update(80.0f, 500u), 21.0, 1e-3);
    CHECK_NEAR(filter.update(80.0f, 1000u), 22.0, 1e-3);
}

void slewLimiterPassesSmallChangesUntouched() {
    SlewRateLimiter filter(2.0f);
    filter.update(20.0f, 0u);
    CHECK_NEAR(filter.update(20.2f, 500u), 20.2, 1e-3);
}

void outlierGateHoldsThenResynchronises() {
    OutlierGate gate(5.0f, 3u);
    gate.update(20.0f, 0u);

    /* A genuine step of +30 degC: rejected three times, then accepted, so the
     * filter can never latch on a stale value. */
    CHECK_NEAR(gate.update(50.0f, 100u), 20.0, 1e-3);
    CHECK_NEAR(gate.update(50.0f, 200u), 20.0, 1e-3);
    CHECK_NEAR(gate.update(50.0f, 300u), 20.0, 1e-3);
    CHECK_NEAR(gate.update(50.0f, 400u), 50.0, 1e-3);
    CHECK_EQ(gate.rejectedCount(), 3u);
}

void chainAppliesStagesInOrder() {
    MedianFilter<3>  median;
    SlewRateLimiter  slew(1000.0f);
    FilterChain      chain;

    CHECK(isOk(chain.append(&median)));
    CHECK(isOk(chain.append(&slew)));
    CHECK_EQ(chain.stageCount(), 2u);

    chain.update(20.0f, 0u);
    chain.update(20.0f, 100u);
    const Real out = chain.update(99.0f, 200u);   // spike killed by the median
    CHECK_NEAR(out, 20.0, 1e-3);
}

void chainRefusesMoreStagesThanItCanHold() {
    PassThroughFilter stages[SENSORFW_MAX_FILTERS_PER_CHAIN + 1];
    FilterChain chain;
    for (int i = 0; i < SENSORFW_MAX_FILTERS_PER_CHAIN; ++i) {
        CHECK(isOk(chain.append(&stages[i])));
    }
    CHECK(chain.append(&stages[SENSORFW_MAX_FILTERS_PER_CHAIN]) == Status::NoSpace);
    CHECK(chain.append(0) == Status::InvalidArgument);
}

void resetClearsHistoryEverywhere() {
    MovingAverageFilter<4> average;
    average.update(100.0f, 0u);
    average.reset();
    CHECK_NEAR(average.update(10.0f, 100u), 10.0, 1e-4);

    EwmaFilter ewma(0.1f);
    ewma.update(100.0f, 0u);
    ewma.reset();
    CHECK_NEAR(ewma.update(10.0f, 100u), 10.0, 1e-4);
}

} // namespace

int main() {
    printf("test_filters\n");
    RUN_TEST(movingAverageHasUnityDcGain);
    RUN_TEST(movingAverageAveragesTheWindow);
    RUN_TEST(movingAverageIsUsableBeforeTheWindowFills);
    RUN_TEST(medianRemovesAnIsolatedSpike);
    RUN_TEST(medianWithEvenWindowAveragesTheTwoMiddleSamples);
    RUN_TEST(ewmaSeedsOnTheFirstSampleInsteadOfRampingFromZero);
    RUN_TEST(ewmaConvergesTowardsAStep);
    RUN_TEST(ewmaTimeConstantIsIndependentOfTheSamplingRate);
    RUN_TEST(kalmanConvergesAndBecomesMoreConfident);
    RUN_TEST(slewLimiterCapsTheRateOfChange);
    RUN_TEST(slewLimiterPassesSmallChangesUntouched);
    RUN_TEST(outlierGateHoldsThenResynchronises);
    RUN_TEST(chainAppliesStagesInOrder);
    RUN_TEST(chainRefusesMoreStagesThanItCanHold);
    RUN_TEST(resetClearsHistoryEverywhere);
    TEST_SUMMARY("test_filters");
}
