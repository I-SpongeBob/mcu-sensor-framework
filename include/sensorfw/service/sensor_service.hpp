/**
 * @file sensor_service.hpp
 * @brief Ties a sensor, a filter and a publisher together - and nothing else.
 *
 * The service is the only component that knows the acquisition rhythm. It is
 * cooperative (no thread, no blocking wait): poll() is called as often as
 * convenient from a bare-metal super-loop, a FreeRTOS task or a timer callback,
 * and it decides on its own whether the period has elapsed.
 *
 * Responsibilities kept here on purpose:
 *   - periodic scheduling (wrap-safe)
 *   - plausibility check against SensorInfo limits
 *   - fault handling: consecutive-error counting and re-init with back-off
 *   - publishing exactly one Measurement per accepted sample
 *
 * Responsibilities deliberately NOT here: how the value is filtered (strategy),
 * where it goes (publisher), how it is displayed or used (application).
 */
#ifndef SENSORFW_SERVICE_SENSOR_SERVICE_HPP
#define SENSORFW_SERVICE_SENSOR_SERVICE_HPP

#include "sensorfw/core/publisher.hpp"
#include "sensorfw/filter/filter.hpp"
#include "sensorfw/hal/clock.hpp"
#include "sensorfw/sensor/sensor.hpp"

namespace sensorfw {

/** Tunables of one acquisition pipeline. */
struct SensorServiceConfig {
    uint32_t samplePeriodMs;   ///< nominal acquisition period
    uint8_t  faultThreshold;   ///< consecutive errors before the sensor is declared faulty
    uint32_t reinitBackoffMs;  ///< wait between two re-init attempts of a faulty sensor
    bool     publishOnError;   ///< also publish a Measurement carrying the error status

    SensorServiceConfig()
        : samplePeriodMs(1000u),
          faultThreshold(3u),
          reinitBackoffMs(5000u),
          publishOnError(true) {}
};

/** Runtime counters, useful on the GUI service page and over MQTT. */
struct SensorServiceStats {
    uint32_t samplesAccepted;
    uint32_t samplesRejected;   ///< outside the SensorInfo range
    uint32_t readErrors;
    uint32_t reinitAttempts;

    SensorServiceStats()
        : samplesAccepted(0u), samplesRejected(0u), readErrors(0u), reinitAttempts(0u) {}
};

class SensorService {
public:
    /**
     * @brief All collaborators are injected - the service allocates nothing and
     *        can be exercised on the host with fakes (see tests/).
     */
    SensorService(ISensor& sensor,
                  IFilter& filter,
                  hal::IClock& clock,
                  MeasurementPublisher& publisher,
                  const SensorServiceConfig& config = SensorServiceConfig());

    /** @brief Initialise the underlying device. Safe to retry. */
    Status begin();

    /**
     * @brief Non-blocking tick. Call it freely; it acts only when due.
     * @return true when a Measurement was published during this call.
     */
    bool poll();

    /** @brief Swap the filter at runtime (eco vs responsive profile). The new
     *         filter is reset so no state leaks across the switch. */
    void setFilter(IFilter& filter);

    /** @brief Change the acquisition period at runtime, clamped to the fastest
     *         rate the driver declares as sensible. */
    void setSamplePeriod(uint32_t periodMs);

    bool healthy() const { return !faulty_; }
    const SensorServiceStats& stats() const { return stats_; }
    const Measurement& lastMeasurement() const { return last_; }
    const SensorInfo& sensorInfo() const { return sensor_.info(); }

private:
    bool tryRecover(TimestampMs now);
    void publishError(Status status, TimestampMs now);

    ISensor&              sensor_;
    IFilter*              filter_;
    hal::IClock&          clock_;
    MeasurementPublisher& publisher_;
    SensorServiceConfig   config_;
    SensorServiceStats    stats_;

    Measurement last_;
    TimestampMs lastSampleTick_;
    TimestampMs lastRecoveryTick_;
    uint8_t     consecutiveErrors_;
    bool        initialised_;
    bool        faulty_;
};

} // namespace sensorfw

#endif // SENSORFW_SERVICE_SENSOR_SERVICE_HPP
