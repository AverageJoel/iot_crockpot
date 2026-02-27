/**
 * @file touch_driver.c
 * @brief FT6336U capacitive touch driver implementation
 *
 * I2C address: 0x38 (fixed, FT5x06 family)
 * I2C port:    I2C_NUM_0
 * Bus speed:   400 kHz
 *
 * Orientation notes:
 *   swap_xy and mirror settings must match the display driver orientation.
 *   If touch points are misaligned with the display, adjust the flags
 *   in touch_driver_init() to match.
 */

#include "touch_driver.h"
#include "display_driver.h"   // for LCD_H_RES / LCD_V_RES

#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "touch_driver";

#define TOUCH_I2C_PORT   I2C_NUM_0
#define TOUCH_I2C_FREQ   400000   // 400 kHz

static esp_lcd_touch_handle_t s_tp_handle  = NULL;
static bool                   s_initialized = false;

bool touch_driver_init(void)
{
    if (s_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing FT6336U touch (SDA=%d SCL=%d RST=%d INT=%d)",
             CONFIG_CROCKPOT_TOUCH_SDA, CONFIG_CROCKPOT_TOUCH_SCL,
             CONFIG_CROCKPOT_TOUCH_RST, CONFIG_CROCKPOT_TOUCH_INT);

    // --- CTP_RST: pulse low to reset, then release ---
    // Guard with #if so the compiler doesn't evaluate (1ULL << -1) when RST=-1.
#if CONFIG_CROCKPOT_TOUCH_RST >= 0
    {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = (1ULL << CONFIG_CROCKPOT_TOUCH_RST),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_cfg);
        gpio_set_level(CONFIG_CROCKPOT_TOUCH_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(CONFIG_CROCKPOT_TOUCH_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif

    // --- I2C bus (legacy driver API) ---
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = CONFIG_CROCKPOT_TOUCH_SDA,
        .scl_io_num       = CONFIG_CROCKPOT_TOUCH_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ,
    };

    esp_err_t ret = i2c_param_config(TOUCH_I2C_PORT, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return false;
    }

    // --- esp_lcd I2C panel IO for touch ---
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();

    ret = esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_I2C_PORT, &tp_io_cfg, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(ret));
        return false;
    }

    // --- FT6336U touch handle ---
    // The FT6336U reports raw coordinates in portrait orientation (x=0..319,
    // y=0..479).  x_max/y_max are used ONLY for mirror arithmetic (mirror_x
    // computes x = x_max - x), so they must match the pre-swap portrait space,
    // not the landscape display dimensions.  Using landscape values (480/320)
    // here shifts the mirror centre off-screen and produces a dead zone.
    //
    // Transform chain (applied in order by esp_lcd_touch base):
    //   1. mirror_x  → x = x_max - x  (x_max = 320)
    //   2. mirror_y  → y = y_max - y  (y_max = 480)
    //   3. swap_xy   → swap x and y  → landscape output (x=0..479, y=0..319)
    //
    // Verified on this module: mirror_x=1, mirror_y=0, swap_xy=1.
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_V_RES,   // portrait x range: 320
        .y_max        = LCD_H_RES,   // portrait y range: 480
        .rst_gpio_num = CONFIG_CROCKPOT_TOUCH_RST,
        .int_gpio_num = CONFIG_CROCKPOT_TOUCH_INT,
        .levels = {
            .reset     = 0,  // active-low reset
            .interrupt = 0,  // active-low interrupt
        },
        .flags = {
            .swap_xy  = 1,  // portrait→landscape
            .mirror_x = 1,  // IC X axis is horizontally inverted on this module
            .mirror_y = 0,
        },
    };

    ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &s_tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_ft5x06 failed: %s", esp_err_to_name(ret));
        return false;
    }

    // FT6336U register 0x86 (CTRL): 0 = keep-active (always scanning),
    //                                1 = auto-monitor (default, enters low-power
    //                                    scan mode between touches — causes the
    //                                    first tap after idle to be missed).
    {
        uint8_t ctrl[] = {0x86, 0x00};
        esp_err_t rc = i2c_master_write_to_device(TOUCH_I2C_PORT, 0x38,
                                                   ctrl, sizeof(ctrl),
                                                   pdMS_TO_TICKS(100));
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "FT6336U keep-active write failed: %s", esp_err_to_name(rc));
        } else {
            ESP_LOGI(TAG, "FT6336U set to keep-active mode");
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "FT6336U touch initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
    return true;
}

esp_lcd_touch_handle_t touch_driver_get_handle(void)
{
    return s_tp_handle;
}

bool touch_driver_read(touch_point_t *point)
{
    if (!s_initialized || point == NULL) {
        if (point) {
            point->pressed = false;
            point->x = 0;
            point->y = 0;
        }
        return false;
    }

    // Trigger a read from the IC
    esp_lcd_touch_read_data(s_tp_handle);

    uint16_t x[1], y[1], strength[1];
    uint8_t  cnt = 0;
    esp_lcd_touch_get_coordinates(s_tp_handle, x, y, strength, &cnt, 1);

    point->pressed = (cnt > 0);
    point->x       = point->pressed ? x[0] : 0;
    point->y       = point->pressed ? y[0] : 0;

    return point->pressed;
}
