/**
 * @file host_clock.hpp
 * @brief hal::IClock implementations for the host (PC) port.
 */
#ifndef SENSORFW_PORT_HOST_CLOCK_HPP
#define SENSORFW_PORT_HOST_CLOCK_HPP

#include "sensorfw/hal/clock.hpp"

namespace sensorfw {
namespace port {
namespace host {

/** @brief Wall clock, backed by std::chrono::steady_clock. */
class HostClock : public hal::IClock {
public:
    HostClock();
    TimestampMs nowMs() const;

private:
    uint64_t originUs_;
};

/**
 * @brief Manually advanced clock.
 *
 * The reason the whole framework takes time as an injected dependency: a unit
 * test can push an hour of samples through a filter chain in microseconds, and
 * the demo can replay a 3 minute scenario in one second - deterministically,
 * with no sleep() anywhere.
 */
class VirtualClock : public hal::IClock {
public:
    explicit VirtualClock(TimestampMs start = 0u) : now_(start) {}

    TimestampMs nowMs() const { return now_; }
    void advance(uint32_t deltaMs) { now_ += deltaMs; }
    void setNow(TimestampMs now) { now_ = now; }

private:
    TimestampMs now_;
};

} // namespace host
} // namespace port
} // namespace sensorfw

#endif // SENSORFW_PORT_HOST_CLOCK_HPP
