/**
 * @file touch_driver.h
 * @brief FT6336U capacitive touch driver
 *
 * Initializes the FT6336U over I2C using the ESP-IDF esp_lcd_touch_ft5x06
 * component (the FT6336U is register-compatible with the FT5x06 family).
 * Provides the touch handle that LVGL will use for its input driver in Phase 5.
 *
 * Coordinate orientation is set to match the display:
 *   swap_xy=true, mirror_x=true (landscape, same as display_driver.c)
 */

#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Single touch point reading
 */
typedef struct {
    bool     pressed;  // true if a touch is currently active
    uint16_t x;        // X coordinate in display pixels
    uint16_t y;        // Y coordinate in display pixels
} touch_point_t;

/**
 * @brief Initialize the FT6336U touch controller
 *
 * Installs the I2C driver, creates the esp_lcd_touch panel IO and
 * touch handle. Safe to call before or after display_driver_init().
 *
 * @return true on success
 */
bool touch_driver_init(void);

/**
 * @brief Get the esp_lcd_touch handle
 *
 * Used by LVGL (Phase 5) to register the input read callback.
 *
 * @return Touch handle, or NULL if not initialized
 */
esp_lcd_touch_handle_t touch_driver_get_handle(void);

/**
 * @brief Read the current touch state directly from hardware (blocking I2C).
 *
 * Prefer touch_driver_read_cached() for the LVGL indev callback — this
 * function is called internally by the touch poll task.
 *
 * @param point  Output: current touch state and coordinates
 * @return true if touch is currently pressed
 */
bool touch_driver_read(touch_point_t *point);

/**
 * @brief Read the most recent touch state from the poll-task cache.
 *
 * Returns immediately (no I2C traffic).  The background touch task keeps
 * this cache updated at ~8 ms regardless of what LVGL is doing, so the
 * LVGL indev callback always gets current data even during heavy rendering.
 *
 * @param point  Output: latest touch state and coordinates
 * @return true if touch is currently pressed
 */
bool touch_driver_read_cached(touch_point_t *point);

/**
 * @brief Start the background touch polling task.
 *
 * Creates a FreeRTOS task that reads the FT6336U every 8 ms and stores
 * the result in an internal cache.  Call once after touch_driver_init().
 *
 * If CONFIG_CROCKPOT_TOUCH_INT >= 0 (INT pin wired), the task uses a
 * GPIO interrupt instead of a fixed interval — giving sub-ms press latency.
 */
void touch_driver_start_task(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_DRIVER_H
