/**
 * @file display_driver.h
 * @brief display_driver stub for PC simulator.
 *
 * The real display_driver.h pulls in ESP-IDF esp_lcd headers.
 * gui.c only needs display_driver_set_backlight() and the LCD dimension
 * defines, so this stub provides exactly those.
 */

#ifndef DISPLAY_DRIVER_STUB_H
#define DISPLAY_DRIVER_STUB_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Control the backlight — stub logs to stderr or updates window title.
 */
void display_driver_set_backlight(bool on);

/* Display pixel dimensions — match the firmware Kconfig defaults */
#define LCD_H_RES  480
#define LCD_V_RES  320

#endif /* DISPLAY_DRIVER_STUB_H */
