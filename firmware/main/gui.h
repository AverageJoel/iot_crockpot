/**
 * @file gui.h
 * @brief Crockpot touchscreen GUI using LVGL
 *
 * Four screens: Main (temperature + controls), Settings, WiFi, Info.
 * Status is polled every 500 ms via an LVGL timer — no separate FreeRTOS
 * task is needed. The LVGL task is managed by esp_lvgl_port.
 *
 * Thread safety: all public functions acquire the LVGL port lock
 * internally and are safe to call from any FreeRTOS task.
 *
 * Typical call order:
 *   display_init()   → hardware + LVGL up
 *   gui_init()       → create all LVGL screen objects
 *   gui_start()      → load main screen, start update timer
 */

#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GUI screen identifiers
 */
typedef enum {
    GUI_SCREEN_MAIN,            // Temperature + heat controls
    GUI_SCREEN_SETTINGS,        // Units toggle, misc settings
    GUI_SCREEN_WIFI,            // Connection status + IP
    GUI_SCREEN_INFO,            // Uptime, firmware version, chip
    GUI_SCREEN_HISTORY,         // Temperature history chart
    GUI_SCREEN_SCHEDULES,       // Schedule preset list
    GUI_SCREEN_SCHEDULE_BUILD,  // Custom schedule builder
    GUI_SCREEN_COUNT
} gui_screen_t;

/**
 * @brief Runtime GUI configuration
 */
typedef struct {
    bool    show_temperature_c;  // true = Celsius, false = Fahrenheit
    bool    show_wifi_status;    // show WiFi indicator in status bar
    uint8_t screen_timeout_s;   // dim backlight after N idle seconds (0 = never)
} gui_config_t;

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * @brief Create all LVGL screen objects.
 *
 * Must be called after display_init() (LVGL must be running).
 *
 * @return true on success
 */
bool gui_init(void);

/**
 * @brief Load the main screen and start the 500 ms status update timer.
 *
 * Must be called after gui_init().
 *
 * @return true on success
 */
bool gui_start(void);

// ============================================================================
// Navigation
// ============================================================================

/**
 * @brief Navigate to a screen with a slide animation.
 *
 * @param screen  Target screen
 */
void gui_set_screen(gui_screen_t screen);

/**
 * @brief Navigate back to the main screen.
 */
void gui_back(void);

/**
 * @brief Get the currently displayed screen.
 */
gui_screen_t gui_get_screen(void);

// ============================================================================
// Toast messages
// ============================================================================

/**
 * @brief Show a temporary toast message (blue, auto-dismisses).
 *
 * @param message     Text to display
 * @param duration_ms Auto-dismiss after this many ms (0 = until tapped)
 */
void gui_show_message(const char *message, uint32_t duration_ms);

/**
 * @brief Show a persistent error toast (red, tap to dismiss).
 *
 * @param error  Error text
 */
void gui_show_error(const char *error);

/**
 * @brief Dismiss the active toast (if any).
 */
void gui_dismiss_message(void);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Update GUI configuration (temperature units, backlight timeout, etc.)
 */
void gui_set_config(const gui_config_t *config);

/**
 * @brief Get current GUI configuration.
 */
gui_config_t gui_get_config(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_H
