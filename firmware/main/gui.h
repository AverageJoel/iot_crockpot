/**
 * @file gui.h
 * @brief Crockpot GUI Layer
 *
 * High-level GUI interface for the crockpot controller.
 * Uses display_hal.h and touch_hal.h for hardware abstraction.
 *
 * Screen hierarchy:
 * - Main Screen: Shows current state, temperature, relay status
 * - Settings Screen: Adjust temperature limits, timers
 * - WiFi Screen: Network status and configuration
 * - Info Screen: Device info, uptime, version
 */

#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stdint.h>
#include "crockpot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GUI screen identifiers
 */
typedef enum {
    GUI_SCREEN_MAIN,        // Main status display
    GUI_SCREEN_SETTINGS,    // Settings menu
    GUI_SCREEN_WIFI,        // WiFi configuration
    GUI_SCREEN_INFO,        // Device info
    GUI_SCREEN_CALIBRATE,   // Touch calibration
    GUI_SCREEN_COUNT        // Number of screens
} gui_screen_t;

/**
 * @brief GUI theme colors
 */
typedef struct {
    uint16_t background;
    uint16_t text;
    uint16_t text_dim;
    uint16_t accent;
    uint16_t state_off;
    uint16_t state_warm;
    uint16_t state_low;
    uint16_t state_high;
    uint16_t error;
    uint16_t success;
} gui_theme_t;

/**
 * @brief GUI configuration
 */
typedef struct {
    bool show_temperature_c;    // Show Celsius (false = Fahrenheit)
    bool show_wifi_status;      // Show WiFi indicator
    uint8_t screen_timeout_s;   // Screen dim timeout (0 = never)
    uint8_t brightness;         // Default brightness (0-100)
} gui_config_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize the GUI subsystem
 *
 * Initializes display and touch HALs, loads theme and config.
 *
 * @return true on success
 */
bool gui_init(void);

/**
 * @brief Start the GUI task
 *
 * Creates the FreeRTOS task that handles GUI updates.
 *
 * @return true if task created successfully
 */
bool gui_start(void);

// ============================================================================
// Screen Management
// ============================================================================

/**
 * @brief Switch to a different screen
 *
 * @param screen Screen to display
 */
void gui_set_screen(gui_screen_t screen);

/**
 * @brief Get current screen
 *
 * @return Current screen identifier
 */
gui_screen_t gui_get_screen(void);

/**
 * @brief Go back to previous screen
 */
void gui_back(void);

// ============================================================================
// Status Updates
// ============================================================================

/**
 * @brief Update GUI with current crockpot status
 *
 * Called periodically to refresh displayed data.
 *
 * @param status Current crockpot status
 */
void gui_update_status(const crockpot_status_t* status);

/**
 * @brief Show temporary message overlay
 *
 * @param message Message to display
 * @param duration_ms Display duration (0 = until dismissed)
 */
void gui_show_message(const char* message, uint32_t duration_ms);

/**
 * @brief Show error message
 *
 * @param error Error message
 */
void gui_show_error(const char* error);

/**
 * @brief Dismiss any active message/error overlay
 */
void gui_dismiss_message(void);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Get current GUI configuration
 *
 * @return Configuration structure
 */
gui_config_t gui_get_config(void);

/**
 * @brief Set GUI configuration
 *
 * @param config New configuration
 */
void gui_set_config(const gui_config_t* config);

/**
 * @brief Set GUI theme colors
 *
 * @param theme Theme colors
 */
void gui_set_theme(const gui_theme_t* theme);

/**
 * @brief Get current theme
 *
 * @return Current theme
 */
gui_theme_t gui_get_theme(void);

// ============================================================================
// Interaction
// ============================================================================

/**
 * @brief Wake up display (e.g., on touch)
 *
 * Resets screen timeout, restores brightness.
 */
void gui_wake(void);

/**
 * @brief Check if display is currently dimmed/off
 *
 * @return true if screen is dimmed
 */
bool gui_is_dimmed(void);

/**
 * @brief Force display refresh
 */
void gui_refresh(void);

// ============================================================================
// Default Theme
// ============================================================================

/**
 * @brief Get default dark theme
 *
 * @return Default dark theme
 */
gui_theme_t gui_default_dark_theme(void);

/**
 * @brief Get default light theme
 *
 * @return Default light theme
 */
gui_theme_t gui_default_light_theme(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_H
