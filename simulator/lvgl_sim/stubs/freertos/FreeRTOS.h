/**
 * @file freertos/FreeRTOS.h
 * @brief FreeRTOS stub for PC simulator.
 *
 * gui.c includes this header but does not use any FreeRTOS types directly —
 * the only FreeRTOS-level calls it makes are through esp_lvgl_port.h
 * (lvgl_port_lock/unlock), which are separately stubbed as no-ops.
 */

#ifndef FREERTOS_STUB_H
#define FREERTOS_STUB_H

/* Nothing needed — gui.c only includes this transitively */

#endif /* FREERTOS_STUB_H */
