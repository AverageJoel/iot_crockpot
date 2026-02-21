/**
 * @file esp_log.h
 * @brief ESP-IDF logging stub for PC simulator.
 *
 * Maps ESP_LOG* macros to fprintf(stderr, ...) so gui.c output is visible
 * in the terminal without pulling in any ESP-IDF headers.
 */

#ifndef ESP_LOG_STUB_H
#define ESP_LOG_STUB_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#endif /* ESP_LOG_STUB_H */
