/**
 * @file wifi.h
 * @brief WiFi stub for PC simulator.
 *
 * The real wifi.h pulls in esp_event.h. gui.c only uses wifi_is_connected()
 * and wifi_get_ip_string(), so this stub provides exactly those.
 */

#ifndef WIFI_STUB_H
#define WIFI_STUB_H

#include <stdbool.h>
#include <stddef.h>

bool wifi_is_connected(void);
bool wifi_get_ip_string(char *buf, size_t buf_len);

/* Simulator control — toggle from keyboard */
void sim_wifi_toggle_connected(void);

#endif /* WIFI_STUB_H */
