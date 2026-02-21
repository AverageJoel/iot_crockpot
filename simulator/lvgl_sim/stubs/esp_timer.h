/**
 * @file esp_timer.h
 * @brief esp_timer stub for PC simulator.
 *
 * gui.c uses esp_timer_get_time() for backlight idle tracking.
 * The stub returns SDL_GetTicks() scaled to microseconds.
 */

#ifndef ESP_TIMER_STUB_H
#define ESP_TIMER_STUB_H

#include <stdint.h>

/**
 * @brief Return monotonic time in microseconds (like esp_timer_get_time).
 * Implemented in stubs/esp_timer.c using SDL_GetTicks().
 */
int64_t esp_timer_get_time(void);

#endif /* ESP_TIMER_STUB_H */
