/**
 * @file display_driver.h
 * @brief ST7796 SPI display driver
 *
 * Initializes the 3.5" ST7796 IPS panel over SPI using the ESP-IDF
 * esp_lcd component. Provides the panel handle that LVGL will use
 * for flush callbacks in Phase 5.
 *
 * Hardware: lcdwiki MSP3526 (or compatible) — 480x320, landscape.
 * Interface: 14-pin SPI module (LCD_CS, LCD_DC, LCD_RST, SDI, SCK, LED, SDO).
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the ST7796 display
 *
 * Initializes the shared SPI bus, creates the esp_lcd panel IO and
 * ST7796 panel handle, runs the init sequence, and enables the backlight.
 *
 * @return true on success
 */
bool display_driver_init(void);

/**
 * @brief Get the esp_lcd panel handle
 *
 * Used by LVGL (Phase 5) to set up the flush callback.
 *
 * @return Panel handle, or NULL if not initialized
 */
esp_lcd_panel_handle_t display_driver_get_panel(void);

/**
 * @brief Get the esp_lcd panel IO handle
 *
 * Used by LVGL (Phase 5) to register the color-transfer-done callback.
 *
 * @return Panel IO handle, or NULL if not initialized
 */
esp_lcd_panel_io_handle_t display_driver_get_io(void);

/**
 * @brief Control the display backlight
 *
 * @param on true to turn backlight on, false to turn off
 */
void display_driver_set_backlight(bool on);

/**
 * @brief Fill the entire screen with a solid color
 *
 * Used to verify the display is working before LVGL is integrated.
 * Writes pixel data directly via esp_lcd_panel_draw_bitmap().
 *
 * @param color_rgb565 Color in RGB565 format (e.g. 0xF800 = red)
 */
void display_driver_fill(uint16_t color_rgb565);

// Display pixel dimensions (landscape after swap_xy)
#define LCD_H_RES  CONFIG_CROCKPOT_LCD_WIDTH   // 480
#define LCD_V_RES  CONFIG_CROCKPOT_LCD_HEIGHT  // 320

// SPI clock for pixel data (40 MHz is safe for initial bring-up; try 80 MHz later)
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_DRIVER_H
