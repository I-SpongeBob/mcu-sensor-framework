#include "sensorfw/service/sensor_service.hpp"

#include "sensorfw/core/time.hpp"

namespace sensorfw {

SensorService::SensorService(ISensor& sensor,
                             IFilter& filter,
                             hal::IClock& clock,
                             MeasurementPublisher& publisher,
                             const SensorServiceConfig& config)
    : sensor_(sensor),
      filter_(&filter),
      clock_(clock),
      publisher_(publisher),
      config_(config),
      stats_(),
      last_(),
      lastSampleTick_(0u),
      lastRecoveryTick_(0u),
      consecutiveErrors_(0u),
      initialised_(false),
      faulty_(false) {
    last_.quantity  = sensor.info().quantity;
    last_.raw       = static_cast<Real>(0);
    last_.filtered  = static_cast<Real>(0);
    last_.timestamp = 0u;
    last_.status    = Status::NotInitialised;
    last_.source    = sensor.info().name;

    setSamplePeriod(config_.samplePeriodMs);
}

Status SensorService::begin() {
    const Status status = sensor_.init();
    initialised_ = isOk(status);
    faulty_      = !initialised_;
    if (initialised_) {
        filter_->reset();
        consecutiveErrors_ = 0u;
        /* Fire the first acquisition on the very next poll() instead of waiting
         * a full period - the display should show a value right after boot. */
        lastSampleTick_ = clock_.nowMs() - config_.samplePeriodMs;
    } else {
        lastRecoveryTick_ = clock_.nowMs();
    }
    return status;
}

void SensorService::setFilter(IFilter& filter) {
    filter_ = &filter;
    filter_->reset();
}

void SensorService::setSamplePeriod(uint32_t periodMs) {
    const uint32_t floorMs = sensor_.info().minPeriodMs;
    config_.samplePeriodMs = (periodMs < floorMs) ? floorMs : periodMs;
}

bool SensorService::poll() {
    const TimestampMs now = clock_.nowMs();

    if (!initialised_) {
        tryRecover(now);
        return false;
    }

    if (!isDue(now, lastSampleTick_, config_.samplePeriodMs)) {
        return false;
    }
    lastSampleTick_ = now;

    Sample sample;
    const Status status = sensor_.read(sample);

    if (status == Status::NotReady) {
        /* Conversion still running (slow devices such as a DS18B20 need 750 ms).
         * Not an error: we simply come back on the next tick. */
        return false;
    }

    if (!isOk(status)) {
        ++stats_.readErrors;
        if (consecutiveErrors_ < 0xFFu) { ++consecutiveErrors_; }
        if (consecutiveErrors_ >= config_.faultThreshold) {
            faulty_           = true;
            initialised_      = false;
            lastRecoveryTick_ = now;
        }
        if (config_.publishOnError) { publishError(status, now); }
        return config_.publishOnError;
    }

    const SensorInfo& info = sensor_.info();
    if (sample.value < info.minValue || sample.value > info.maxValue) {
        /* Physically impossible reading: a disconnected NTC reads far below the
         * range, a shorted one far above. Drop it before it poisons the filter. */
        ++stats_.samplesRejected;
        if (config_.publishOnError) { publishError(Status::OutOfRange, now); }
        return config_.publishOnError;
    }

    consecutiveErrors_ = 0u;
    faulty_            = false;
    ++stats_.samplesAccepted;

    last_.quantity  = info.quantity;
    last_.raw       = sample.value;
    last_.filtered  = filter_->update(sample.value, sample.timestamp);
    last_.timestamp = sample.timestamp;
    last_.status    = Status::Ok;
    last_.source    = info.name;

    publisher_.publish(last_);
    return true;
}

bool SensorService::tryRecover(TimestampMs now) {
    if (!isDue(now, lastRecoveryTick_, config_.reinitBackoffMs)) {
        return false;
    }
    lastRecoveryTick_ = now;
    ++stats_.reinitAttempts;

    if (isOk(sensor_.init())) {
        initialised_       = true;
        faulty_            = false;
        consecutiveErrors_ = 0u;
        filter_->reset();          // history is stale after a device reset
        lastSampleTick_    = now - config_.samplePeriodMs;
        return true;
    }
    return false;
}

void SensorService::publishError(Status status, TimestampMs now) {
    last_.status    = status;
    last_.timestamp = now;
    /* raw/filtered keep their previous values on purpose: the GUI can grey out
     * the last known reading instead of flashing a meaningless 0.0. */
    publisher_.publish(last_);
}

} // namespace sensorfw
