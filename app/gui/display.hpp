/**
 * @file display.hpp
 * @brief Minimal display abstraction so the GUI code is not tied to a screen.
 *
 * A real product would put LVGL, u8g2 or a segment LCD behind this interface;
 * the host build puts a terminal behind it. The view code above does not change.
 */
#ifndef SENSORFW_APP_GUI_DISPLAY_HPP
#define SENSORFW_APP_GUI_DISPLAY_HPP

#include "sensorfw/core/types.hpp"

namespace sensorfw {
namespace app {

class IDisplay {
public:
    /** @brief Erase the back buffer. */
    virtual void clear() = 0;

    /** @brief Draw a NUL terminated string at a character cell position. */
    virtual void drawText(uint8_t column, uint8_t row, const char* text) = 0;

    /** @brief Push the back buffer to the panel (one SPI/I2C burst on target). */
    virtual void flush() = 0;

protected:
    ~IDisplay() {}
};

} // namespace app
} // namespace sensorfw

#endif // SENSORFW_APP_GUI_DISPLAY_HPP
