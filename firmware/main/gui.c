/**
 * @file gui.c
 * @brief Crockpot GUI implementation
 *
 * Uses display_hal.h and touch_hal.h for hardware abstraction.
 */

#include "gui.h"
#include "display_hal.h"
#include "touch_hal.h"
#include "crockpot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "gui";

// ============================================================================
// State
// ============================================================================

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;

// Current screen
static gui_screen_t s_current_screen = GUI_SCREEN_MAIN;
static gui_screen_t s_previous_screen = GUI_SCREEN_MAIN;

// Configuration and theme
static gui_config_t s_config = {
    .show_temperature_c = false,
    .show_wifi_status = true,
    .screen_timeout_s = 30,
    .brightness = 80
};

static gui_theme_t s_theme;

// Cached status for rendering
static crockpot_status_t s_status = {0};

// Message overlay
static char s_message[64] = "";
static uint32_t s_message_until_ms = 0;
static bool s_message_is_error = false;

// Screen timeout
static uint32_t s_last_interaction_ms = 0;
static bool s_dimmed = false;

// Display info cache
static display_info_t s_display_info;

// ============================================================================
// Default Themes
// ============================================================================

gui_theme_t gui_default_dark_theme(void)
{
    return (gui_theme_t){
        .background  = COLOR_BLACK,
        .text        = COLOR_WHITE,
        .text_dim    = COLOR_GRAY,
        .accent      = COLOR_BLUE,
        .state_off   = COLOR_DARK_GRAY,
        .state_warm  = COLOR_YELLOW,
        .state_low   = COLOR_ORANGE,
        .state_high  = COLOR_RED,
        .error       = COLOR_RED,
        .success     = COLOR_GREEN
    };
}

gui_theme_t gui_default_light_theme(void)
{
    return (gui_theme_t){
        .background  = COLOR_WHITE,
        .text        = COLOR_BLACK,
        .text_dim    = COLOR_GRAY,
        .accent      = COLOR_BLUE,
        .state_off   = COLOR_GRAY,
        .state_warm  = COLOR_RGB(200, 150, 0),
        .state_low   = COLOR_ORANGE,
        .state_high  = COLOR_RED,
        .error       = COLOR_RED,
        .success     = COLOR_GREEN
    };
}

// ============================================================================
// Screen Rendering
// ============================================================================

/**
 * @brief Get color for crockpot state
 */
static color_t get_state_color(crockpot_state_t state)
{
    switch (state) {
        case CROCKPOT_OFF:  return s_theme.state_off;
        case CROCKPOT_WARM: return s_theme.state_warm;
        case CROCKPOT_LOW:  return s_theme.state_low;
        case CROCKPOT_HIGH: return s_theme.state_high;
        default:            return s_theme.text;
    }
}

/**
 * @brief Render main screen
 */
static void render_main_screen(void)
{
    int16_t w = s_display_info.width;
    int16_t h = s_display_info.height;
    int16_t cx = w / 2;

    // State indicator (large, centered at top)
    const char* state_str = crockpot_state_to_string(s_status.state);
    color_t state_color = get_state_color(s_status.state);

    display_hal_text(cx, 20, state_str, FONT_XLARGE, state_color, ALIGN_CENTER);

    // Temperature (large, centered)
    char temp_str[16];
    if (s_config.show_temperature_c) {
        float temp_c = (s_status.temperature_f - 32.0f) * 5.0f / 9.0f;
        snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_c);
    } else {
        snprintf(temp_str, sizeof(temp_str), "%.1f F", s_status.temperature_f);
    }
    display_hal_text(cx, 60, temp_str, FONT_LARGE, s_theme.text, ALIGN_CENTER);

    // Sensor error indicator
    if (s_status.sensor_error) {
        display_hal_text(cx, 90, "SENSOR ERROR", FONT_SMALL, s_theme.error, ALIGN_CENTER);
    }

    // Status bar at bottom
    int16_t bar_y = h - 30;

    // WiFi indicator
    if (s_config.show_wifi_status) {
        const char* wifi_str = s_status.wifi_connected ? "WiFi" : "----";
        color_t wifi_color = s_status.wifi_connected ? s_theme.success : s_theme.text_dim;
        display_hal_text(10, bar_y, wifi_str, FONT_SMALL, wifi_color, ALIGN_LEFT);
    }

    // Uptime
    char uptime_str[16];
    uint32_t h_up = s_status.uptime_seconds / 3600;
    uint32_t m_up = (s_status.uptime_seconds % 3600) / 60;
    snprintf(uptime_str, sizeof(uptime_str), "%02lu:%02lu", h_up, m_up);
    display_hal_text(w - 10, bar_y, uptime_str, FONT_SMALL, s_theme.text_dim, ALIGN_RIGHT);

    // Touch zones (visual hints for touchscreen)
    touch_info_t touch_info = touch_hal_get_info();
    if (touch_info.type != TOUCH_TYPE_NONE && touch_info.type != TOUCH_TYPE_BUTTONS) {
        // Draw state change buttons
        int16_t btn_y = h - 70;
        int16_t btn_w = 60;
        int16_t btn_h = 30;

        // DOWN button
        display_hal_rect(20, btn_y, btn_w, btn_h, s_theme.text_dim);
        display_hal_text(20 + btn_w/2, btn_y + 8, "-", FONT_MEDIUM, s_theme.text, ALIGN_CENTER);

        // UP button
        display_hal_rect(w - 20 - btn_w, btn_y, btn_w, btn_h, s_theme.text_dim);
        display_hal_text(w - 20 - btn_w/2, btn_y + 8, "+", FONT_MEDIUM, s_theme.text, ALIGN_CENTER);
    }
}

/**
 * @brief Render settings screen
 */
static void render_settings_screen(void)
{
    int16_t cx = s_display_info.width / 2;

    display_hal_text(cx, 10, "Settings", FONT_LARGE, s_theme.accent, ALIGN_CENTER);

    // TODO: Implement settings menu items
    display_hal_text(cx, 50, "Not implemented", FONT_SMALL, s_theme.text_dim, ALIGN_CENTER);

    // Back hint
    display_hal_text(cx, s_display_info.height - 20, "Touch to go back",
                     FONT_SMALL, s_theme.text_dim, ALIGN_CENTER);
}

/**
 * @brief Render WiFi screen
 */
static void render_wifi_screen(void)
{
    int16_t cx = s_display_info.width / 2;

    display_hal_text(cx, 10, "WiFi", FONT_LARGE, s_theme.accent, ALIGN_CENTER);

    const char* status = s_status.wifi_connected ? "Connected" : "Disconnected";
    color_t status_color = s_status.wifi_connected ? s_theme.success : s_theme.error;
    display_hal_text(cx, 50, status, FONT_MEDIUM, status_color, ALIGN_CENTER);

    // TODO: Show SSID, IP address, signal strength

    display_hal_text(cx, s_display_info.height - 20, "Touch to go back",
                     FONT_SMALL, s_theme.text_dim, ALIGN_CENTER);
}

/**
 * @brief Render info screen
 */
static void render_info_screen(void)
{
    int16_t cx = s_display_info.width / 2;

    display_hal_text(cx, 10, "Device Info", FONT_LARGE, s_theme.accent, ALIGN_CENTER);

    // Uptime
    char uptime[32];
    uint32_t days = s_status.uptime_seconds / 86400;
    uint32_t hours = (s_status.uptime_seconds % 86400) / 3600;
    uint32_t mins = (s_status.uptime_seconds % 3600) / 60;
    snprintf(uptime, sizeof(uptime), "Uptime: %lud %02lu:%02lu", days, hours, mins);
    display_hal_text(cx, 50, uptime, FONT_SMALL, s_theme.text, ALIGN_CENTER);

    // Version
    display_hal_text(cx, 70, "v1.0.0", FONT_SMALL, s_theme.text_dim, ALIGN_CENTER);

    display_hal_text(cx, s_display_info.height - 20, "Touch to go back",
                     FONT_SMALL, s_theme.text_dim, ALIGN_CENTER);
}

/**
 * @brief Render current screen
 */
static void render_screen(void)
{
    // Clear screen
    display_hal_clear(s_theme.background);

    // Render active screen
    switch (s_current_screen) {
        case GUI_SCREEN_MAIN:
            render_main_screen();
            break;
        case GUI_SCREEN_SETTINGS:
            render_settings_screen();
            break;
        case GUI_SCREEN_WIFI:
            render_wifi_screen();
            break;
        case GUI_SCREEN_INFO:
            render_info_screen();
            break;
        default:
            break;
    }

    // Message overlay
    if (s_message[0] != '\0') {
        int16_t cx = s_display_info.width / 2;
        int16_t cy = s_display_info.height / 2;
        int16_t box_w = s_display_info.width - 40;
        int16_t box_h = 40;

        color_t box_color = s_message_is_error ? s_theme.error : s_theme.accent;

        display_hal_fill_round_rect(20, cy - box_h/2, box_w, box_h, 5, box_color);
        display_hal_text(cx, cy - 6, s_message, FONT_MEDIUM, COLOR_WHITE, ALIGN_CENTER);
    }

    // Flush to display
    display_hal_flush();
}

// ============================================================================
// Touch Handling
// ============================================================================

/**
 * @brief Handle touch event on main screen
 */
static void handle_main_touch(int16_t x, int16_t y)
{
    int16_t w = s_display_info.width;
    int16_t h = s_display_info.height;

    // Check button zones
    int16_t btn_y = h - 70;
    int16_t btn_h = 30;

    if (y >= btn_y && y <= btn_y + btn_h) {
        crockpot_state_t current = s_status.state;
        crockpot_state_t new_state = current;

        // Left button (decrease)
        if (x < w / 3) {
            switch (current) {
                case CROCKPOT_HIGH: new_state = CROCKPOT_LOW;  break;
                case CROCKPOT_LOW:  new_state = CROCKPOT_WARM; break;
                case CROCKPOT_WARM: new_state = CROCKPOT_OFF;  break;
                default: break;
            }
        }
        // Right button (increase)
        else if (x > 2 * w / 3) {
            switch (current) {
                case CROCKPOT_OFF:  new_state = CROCKPOT_WARM; break;
                case CROCKPOT_WARM: new_state = CROCKPOT_LOW;  break;
                case CROCKPOT_LOW:  new_state = CROCKPOT_HIGH; break;
                default: break;
            }
        }

        if (new_state != current) {
            crockpot_set_state(new_state);
            gui_show_message(crockpot_state_to_string(new_state), 1000);
        }
    }
}

/**
 * @brief Touch event callback
 */
static void touch_callback(const touch_event_t* event, void* user_data)
{
    (void)user_data;

    if (event->type == TOUCH_EVENT_PRESS || event->type == TOUCH_EVENT_RELEASE) {
        // Wake display on any touch
        gui_wake();
    }

    if (event->type != TOUCH_EVENT_PRESS) {
        return;
    }

    ESP_LOGD(TAG, "Touch at (%d, %d)", event->x, event->y);

    // Dismiss message overlay first
    if (s_message[0] != '\0') {
        gui_dismiss_message();
        return;
    }

    // Handle screen-specific touch
    switch (s_current_screen) {
        case GUI_SCREEN_MAIN:
            handle_main_touch(event->x, event->y);
            break;

        case GUI_SCREEN_SETTINGS:
        case GUI_SCREEN_WIFI:
        case GUI_SCREEN_INFO:
            gui_back();
            break;

        default:
            break;
    }
}

// ============================================================================
// GUI Task
// ============================================================================

static void gui_task(void* pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "GUI task started");

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t update_period = pdMS_TO_TICKS(100);  // 10 Hz

    while (1) {
        // Get current time
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        // Poll for touch events
        touch_event_t event;
        while (touch_hal_poll_event(&event)) {
            touch_callback(&event, NULL);
        }

        // Check message timeout
        if (s_message[0] != '\0' && s_message_until_ms > 0) {
            if (now_ms >= s_message_until_ms) {
                gui_dismiss_message();
            }
        }

        // Check screen timeout
        if (s_config.screen_timeout_s > 0 && !s_dimmed) {
            uint32_t idle_ms = now_ms - s_last_interaction_ms;
            if (idle_ms > s_config.screen_timeout_s * 1000) {
                s_dimmed = true;
                display_hal_set_brightness(10);  // Dim to 10%
            }
        }

        // Update cached status
        s_status = crockpot_get_status();

        // Render
        render_screen();

        // Wait for next update
        vTaskDelayUntil(&last_wake, update_period);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool gui_init(void)
{
    if (s_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing GUI");

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // Initialize display HAL
    if (!display_hal_init()) {
        ESP_LOGE(TAG, "Display HAL init failed");
        return false;
    }
    s_display_info = display_hal_get_info();

    // Initialize touch HAL
    if (!touch_hal_init()) {
        ESP_LOGW(TAG, "Touch HAL init failed - continuing without touch");
    }

    // Set up touch callback
    touch_hal_set_callback(touch_callback, NULL);

    // Load default theme
    s_theme = gui_default_dark_theme();

    // Set initial brightness
    display_hal_set_brightness(s_config.brightness);

    s_last_interaction_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_initialized = true;

    ESP_LOGI(TAG, "GUI initialized (%dx%d display)",
             s_display_info.width, s_display_info.height);

    return true;
}

bool gui_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "GUI not initialized");
        return false;
    }

    BaseType_t ret = xTaskCreate(
        gui_task,
        "gui_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task");
        return false;
    }

    ESP_LOGI(TAG, "GUI task started");
    return true;
}

void gui_set_screen(gui_screen_t screen)
{
    if (screen >= GUI_SCREEN_COUNT) {
        return;
    }

    s_previous_screen = s_current_screen;
    s_current_screen = screen;
    gui_wake();

    ESP_LOGD(TAG, "Screen changed to %d", screen);
}

gui_screen_t gui_get_screen(void)
{
    return s_current_screen;
}

void gui_back(void)
{
    s_current_screen = s_previous_screen;
    s_previous_screen = GUI_SCREEN_MAIN;
    gui_wake();
}

void gui_update_status(const crockpot_status_t* status)
{
    if (status != NULL) {
        s_status = *status;
    }
}

void gui_show_message(const char* message, uint32_t duration_ms)
{
    if (message == NULL) {
        return;
    }

    strncpy(s_message, message, sizeof(s_message) - 1);
    s_message[sizeof(s_message) - 1] = '\0';
    s_message_is_error = false;

    if (duration_ms > 0) {
        s_message_until_ms = (uint32_t)(esp_timer_get_time() / 1000) + duration_ms;
    } else {
        s_message_until_ms = 0;
    }

    gui_wake();
}

void gui_show_error(const char* error)
{
    if (error == NULL) {
        return;
    }

    strncpy(s_message, error, sizeof(s_message) - 1);
    s_message[sizeof(s_message) - 1] = '\0';
    s_message_is_error = true;
    s_message_until_ms = 0;  // Don't auto-dismiss errors

    gui_wake();
}

void gui_dismiss_message(void)
{
    s_message[0] = '\0';
    s_message_until_ms = 0;
    s_message_is_error = false;
}

gui_config_t gui_get_config(void)
{
    return s_config;
}

void gui_set_config(const gui_config_t* config)
{
    if (config != NULL) {
        s_config = *config;
        display_hal_set_brightness(s_config.brightness);
    }
}

void gui_set_theme(const gui_theme_t* theme)
{
    if (theme != NULL) {
        s_theme = *theme;
    }
}

gui_theme_t gui_get_theme(void)
{
    return s_theme;
}

void gui_wake(void)
{
    s_last_interaction_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (s_dimmed) {
        s_dimmed = false;
        display_hal_set_brightness(s_config.brightness);
    }
}

bool gui_is_dimmed(void)
{
    return s_dimmed;
}

void gui_refresh(void)
{
    render_screen();
}
