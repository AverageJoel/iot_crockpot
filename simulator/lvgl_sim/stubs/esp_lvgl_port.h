/**
 * @file esp_lvgl_port.h
 * @brief esp_lvgl_port stub for PC simulator.
 *
 * The PC simulator is single-threaded — there is no separate LVGL task.
 * lv_timer_handler() is called from the main loop, so lock/unlock are no-ops.
 * Both functions are inlined to avoid any function-call overhead.
 */

#ifndef ESP_LVGL_PORT_STUB_H
#define ESP_LVGL_PORT_STUB_H

#include <stdbool.h>
#include <stdint.h>

static inline bool lvgl_port_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}

static inline void lvgl_port_unlock(void)
{
    /* no-op */
}

#endif /* ESP_LVGL_PORT_STUB_H */
