/**
 * @file wifi.c
 * @brief WiFi mock implementation for PC simulator.
 */

#include "wifi.h"
#include <string.h>
#include <stdio.h>

static bool s_connected = true;

bool wifi_is_connected(void)
{
    return s_connected;
}

bool wifi_get_ip_string(char *buf, size_t buf_len)
{
    if (!s_connected || buf == NULL || buf_len == 0) {
        if (buf && buf_len > 0) buf[0] = '\0';
        return false;
    }
    strncpy(buf, "192.168.1.100", buf_len - 1);
    buf[buf_len - 1] = '\0';
    return true;
}

void sim_wifi_toggle_connected(void)
{
    s_connected = !s_connected;
    fprintf(stderr, "[sim] WiFi: %s\n", s_connected ? "connected" : "disconnected");
}
