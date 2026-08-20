/**
 * @file types.hpp
 * @brief Status codes and the sample/measurement value objects.
 */
#ifndef SENSORFW_CORE_TYPES_HPP
#define SENSORFW_CORE_TYPES_HPP

#include "sensorfw/config.hpp"

namespace sensorfw {

/**
 * @brief Return code used by the whole framework.
 *
 * The framework is compiled with -fno-exceptions: errors are values, never
 * thrown. Status is a uint8_t so it stays cheap to return in a register.
 */
enum class Status : uint8_t {
    Ok = 0,
    NotInitialised,   ///< init() was not called or failed
    NotReady,         ///< conversion still running, try again later
    BusError,         ///< I2C/SPI/ADC transport failure
    Timeout,          ///< device did not answer in time
    OutOfRange,       ///< value outside the sensor's physical range
    InvalidArgument,
    NoSpace           ///< a fixed-capacity container is full
};

inline bool isOk(Status s) { return s == Status::Ok; }

const char* toString(Status s);

/** Physical quantity a sensor produces. Adding a new one does not change any
 *  existing code - the framework is quantity-agnostic. */
enum class Quantity : uint8_t {
    Temperature,   ///< degree Celsius
    Humidity,      ///< %RH
    Pressure,      ///< Pa
    Pm25,          ///< ug/m3
    Voc            ///< index
};

/** Static description of a sensor instance, kept in flash. */
struct SensorInfo {
    const char* name;       ///< e.g. "ntc-10k@adc1"
    Quantity    quantity;
    Real        minValue;   ///< physical lower limit, used for plausibility checks
    Real        maxValue;
    uint16_t    minPeriodMs;///< fastest sensible sampling period for this device
};

/** One raw acquisition straight from the driver. */
struct Sample {
    Real        value;
    TimestampMs timestamp;
};

/**
 * @brief What the service publishes to the application layer.
 *
 * Carrying both raw and filtered value costs 4 bytes and makes the filter
 * behaviour observable at runtime (very useful when tuning on real hardware).
 */
struct Measurement {
    Quantity    quantity;
    Real        raw;
    Real        filtered;
    TimestampMs timestamp;
    Status      status;     ///< Ok means raw/filtered are usable
    const char* source;     ///< SensorInfo::name of the producing sensor
};

} // namespace sensorfw

#endif // SENSORFW_CORE_TYPES_HPP
