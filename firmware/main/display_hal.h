/**
 * @file display_hal.h
 * @brief Display Hardware Abstraction Layer
 *
 * Abstract interface for display rendering. Allows GUI code to be
 * developed independently of the actual display hardware.
 *
 * Implementations:
 * - display_hal_none.c   (stub, logs only)
 * - display_hal_ili9341.c (TFT via SPI)
 * - display_hal_ssd1306.c (OLED via I2C)
 */

#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGB565 color type (16-bit color)
 */
typedef uint16_t color_t;

/**
 * @brief Common colors (RGB565 format)
 */
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_ORANGE      0xFD20
#define COLOR_GRAY        0x8410
#define COLOR_DARK_GRAY   0x4208

/**
 * @brief Convert RGB888 to RGB565
 */
#define COLOR_RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

/**
 * @brief Display capabilities structure
 */
typedef struct {
    uint16_t width;           // Display width in pixels
    uint16_t height;          // Display height in pixels
    uint8_t  bits_per_pixel;  // Color depth (1=mono, 16=RGB565)
    bool     touch_capable;   // Has touch input
    bool     initialized;     // Successfully initialized
} display_info_t;

/**
 * @brief Text alignment options
 */
typedef enum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
} text_align_t;

/**
 * @brief Font size options
 */
typedef enum {
    FONT_SMALL,    // ~8px height
    FONT_MEDIUM,   // ~12px height
    FONT_LARGE,    // ~16px height
    FONT_XLARGE    // ~24px height
} font_size_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize the display hardware
 *
 * @return true on success, false if no display found
 */
bool display_hal_init(void);

/**
 * @brief Get display information
 *
 * @return Display capabilities structure
 */
display_info_t display_hal_get_info(void);

// ============================================================================
// Drawing Primitives
// ============================================================================

/**
 * @brief Clear entire screen with color
 *
 * @param color Fill color
 */
void display_hal_clear(color_t color);

/**
 * @brief Draw a single pixel
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color
 */
void display_hal_pixel(int16_t x, int16_t y, color_t color);

/**
 * @brief Draw a horizontal line
 *
 * @param x Start X
 * @param y Y coordinate
 * @param w Width
 * @param color Line color
 */
void display_hal_hline(int16_t x, int16_t y, int16_t w, color_t color);

/**
 * @brief Draw a vertical line
 *
 * @param x X coordinate
 * @param y Start Y
 * @param h Height
 * @param color Line color
 */
void display_hal_vline(int16_t x, int16_t y, int16_t h, color_t color);

/**
 * @brief Draw a line between two points
 *
 * @param x0 Start X
 * @param y0 Start Y
 * @param x1 End X
 * @param y1 End Y
 * @param color Line color
 */
void display_hal_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color);

/**
 * @brief Draw rectangle outline
 *
 * @param x Top-left X
 * @param y Top-left Y
 * @param w Width
 * @param h Height
 * @param color Line color
 */
void display_hal_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color);

/**
 * @brief Draw filled rectangle
 *
 * @param x Top-left X
 * @param y Top-left Y
 * @param w Width
 * @param h Height
 * @param color Fill color
 */
void display_hal_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color);

/**
 * @brief Draw rounded rectangle outline
 *
 * @param x Top-left X
 * @param y Top-left Y
 * @param w Width
 * @param h Height
 * @param r Corner radius
 * @param color Line color
 */
void display_hal_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, color_t color);

/**
 * @brief Draw filled rounded rectangle
 *
 * @param x Top-left X
 * @param y Top-left Y
 * @param w Width
 * @param h Height
 * @param r Corner radius
 * @param color Fill color
 */
void display_hal_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, color_t color);

/**
 * @brief Draw circle outline
 *
 * @param x Center X
 * @param y Center Y
 * @param r Radius
 * @param color Line color
 */
void display_hal_circle(int16_t x, int16_t y, int16_t r, color_t color);

/**
 * @brief Draw filled circle
 *
 * @param x Center X
 * @param y Center Y
 * @param r Radius
 * @param color Fill color
 */
void display_hal_fill_circle(int16_t x, int16_t y, int16_t r, color_t color);

// ============================================================================
// Text Rendering
// ============================================================================

/**
 * @brief Draw text string
 *
 * @param x X coordinate (meaning depends on alignment)
 * @param y Y coordinate (top of text)
 * @param text Null-terminated string
 * @param font Font size
 * @param color Text color
 * @param align Text alignment relative to x
 */
void display_hal_text(int16_t x, int16_t y, const char* text,
                      font_size_t font, color_t color, text_align_t align);

/**
 * @brief Get text width in pixels
 *
 * @param text Null-terminated string
 * @param font Font size
 * @return Width in pixels
 */
int16_t display_hal_text_width(const char* text, font_size_t font);

/**
 * @brief Get font height in pixels
 *
 * @param font Font size
 * @return Height in pixels
 */
int16_t display_hal_font_height(font_size_t font);

// ============================================================================
// Display Control
// ============================================================================

/**
 * @brief Set display brightness
 *
 * @param brightness 0-100 percentage
 */
void display_hal_set_brightness(uint8_t brightness);

/**
 * @brief Flush framebuffer to display
 *
 * For displays with double-buffering, this copies the
 * back buffer to the display. For immediate-mode displays,
 * this may be a no-op.
 */
void display_hal_flush(void);

/**
 * @brief Set display rotation
 *
 * @param rotation 0, 90, 180, or 270 degrees
 */
void display_hal_set_rotation(uint16_t rotation);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_HAL_H
