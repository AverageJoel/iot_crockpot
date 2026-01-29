/**
 * @file touch_hal.c
 * @brief Touch HAL stub implementation
 *
 * Stub that provides no input. Replace with actual driver implementation
 * when touch hardware is selected.
 *
 * To implement for specific hardware:
 * 1. Copy this file to touch_hal_<driver>.c
 * 2. Implement actual hardware communication
 * 3. Update CMakeLists.txt to use your implementation
 */

#include "touch_hal.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "touch_hal";

// Stub touch info
static touch_info_t s_info = {
    .type = TOUCH_TYPE_NONE,
    .multitouch = false,
    .pressure_sense = false,
    .width = 320,
    .height = 240,
    .num_buttons = 0,
    .initialized = false
};

// Configuration
static uint32_t s_long_press_ms = 500;
static uint32_t s_debounce_ms = 50;

// Event callback
static touch_callback_t s_callback = NULL;
static void* s_callback_user_data = NULL;

// Touch state
static bool s_pressed = false;
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;

bool touch_hal_init(void)
{
    ESP_LOGI(TAG, "Touch HAL initializing (STUB)");

    // TODO: Initialize actual touch hardware here
    // For XPT2046 (resistive):
    //   - Configure SPI for touch controller
    //   - Load calibration from NVS
    // For FT6236 (capacitive):
    //   - Configure I2C
    //   - Read chip ID to verify communication
    // For buttons:
    //   - Configure GPIO inputs with pull-ups/interrupts

    s_info.type = TOUCH_TYPE_NONE;
    s_info.initialized = true;

    ESP_LOGI(TAG, "Touch HAL initialized (STUB - implement real driver)");
    return true;
}

touch_info_t touch_hal_get_info(void)
{
    return s_info;
}

bool touch_hal_is_pressed(void)
{
    // TODO: Check actual touch/button state
    return s_pressed;
}

bool touch_hal_get_point(int16_t* x, int16_t* y)
{
    if (!s_pressed) {
        return false;
    }

    if (x) *x = s_last_x;
    if (y) *y = s_last_y;

    return true;
}

uint8_t touch_hal_get_pressure(void)
{
    // TODO: For resistive touch, calculate pressure from Z measurements
    return s_pressed ? 128 : 0;
}

bool touch_hal_button_pressed(button_id_t button)
{
    // TODO: Check specific button GPIO state
    (void)button;
    return false;
}

bool touch_hal_poll_event(touch_event_t* event)
{
    if (event == NULL) {
        return false;
    }

    // TODO: Implement actual event detection:
    // 1. Read current touch/button state
    // 2. Compare with previous state
    // 3. Detect press, release, move, long_press
    // 4. Apply debouncing
    // 5. Populate event structure

    // Stub: no events
    event->type = TOUCH_EVENT_NONE;
    return false;
}

void touch_hal_set_callback(touch_callback_t callback, void* user_data)
{
    s_callback = callback;
    s_callback_user_data = user_data;
}

bool touch_hal_start_calibration(void)
{
    ESP_LOGI(TAG, "start_calibration() - STUB");

    // TODO: For resistive touch, implement 3-point calibration:
    // 1. Display target at top-left, wait for touch
    // 2. Display target at top-right, wait for touch
    // 3. Display target at bottom-center, wait for touch
    // 4. Calculate calibration matrix

    return false;
}

bool touch_hal_needs_calibration(void)
{
    // TODO: Check if calibration data exists in NVS
    // For resistive: usually needs calibration
    // For capacitive: usually pre-calibrated
    return (s_info.type == TOUCH_TYPE_RESISTIVE);
}

bool touch_hal_save_calibration(void)
{
    ESP_LOGI(TAG, "save_calibration() - STUB");

    // TODO: Save calibration matrix to NVS

    return false;
}

void touch_hal_set_long_press_duration(uint32_t duration_ms)
{
    s_long_press_ms = duration_ms;
}

void touch_hal_set_debounce(uint32_t debounce_ms)
{
    s_debounce_ms = debounce_ms;
}

void touch_hal_set_rotation(uint16_t rotation)
{
    ESP_LOGI(TAG, "set_rotation(%d)", rotation);

    // TODO: Update coordinate transformation matrix
    // to match display rotation
}

// ============================================================================
// Internal helper for drivers to emit events
// ============================================================================

/**
 * @brief Emit a touch event (call from driver implementation)
 *
 * @param event Event to emit
 */
void touch_hal_emit_event(const touch_event_t* event)
{
    if (event == NULL) return;

    ESP_LOGD(TAG, "Event: type=%d x=%d y=%d button=%d",
             event->type, event->x, event->y, event->button);

    if (s_callback) {
        s_callback(event, s_callback_user_data);
    }
}
