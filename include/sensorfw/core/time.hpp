/**
 * @file time.hpp
 * @brief Wrap-safe helpers for the 32-bit millisecond tick.
 */
#ifndef SENSORFW_CORE_TIME_HPP
#define SENSORFW_CORE_TIME_HPP

#include "sensorfw/config.hpp"

namespace sensorfw {

/**
 * @brief Elapsed time between two ticks, correct across the 49.7 day wrap.
 *
 * Unsigned arithmetic wraps by definition, so (now - past) is the real delta
 * as long as it is smaller than 2^31 ms. This is the standard embedded idiom
 * and the reason no int is used for timestamps anywhere in the framework.
 */
inline uint32_t elapsedMs(TimestampMs now, TimestampMs past) {
    return static_cast<uint32_t>(now - past);
}

/** @brief True when @p periodMs has passed since @p last. */
inline bool isDue(TimestampMs now, TimestampMs last, uint32_t periodMs) {
    return elapsedMs(now, last) >= periodMs;
}

} // namespace sensorfw

#endif // SENSORFW_CORE_TIME_HPP
