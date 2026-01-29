/**
 * @file touch_hal.h
 * @brief Touch/Input Hardware Abstraction Layer
 *
 * Abstract interface for touch and button input. Allows GUI code to be
 * developed independently of the actual input hardware.
 *
 * Implementations:
 * - touch_hal_none.c     (stub, no input)
 * - touch_hal_buttons.c  (GPIO buttons)
 * - touch_hal_xpt2046.c  (resistive touch via SPI)
 * - touch_hal_ft6236.c   (capacitive touch via I2C)
 */

#ifndef TOUCH_HAL_H
#define TOUCH_HAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch input type
 */
typedef enum {
    TOUCH_TYPE_NONE,        // No touch input available
    TOUCH_TYPE_BUTTONS,     // Physical buttons only
    TOUCH_TYPE_RESISTIVE,   // Resistive touchscreen (XPT2046)
    TOUCH_TYPE_CAPACITIVE   // Capacitive touchscreen (FT6236)
} touch_type_t;

/**
 * @brief Touch event types
 */
typedef enum {
    TOUCH_EVENT_NONE,       // No event
    TOUCH_EVENT_PRESS,      // Touch/button press started
    TOUCH_EVENT_RELEASE,    // Touch/button released
    TOUCH_EVENT_MOVE,       // Touch point moved (drag)
    TOUCH_EVENT_LONG_PRESS  // Long press detected
} touch_event_type_t;

/**
 * @brief Virtual button IDs (for button-based input)
 *
 * These map physical buttons or touch zones to logical actions.
 */
typedef enum {
    BUTTON_NONE = 0,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_SELECT,
    BUTTON_BACK,
    BUTTON_POWER
} button_id_t;

/**
 * @brief Touch event data
 */
typedef struct {
    touch_event_type_t type;    // Event type
    int16_t x;                  // X coordinate (for touch)
    int16_t y;                  // Y coordinate (for touch)
    button_id_t button;         // Button ID (for button events)
    uint32_t timestamp_ms;      // Event timestamp
    uint8_t pressure;           // Touch pressure (0-255, resistive only)
} touch_event_t;

/**
 * @brief Touch capabilities
 */
typedef struct {
    touch_type_t type;          // Input type
    bool multitouch;            // Supports multiple touch points
    bool pressure_sense;        // Has pressure sensing
    uint16_t width;             // Touch area width
    uint16_t height;            // Touch area height
    uint8_t num_buttons;        // Number of physical buttons
    bool initialized;           // Successfully initialized
} touch_info_t;

/**
 * @brief Touch event callback type
 */
typedef void (*touch_callback_t)(const touch_event_t* event, void* user_data);

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize touch input hardware
 *
 * @return true on success, false if no input found
 */
bool touch_hal_init(void);

/**
 * @brief Get touch capabilities
 *
 * @return Touch info structure
 */
touch_info_t touch_hal_get_info(void);

// ============================================================================
// Polling Interface
// ============================================================================

/**
 * @brief Check if touch/button is currently pressed
 *
 * @return true if pressed
 */
bool touch_hal_is_pressed(void);

/**
 * @brief Get current touch position
 *
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 * @return true if touch is active
 */
bool touch_hal_get_point(int16_t* x, int16_t* y);

/**
 * @brief Get touch pressure (resistive only)
 *
 * @return Pressure value 0-255, or 0 if not pressed
 */
uint8_t touch_hal_get_pressure(void);

/**
 * @brief Check if a specific button is pressed
 *
 * @param button Button ID to check
 * @return true if pressed
 */
bool touch_hal_button_pressed(button_id_t button);

// ============================================================================
// Event Interface
// ============================================================================

/**
 * @brief Poll for touch events
 *
 * Non-blocking check for pending touch events.
 *
 * @param event Pointer to store event data
 * @return true if event available
 */
bool touch_hal_poll_event(touch_event_t* event);

/**
 * @brief Register touch event callback
 *
 * Callback is invoked from touch task when events occur.
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void touch_hal_set_callback(touch_callback_t callback, void* user_data);

// ============================================================================
// Calibration
// ============================================================================

/**
 * @brief Start touch calibration
 *
 * For resistive touchscreens, initiates calibration sequence.
 *
 * @return true if calibration started
 */
bool touch_hal_start_calibration(void);

/**
 * @brief Check if calibration is needed
 *
 * @return true if calibration data is missing or invalid
 */
bool touch_hal_needs_calibration(void);

/**
 * @brief Save calibration to NVS
 *
 * @return true on success
 */
bool touch_hal_save_calibration(void);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Set long press duration threshold
 *
 * @param duration_ms Duration in milliseconds (default 500ms)
 */
void touch_hal_set_long_press_duration(uint32_t duration_ms);

/**
 * @brief Set touch debounce time
 *
 * @param debounce_ms Debounce time in milliseconds (default 50ms)
 */
void touch_hal_set_debounce(uint32_t debounce_ms);

/**
 * @brief Set coordinate mapping/rotation
 *
 * Maps raw touch coordinates to display coordinates.
 *
 * @param rotation 0, 90, 180, or 270 degrees
 */
void touch_hal_set_rotation(uint16_t rotation);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_HAL_H
