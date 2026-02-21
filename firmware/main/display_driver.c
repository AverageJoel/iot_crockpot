/**
 * @file display_driver.c
 * @brief ST7796 SPI display driver implementation
 *
 * Orientation notes (may need tuning per physical module):
 *   swap_xy=true + mirror_x=true gives landscape with USB-C at the bottom.
 *   If the image appears rotated or mirrored, adjust the swap/mirror calls
 *   in display_driver_init() and update LVGL's rotation setting to match.
 *
 * Color inversion:
 *   ST7796 modules from lcdwiki/Hosyond/Waveshare typically require
 *   invert_color=true for correct colors. Disable if colors appear inverted.
 */

#include "display_driver.h"
#include "spi_bus.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display_driver";

static esp_lcd_panel_handle_t    s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle    = NULL;
static bool                      s_initialized  = false;

bool display_driver_init(void)
{
    if (s_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing ST7796 display (CS=%d DC=%d RST=%d BL=%d)",
             CONFIG_CROCKPOT_LCD_CS, CONFIG_CROCKPOT_LCD_DC,
             CONFIG_CROCKPOT_LCD_RST, CONFIG_CROCKPOT_LCD_BL);

    // --- Backlight GPIO ---
    if (CONFIG_CROCKPOT_LCD_BL >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = (1ULL << CONFIG_CROCKPOT_LCD_BL),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&bl_cfg);
        gpio_set_level(CONFIG_CROCKPOT_LCD_BL, 0);  // off during init
    }

    // --- Shared SPI bus ---
    if (!spi_bus_init()) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return false;
    }

    // --- esp_lcd SPI panel IO ---
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = CONFIG_CROCKPOT_LCD_DC,
        .cs_gpio_num       = CONFIG_CROCKPOT_LCD_CS,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
        // on_color_trans_done and user_ctx are set by LVGL in Phase 5
        .on_color_trans_done = NULL,
        .user_ctx            = NULL,
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SHARED_SPI_HOST, &io_config, &s_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(ret));
        return false;
    }

    // --- ST7796 panel ---
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_CROCKPOT_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7796(s_io_handle, &panel_config, &s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7796 failed: %s", esp_err_to_name(ret));
        return false;
    }

    // --- Panel init sequence ---
    esp_lcd_panel_reset(s_panel_handle);
    esp_lcd_panel_init(s_panel_handle);

    // ST7796 modules typically need color inversion for correct colors.
    // Disable this if your display shows inverted colors.
    esp_lcd_panel_invert_color(s_panel_handle, true);

    // Landscape orientation: 480 wide x 320 tall.
    // Adjust swap_xy / mirror_x / mirror_y if image is rotated or mirrored.
    esp_lcd_panel_swap_xy(s_panel_handle, true);
    esp_lcd_panel_mirror(s_panel_handle, true, false);

    esp_lcd_panel_set_gap(s_panel_handle, 0, 0);

    // Turn on the display
    esp_lcd_panel_disp_on_off(s_panel_handle, true);

    // Backlight on
    display_driver_set_backlight(true);

    s_initialized = true;
    ESP_LOGI(TAG, "ST7796 display initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
    return true;
}

esp_lcd_panel_handle_t display_driver_get_panel(void)
{
    return s_panel_handle;
}

esp_lcd_panel_io_handle_t display_driver_get_io(void)
{
    return s_io_handle;
}

void display_driver_set_backlight(bool on)
{
    if (CONFIG_CROCKPOT_LCD_BL >= 0) {
        gpio_set_level(CONFIG_CROCKPOT_LCD_BL, on ? 1 : 0);
    }
}

void display_driver_fill(uint16_t color_rgb565)
{
    if (!s_initialized) {
        return;
    }

    // Allocate a one-line DMA-capable buffer
    uint16_t *line = heap_caps_malloc(
        LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (line == NULL) {
        ESP_LOGE(TAG, "display_driver_fill: out of memory");
        return;
    }

    for (int i = 0; i < LCD_H_RES; i++) {
        line[i] = color_rgb565;
    }

    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, LCD_H_RES, y + 1, line);
    }

    heap_caps_free(line);
}
