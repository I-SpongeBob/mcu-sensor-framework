#include "host_clock.hpp"

#include <chrono>

namespace sensorfw {
namespace port {
namespace host {

namespace {
uint64_t steadyMicros() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
}

HostClock::HostClock() : originUs_(steadyMicros()) {}

TimestampMs HostClock::nowMs() const {
    return static_cast<TimestampMs>((steadyMicros() - originUs_) / 1000u);
}

} // namespace host
} // namespace port
} // namespace sensorfw
