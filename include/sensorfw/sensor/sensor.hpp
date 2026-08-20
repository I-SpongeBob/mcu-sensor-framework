/**
 * @file sensor.hpp
 * @brief Driver-facing interfaces. Everything above this line is portable C++;
 *        everything below it is chip specific.
 */
#ifndef SENSORFW_SENSOR_SENSOR_HPP
#define SENSORFW_SENSOR_SENSOR_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {

/**
 * @brief A device that produces one scalar physical value.
 *
 * Contract:
 *  - init() is called once at start-up and may be retried after a failure.
 *  - read() must not block for more than a few hundred microseconds. A device
 *    with a long conversion time (e.g. DS18B20, 750 ms) returns
 *    Status::NotReady until the conversion completes; the service simply polls
 *    again on the next tick, so the main loop is never stalled.
 */
class ISensor {
public:
    virtual Status init() = 0;
    virtual Status read(Sample& out) = 0;
    virtual const SensorInfo& info() const = 0;

protected:
    ~ISensor() {}
};

/**
 * @brief Marker interface for sensors reporting degrees Celsius.
 *
 * It adds no members - its value is type safety: a HumiditySensor cannot be
 * wired into a thermostat by accident. Adding a new quantity means adding a
 * marker like this one, no change to the framework.
 */
class ITemperatureSensor : public ISensor {
protected:
    ~ITemperatureSensor() {}
};

} // namespace sensorfw

#endif // SENSORFW_SENSOR_SENSOR_HPP
