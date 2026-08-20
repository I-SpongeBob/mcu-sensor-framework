/**
 * @file config.hpp
 * @brief Compile-time configuration of the sensor framework.
 *
 * Every tunable that affects RAM/ROM footprint lives here so that a port to a
 * smaller MCU is a matter of editing one file (or overriding the macros from
 * the build system with -DSENSORFW_xxx).
 */
#ifndef SENSORFW_CONFIG_HPP
#define SENSORFW_CONFIG_HPP

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Numeric type used for physical values.
 *
 * Cortex-M4F/M7 and ESP32 have a single precision FPU -> float is free.
 * On Cortex-M0/M3 (no FPU) float is emulated; the typedef is kept in one place
 * so the whole framework can be moved to a fixed-point type (e.g. Q16.16)
 * without touching the algorithms.
 * ------------------------------------------------------------------------ */
#ifndef SENSORFW_REAL_TYPE
#define SENSORFW_REAL_TYPE float
#endif

/** Maximum number of filters that can be stacked in one FilterChain. */
#ifndef SENSORFW_MAX_FILTERS_PER_CHAIN
#define SENSORFW_MAX_FILTERS_PER_CHAIN 6
#endif

/** Maximum number of subscribers on one publisher (GUI + MQTT + logic + ...). */
#ifndef SENSORFW_MAX_SUBSCRIBERS
#define SENSORFW_MAX_SUBSCRIBERS 8
#endif

namespace sensorfw {

/** Physical value type used across the framework. */
typedef SENSORFW_REAL_TYPE Real;

/** Monotonic millisecond tick. Wrap-around at 49.7 days is handled by using
 *  unsigned subtraction everywhere (see core/time.hpp). */
typedef uint32_t TimestampMs;

} // namespace sensorfw

#endif // SENSORFW_CONFIG_HPP
