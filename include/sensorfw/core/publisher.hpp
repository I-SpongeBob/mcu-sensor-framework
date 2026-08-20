/**
 * @file publisher.hpp
 * @brief Fixed-capacity publish/subscribe channel - the seam that decouples
 *        the sensor pipeline from the application layer.
 *
 * The sensor service knows nothing about GUI, MQTT or business logic: it only
 * publishes a Measurement. Applications subscribe. Adding a fourth consumer
 * (e.g. a data logger) touches zero lines of existing code.
 */
#ifndef SENSORFW_CORE_PUBLISHER_HPP
#define SENSORFW_CORE_PUBLISHER_HPP

#include "sensorfw/config.hpp"
#include "sensorfw/core/callback.hpp"
#include "sensorfw/core/types.hpp"

namespace sensorfw {

/**
 * @tparam T        payload type, published by const reference
 * @tparam Capacity maximum number of subscribers (static, no heap)
 */
template <typename T, uint8_t Capacity = SENSORFW_MAX_SUBSCRIBERS>
class Publisher {
public:
    typedef Callback<const T&> Subscriber;

    Publisher() : count_(0) {}

    /**
     * @brief Register a subscriber.
     * @return Status::NoSpace when Capacity is exhausted - checked at start-up,
     *         never silently ignored.
     */
    Status subscribe(const Subscriber& subscriber) {
        if (!subscriber.valid()) { return Status::InvalidArgument; }
        if (count_ >= Capacity)  { return Status::NoSpace; }
        subscribers_[count_++] = subscriber;
        return Status::Ok;
    }

    /**
     * @brief Deliver @p value to every subscriber, in registration order.
     *
     * Called from the sampling context (main loop or sensor task). Subscribers
     * must therefore be non-blocking; the MQTT reporter for instance only
     * queues a payload and lets the network task drain it.
     */
    void publish(const T& value) const {
        for (uint8_t i = 0; i < count_; ++i) {
            subscribers_[i](value);
        }
    }

    uint8_t subscriberCount() const { return count_; }
    static uint8_t capacity() { return Capacity; }

private:
    Subscriber subscribers_[Capacity];
    uint8_t    count_;
};

/** Channel used by every temperature source in the system. */
typedef Publisher<Measurement> MeasurementPublisher;

} // namespace sensorfw

#endif // SENSORFW_CORE_PUBLISHER_HPP
