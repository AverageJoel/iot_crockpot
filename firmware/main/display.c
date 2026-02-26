/**
 * @file display.c
 * @brief Display subsystem: hardware init + LVGL lifecycle
 *
 * Responsibilities:
 *   1. Initialize ST7796 SPI display hardware (display_driver)
 *   2. Initialize FT6336U capacitive touch hardware (touch_driver)
 *   3. Initialize LVGL and register both devices via esp_lvgl_port
 *   4. Show a splash screen while the rest of firmware loads
 *   5. Expose LVGL display/input handles for the GUI layer (Phase 6)
 *
 * The esp_lvgl_port component creates and manages the LVGL task internally.
 * display_task() is a thin loop that only handles optional physical buttons.
 *
 * Thread safety: all LVGL API calls must be wrapped with
 *   lvgl_port_lock(0) / lvgl_port_unlock()
 */

#include "display.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "crockpot.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "display";

// Draw buffer height in lines — set via Kconfig (menuconfig → IoT Crockpot → Test/Development).
// ESP32-S3 default: 50 lines (48KB, DMA SRAM).
// ESP32-C3 default: 20 lines (19KB, fits in 400KB SRAM without PSRAM).
#define LVGL_DRAW_BUF_LINES  CONFIG_CROCKPOT_LCD_DRAW_BUFFER_LINES

// ============================================================================
// State
// ============================================================================

static display_type_t  s_display_type = DISPLAY_TYPE_NONE;
static bool            s_initialized  = false;
static lv_display_t   *s_disp         = NULL;
static lv_indev_t     *s_indev        = NULL;

// Timed message string (shown by gui layer; tracked here for timeout)
static char     s_message[64] = "";
static uint32_t s_message_timeout = 0;

// Optional physical button state (ISR-set flags)
static volatile bool s_button_up_pressed     = false;
static volatile bool s_button_down_pressed   = false;
static volatile bool s_button_select_pressed = false;

// ============================================================================
// Physical buttons (optional hardware — remove if not populated on PCB)
// ============================================================================

// ISR only compiled when at least one button GPIO is enabled
#if !(CONFIG_CROCKPOT_BUTTON_UP_GPIO < 0 && \
      CONFIG_CROCKPOT_BUTTON_DOWN_GPIO < 0 && \
      CONFIG_CROCKPOT_BUTTON_SELECT_GPIO < 0)
static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if      (gpio_num == BUTTON_UP_GPIO)     s_button_up_pressed     = true;
    else if (gpio_num == BUTTON_DOWN_GPIO)   s_button_down_pressed   = true;
    else if (gpio_num == BUTTON_SELECT_GPIO) s_button_select_pressed = true;
}
#endif

static void init_buttons(void)
{
#if CONFIG_CROCKPOT_BUTTON_UP_GPIO < 0 && \
    CONFIG_CROCKPOT_BUTTON_DOWN_GPIO < 0 && \
    CONFIG_CROCKPOT_BUTTON_SELECT_GPIO < 0
    ESP_LOGI(TAG, "Physical buttons disabled (all GPIOs set to -1)");
    return;
#else
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) |
                        (1ULL << BUTTON_DOWN_GPIO) |
                        (1ULL << BUTTON_SELECT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGW(TAG, "Button GPIO config failed (buttons may not be populated)");
        return;
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_UP_GPIO,     button_isr_handler, (void *)BUTTON_UP_GPIO);
    gpio_isr_handler_add(BUTTON_DOWN_GPIO,   button_isr_handler, (void *)BUTTON_DOWN_GPIO);
    gpio_isr_handler_add(BUTTON_SELECT_GPIO, button_isr_handler, (void *)BUTTON_SELECT_GPIO);

    ESP_LOGI(TAG, "Physical buttons initialized");
#endif
}

static void process_buttons(void)
{
    crockpot_status_t status = crockpot_get_status();
    crockpot_state_t new_state = status.state;

    if (s_button_up_pressed) {
        s_button_up_pressed = false;
        switch (status.state) {
            case CROCKPOT_OFF:  new_state = CROCKPOT_WARM; break;
            case CROCKPOT_WARM: new_state = CROCKPOT_LOW;  break;
            case CROCKPOT_LOW:  new_state = CROCKPOT_HIGH; break;
            default: break;
        }
    }

    if (s_button_down_pressed) {
        s_button_down_pressed = false;
        switch (status.state) {
            case CROCKPOT_HIGH: new_state = CROCKPOT_LOW;  break;
            case CROCKPOT_LOW:  new_state = CROCKPOT_WARM; break;
            case CROCKPOT_WARM: new_state = CROCKPOT_OFF;  break;
            default: break;
        }
    }

    if (s_button_select_pressed) {
        s_button_select_pressed = false;
        new_state = (status.state == CROCKPOT_OFF) ? CROCKPOT_LOW : CROCKPOT_OFF;
    }

    if (new_state != status.state) {
        crockpot_set_state(new_state);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool display_init(void)
{
    ESP_LOGI(TAG, "Initializing display subsystem");

    // --- 1. ST7796 SPI display hardware ---
    if (!display_driver_init()) {
        ESP_LOGE(TAG, "Display driver init failed");
        return false;
    }
    s_display_type = DISPLAY_TYPE_TFT_ST7796;

    // --- 2. FT6336U capacitive touch ---
    if (!touch_driver_init()) {
        ESP_LOGW(TAG, "Touch driver init failed — continuing without touch");
    }

    // --- 3. LVGL init ---
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return false;
    }

    // --- 4. Register display with LVGL ---
    // lvgl_port_add_disp() calls lvgl_port_disp_rotation_update() internally,
    // which issues esp_lcd_panel_swap_xy() and esp_lcd_panel_mirror() using the
    // values from disp_cfg.rotation.  Any swap_xy/mirror calls made before this
    // point (e.g. in display_driver_init) are OVERWRITTEN — so this rotation
    // struct is the authoritative orientation control.
    //
    // ST7796 is portrait-native (320 cols × 480 rows). swap_xy=true (MADCTL MV)
    // rotates addressing to landscape (480 cols × 320 rows).
    // mirror_x / mirror_y tune which corner is (0,0) — adjust if image is
    // flipped horizontally or vertically.
    //
    // Note: full_refresh requires buffer_size == hres*vres (153,600 px = 300 KB).
    // That exceeds ESP32-C3 SRAM budget, so we use partial-refresh mode instead.
    // The partial-buffer approach works correctly once landscape mode is active
    // (all 480×320 pixels map cleanly without GRAM-wrap noise).
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = display_driver_get_io(),
        .panel_handle = display_driver_get_panel(),
        .buffer_size  = LCD_H_RES * LVGL_DRAW_BUF_LINES,  // pixels, not bytes
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy  = true,   // landscape: swap rows/cols (MADCTL MV bit)
            .mirror_x = false,  // tune if image is LR-flipped
            .mirror_y = false,  // tune if image is UD-flipped
        },
        .flags = {
            .buff_dma    = true,  // internal DMA-capable SRAM — required for SPI
            .buff_spiram = false, // set true for larger buffer from PSRAM if needed
            // ESP32 is little-endian: LVGL stores RGB565 with the low byte at the
            // lower address. SPI DMA sends low byte first, so the ST7796 receives
            // bytes in the wrong order (R and B channels swap → pink/purple tint).
            // swap_bytes tells esp_lvgl_port to byte-swap each RGB565 word in
            // software before handing it to esp_lcd, restoring correct wire order.
            .swap_bytes  = true,
        },
    };

    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return false;
    }

    // --- 5. Register touch with LVGL ---
    esp_lcd_touch_handle_t tp = touch_driver_get_handle();
    if (tp != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = s_disp,
            .handle = tp,
        };
        s_indev = lvgl_port_add_touch(&touch_cfg);
    }

    // --- 6. Optional physical buttons ---
    init_buttons();

    s_initialized = true;
    ESP_LOGI(TAG, "Display initialized — LVGL running (%dx%d)", LCD_H_RES, LCD_V_RES);
    return true;
}

void display_task(void *pvParameters)
{
    // LVGL processing is handled by esp_lvgl_port's internal task.
    // This task only handles optional physical button inputs.
    // Phase 6 removes this task when the GUI is rewritten with LVGL widgets.
    ESP_LOGI(TAG, "Display task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        process_buttons();

        if (s_message_timeout > 0) {
            if (s_message_timeout <= DISPLAY_UPDATE_INTERVAL_MS) {
                s_message[0]      = '\0';
                s_message_timeout = 0;
            } else {
                s_message_timeout -= DISPLAY_UPDATE_INTERVAL_MS;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS));
    }
}

void display_refresh(void)
{
    // LVGL handles its own refresh cycle via the lvgl_port task.
    // Force a redraw by invalidating the active screen.
    if (s_initialized && lvgl_port_lock(0)) {
        lv_obj_invalidate(lv_scr_act());
        lvgl_port_unlock();
    }
}

void display_show_message(const char *message, uint32_t duration_ms)
{
    if (message == NULL) return;
    strncpy(s_message, message, sizeof(s_message) - 1);
    s_message[sizeof(s_message) - 1] = '\0';
    s_message_timeout = duration_ms;
    ESP_LOGI(TAG, "Message: %s", s_message);
}

void display_clear_message(void)
{
    s_message[0]      = '\0';
    s_message_timeout = 0;
}

void display_set_brightness(uint8_t brightness)
{
    // Simple on/off for now; full PWM brightness can be added later via LEDC.
    display_driver_set_backlight(brightness > 0);
}

display_type_t display_get_type(void)
{
    return s_display_type;
}

lv_display_t *display_get_lvgl_disp(void)
{
    return s_disp;
}

lv_indev_t *display_get_lvgl_indev(void)
{
    return s_indev;
}
