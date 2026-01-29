/**
 * @file display_hal.c
 * @brief Display HAL stub implementation
 *
 * Stub that logs drawing calls. Replace with actual driver implementation
 * when display hardware is selected.
 *
 * To implement for a specific display:
 * 1. Copy this file to display_hal_<driver>.c
 * 2. Implement actual hardware communication
 * 3. Update CMakeLists.txt to use your implementation
 */

#include "display_hal.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "display_hal";

// Stub display info
static display_info_t s_info = {
    .width = 320,
    .height = 240,
    .bits_per_pixel = 16,
    .touch_capable = true,
    .initialized = false
};

// Font heights for size calculations
static const int16_t s_font_heights[] = {
    [FONT_SMALL]  = 8,
    [FONT_MEDIUM] = 12,
    [FONT_LARGE]  = 16,
    [FONT_XLARGE] = 24
};

// Average character widths (approximate)
static const int16_t s_char_widths[] = {
    [FONT_SMALL]  = 6,
    [FONT_MEDIUM] = 7,
    [FONT_LARGE]  = 10,
    [FONT_XLARGE] = 14
};

bool display_hal_init(void)
{
    ESP_LOGI(TAG, "Display HAL initializing (STUB)");
    ESP_LOGI(TAG, "Configured for %dx%d, %d bpp",
             s_info.width, s_info.height, s_info.bits_per_pixel);

    // TODO: Initialize actual display hardware here
    // - Configure SPI/I2C bus
    // - Reset display
    // - Send initialization sequence
    // - Set rotation

    s_info.initialized = true;
    ESP_LOGI(TAG, "Display HAL initialized (STUB - implement real driver)");
    return true;
}

display_info_t display_hal_get_info(void)
{
    return s_info;
}

void display_hal_clear(color_t color)
{
    ESP_LOGD(TAG, "clear(0x%04X)", color);
    // TODO: Fill entire screen with color
}

void display_hal_pixel(int16_t x, int16_t y, color_t color)
{
    // Too verbose to log every pixel
    (void)x; (void)y; (void)color;
    // TODO: Set single pixel
}

void display_hal_hline(int16_t x, int16_t y, int16_t w, color_t color)
{
    ESP_LOGV(TAG, "hline(%d,%d,%d,0x%04X)", x, y, w, color);
    // TODO: Draw horizontal line
}

void display_hal_vline(int16_t x, int16_t y, int16_t h, color_t color)
{
    ESP_LOGV(TAG, "vline(%d,%d,%d,0x%04X)", x, y, h, color);
    // TODO: Draw vertical line
}

void display_hal_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color)
{
    ESP_LOGV(TAG, "line(%d,%d,%d,%d,0x%04X)", x0, y0, x1, y1, color);
    // TODO: Implement Bresenham's line algorithm
}

void display_hal_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color)
{
    ESP_LOGV(TAG, "rect(%d,%d,%d,%d,0x%04X)", x, y, w, h, color);
    // TODO: Draw rectangle outline using hline/vline
    display_hal_hline(x, y, w, color);
    display_hal_hline(x, y + h - 1, w, color);
    display_hal_vline(x, y, h, color);
    display_hal_vline(x + w - 1, y, h, color);
}

void display_hal_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color)
{
    ESP_LOGV(TAG, "fill_rect(%d,%d,%d,%d,0x%04X)", x, y, w, h, color);
    // TODO: Fill rectangle (use display's block fill if available)
}

void display_hal_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, color_t color)
{
    ESP_LOGV(TAG, "round_rect(%d,%d,%d,%d,r=%d,0x%04X)", x, y, w, h, r, color);
    // TODO: Draw rounded rectangle
}

void display_hal_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, color_t color)
{
    ESP_LOGV(TAG, "fill_round_rect(%d,%d,%d,%d,r=%d,0x%04X)", x, y, w, h, r, color);
    // TODO: Fill rounded rectangle
}

void display_hal_circle(int16_t x, int16_t y, int16_t r, color_t color)
{
    ESP_LOGV(TAG, "circle(%d,%d,r=%d,0x%04X)", x, y, r, color);
    // TODO: Draw circle using midpoint algorithm
}

void display_hal_fill_circle(int16_t x, int16_t y, int16_t r, color_t color)
{
    ESP_LOGV(TAG, "fill_circle(%d,%d,r=%d,0x%04X)", x, y, r, color);
    // TODO: Fill circle
}

void display_hal_text(int16_t x, int16_t y, const char* text,
                      font_size_t font, color_t color, text_align_t align)
{
    if (text == NULL) return;

    ESP_LOGD(TAG, "text(%d,%d,\"%s\",font=%d,0x%04X,align=%d)",
             x, y, text, font, color, align);

    // TODO: Render text using font
    // - Calculate position based on alignment
    // - Render each character glyph
}

int16_t display_hal_text_width(const char* text, font_size_t font)
{
    if (text == NULL) return 0;

    int16_t char_width = (font < sizeof(s_char_widths)/sizeof(s_char_widths[0]))
                         ? s_char_widths[font] : 6;

    return strlen(text) * char_width;
}

int16_t display_hal_font_height(font_size_t font)
{
    if (font < sizeof(s_font_heights)/sizeof(s_font_heights[0])) {
        return s_font_heights[font];
    }
    return 8;
}

void display_hal_set_brightness(uint8_t brightness)
{
    ESP_LOGI(TAG, "set_brightness(%d%%)", brightness);
    // TODO: Control backlight PWM
}

void display_hal_flush(void)
{
    ESP_LOGV(TAG, "flush()");
    // TODO: For double-buffered displays, swap buffers
    // For immediate-mode displays, this may be a no-op
}

void display_hal_set_rotation(uint16_t rotation)
{
    ESP_LOGI(TAG, "set_rotation(%d)", rotation);
    // TODO: Update display rotation and swap width/height if needed
}
