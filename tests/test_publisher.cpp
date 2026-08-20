/**
 * @file test_publisher.cpp
 * @brief The decoupling seam: callbacks, capacity limits, delivery order.
 */
#include "test_support.hpp"

#include "sensorfw/core/publisher.hpp"
#include "sensorfw/core/ring_buffer.hpp"
#include "sensorfw/core/time.hpp"

using namespace sensorfw;

namespace {

/** A subscriber that only records what it was given. */
class Recorder {
public:
    Recorder() : count(0), last(), tag(0) {}

    void onMeasurement(const Measurement& measurement) {
        ++count;
        last = measurement;
        if (order != 0 && *orderIndex < 8) { order[(*orderIndex)++] = tag; }
    }

    Status attachTo(MeasurementPublisher& publisher) {
        return publisher.subscribe(
            MeasurementPublisher::Subscriber::bind<Recorder,
                                                   &Recorder::onMeasurement>(this));
    }

    uint32_t    count;
    Measurement last;
    int         tag;

    static int*     order;
    static uint8_t* orderIndex;
};

int*     Recorder::order      = 0;
uint8_t* Recorder::orderIndex = 0;

Measurement makeMeasurement(Real value, TimestampMs timestamp) {
    Measurement measurement;
    measurement.quantity  = Quantity::Temperature;
    measurement.raw       = value;
    measurement.filtered  = value;
    measurement.timestamp = timestamp;
    measurement.status    = Status::Ok;
    measurement.source    = "unit-test";
    return measurement;
}

void everySubscriberReceivesEveryMessage() {
    MeasurementPublisher publisher;
    Recorder a;
    Recorder b;
    Recorder c;
    CHECK(isOk(a.attachTo(publisher)));
    CHECK(isOk(b.attachTo(publisher)));
    CHECK(isOk(c.attachTo(publisher)));
    CHECK_EQ(publisher.subscriberCount(), 3u);

    publisher.publish(makeMeasurement(21.5f, 1000u));

    CHECK_EQ(a.count, 1u);
    CHECK_EQ(b.count, 1u);
    CHECK_EQ(c.count, 1u);
    CHECK_NEAR(c.last.filtered, 21.5, 1e-4);
    CHECK_STREQ(c.last.source, "unit-test");
}

void deliveryFollowsRegistrationOrder() {
    int     order[8] = { 0 };
    uint8_t index    = 0u;
    Recorder::order      = order;
    Recorder::orderIndex = &index;

    MeasurementPublisher publisher;
    Recorder first;  first.tag  = 1;
    Recorder second; second.tag = 2;
    Recorder third;  third.tag  = 3;
    first.attachTo(publisher);
    second.attachTo(publisher);
    third.attachTo(publisher);

    publisher.publish(makeMeasurement(20.0f, 0u));

    CHECK_EQ(index, 3u);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 3);

    Recorder::order      = 0;
    Recorder::orderIndex = 0;
}

void publishingWithNoSubscribersIsHarmless() {
    MeasurementPublisher publisher;
    publisher.publish(makeMeasurement(20.0f, 0u));   // must not crash
    CHECK_EQ(publisher.subscriberCount(), 0u);
}

void capacityIsEnforcedAndReported() {
    Publisher<Measurement, 2> publisher;
    Recorder a;
    Recorder b;
    Recorder c;

    CHECK(isOk(publisher.subscribe(
        Publisher<Measurement, 2>::Subscriber::bind<Recorder, &Recorder::onMeasurement>(&a))));
    CHECK(isOk(publisher.subscribe(
        Publisher<Measurement, 2>::Subscriber::bind<Recorder, &Recorder::onMeasurement>(&b))));
    /* The third one is refused with a status, not silently dropped: wiring
     * mistakes have to be visible at start-up. */
    CHECK(publisher.subscribe(
        Publisher<Measurement, 2>::Subscriber::bind<Recorder, &Recorder::onMeasurement>(&c))
          == Status::NoSpace);

    publisher.publish(makeMeasurement(20.0f, 0u));
    CHECK_EQ(c.count, 0u);
}

void emptyCallbackIsRejected() {
    MeasurementPublisher publisher;
    MeasurementPublisher::Subscriber empty;
    CHECK(publisher.subscribe(empty) == Status::InvalidArgument);
}

void ringBufferKeepsTheNewestSamples() {
    RingBuffer<int, 3> buffer;
    CHECK(buffer.empty());

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    CHECK(buffer.full());
    CHECK_EQ(buffer.at(0), 1);
    CHECK_EQ(buffer.newest(), 3);

    buffer.push(4);                 // 1 falls out
    CHECK_EQ(buffer.size(), 3u);
    CHECK_EQ(buffer.at(0), 2);
    CHECK_EQ(buffer.at(2), 4);
    CHECK_EQ(buffer.newest(), 4);

    buffer.clear();
    CHECK(buffer.empty());
}

void elapsedTimeSurvivesTheTickWraparound() {
    /* 49.7 days after boot the millisecond tick wraps. Unsigned arithmetic has
     * to keep giving the right delta or every timer in the product misfires. */
    const TimestampMs beforeWrap = 0xFFFFFF00u;
    const TimestampMs afterWrap  = 0x00000100u;   // 512 ms later

    CHECK_EQ(elapsedMs(afterWrap, beforeWrap), 512u);
    CHECK(isDue(afterWrap, beforeWrap, 500u));
    CHECK(!isDue(afterWrap, beforeWrap, 600u));
}

} // namespace

int main() {
    printf("test_publisher\n");
    RUN_TEST(everySubscriberReceivesEveryMessage);
    RUN_TEST(deliveryFollowsRegistrationOrder);
    RUN_TEST(publishingWithNoSubscribersIsHarmless);
    RUN_TEST(capacityIsEnforcedAndReported);
    RUN_TEST(emptyCallbackIsRejected);
    RUN_TEST(ringBufferKeepsTheNewestSamples);
    RUN_TEST(elapsedTimeSurvivesTheTickWraparound);
    TEST_SUMMARY("test_publisher");
}
