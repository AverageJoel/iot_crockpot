/**
 * @file display.h
 * @brief Local display interface (OLED/touchscreen)
 *
 * Handles local UI rendering and button/touch input.
 * Display hardware TBD (OLED + buttons or touchscreen).
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display type enumeration
 */
typedef enum {
    DISPLAY_TYPE_NONE,
    DISPLAY_TYPE_OLED_SSD1306,
    DISPLAY_TYPE_TFT_ILI9341,
    // Add more display types as needed
} display_type_t;

/**
 * @brief Initialize display subsystem
 *
 * Detects connected display and initializes driver.
 *
 * @return true on success, false if no display found
 */
bool display_init(void);

/**
 * @brief Main display task
 *
 * FreeRTOS task that handles:
 * - Screen rendering/updates
 * - Button/touch input handling
 * - UI state management
 *
 * @param pvParameters Task parameters (unused)
 */
void display_task(void* pvParameters);

/**
 * @brief Force display refresh
 *
 * Triggers immediate screen update.
 */
void display_refresh(void);

/**
 * @brief Show message on display
 *
 * Displays a temporary message overlay.
 *
 * @param message Message to display
 * @param duration_ms Duration to show message (0 = until cleared)
 */
void display_show_message(const char* message, uint32_t duration_ms);

/**
 * @brief Clear message overlay
 */
void display_clear_message(void);

/**
 * @brief Set display brightness
 *
 * @param brightness 0-100 percentage
 */
void display_set_brightness(uint8_t brightness);

/**
 * @brief Get current display type
 *
 * @return Detected display type
 */
display_type_t display_get_type(void);

// Display configuration (TODO: move to menuconfig)
#define DISPLAY_SDA_GPIO 21
#define DISPLAY_SCL_GPIO 22
#define DISPLAY_WIDTH    128
#define DISPLAY_HEIGHT   64

// Button GPIOs (if using OLED + buttons)
#define BUTTON_UP_GPIO    12
#define BUTTON_DOWN_GPIO  13
#define BUTTON_SELECT_GPIO 14

// Display update interval
#define DISPLAY_UPDATE_INTERVAL_MS 250

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
