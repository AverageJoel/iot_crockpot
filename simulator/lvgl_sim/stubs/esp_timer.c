/**
 * @file esp_timer.c
 * @brief esp_timer_get_time() implementation for PC simulator.
 */

#include "esp_timer.h"
#include <SDL2/SDL.h>

int64_t esp_timer_get_time(void)
{
    /* SDL_GetTicks() returns milliseconds; scale to microseconds */
    return (int64_t)SDL_GetTicks() * 1000LL;
}
