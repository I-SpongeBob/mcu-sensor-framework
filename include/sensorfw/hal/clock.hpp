/**
 * @file clock.hpp
 * @brief Time source abstraction - the only thing the framework needs from the
 *        RTOS/bare-metal environment.
 */
#ifndef SENSORFW_HAL_CLOCK_HPP
#define SENSORFW_HAL_CLOCK_HPP

#include "sensorfw/config.hpp"

namespace sensorfw {
namespace hal {

/**
 * @brief Monotonic millisecond clock.
 *
 * STM32  -> HAL_GetTick()
 * ESP32  -> esp_timer_get_time()/1000 or xTaskGetTickCount()
 * Linux  -> std::chrono::steady_clock
 *
 * Note the protected non-virtual destructor: the framework never deletes
 * through a base pointer (there is no heap), so paying for a virtual
 * destructor slot in every vtable would be pure waste. The compiler enforces
 * the rule for us.
 */
class IClock {
public:
    virtual TimestampMs nowMs() const = 0;
protected:
    ~IClock() {}
};

} // namespace hal
} // namespace sensorfw

#endif // SENSORFW_HAL_CLOCK_HPP
