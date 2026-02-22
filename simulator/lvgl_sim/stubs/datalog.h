/**
 * @file datalog.h
 * @brief Data logging for the PC simulator.
 *
 * Maintains a 24-hour ring buffer (1440 entries at 60 s intervals) and
 * can export to a timestamped CSV file.
 */

#ifndef DATALOG_H
#define DATALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "crockpot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One logged sample. */
typedef struct {
    uint32_t timestamp_s;
    float    temperature_f;
    uint8_t  state;            // crockpot_state_t value
    bool     relay_main;
    bool     relay_aux;
    bool     schedule_active;
    char     schedule_name[32];
    uint8_t  schedule_step;
} datalog_entry_t;

/** Initialize the ring buffer. Call once at startup. */
void datalog_init(void);

/**
 * @brief Per-second tick.
 *
 * Call this every second from the simulator's 1 s LVGL timer.
 * Logs a sample every 60 seconds.
 *
 * @param st  Current crockpot status snapshot
 */
void datalog_tick(const crockpot_status_t *st);

/**
 * @brief Export all logged samples to a CSV file.
 *
 * Writes to the given path; creates the file if it does not exist.
 * Returns false on error.
 *
 * @param path  File path (e.g., "crockpot_log.csv")
 */
bool datalog_export_csv(const char *path);

/** Number of samples currently in the buffer (0 – 1440). */
int datalog_count(void);

/**
 * @brief Retrieve the last N samples (up to DATALOG_CAPACITY) into buf.
 *
 * Returns the number of samples actually written.
 */
int datalog_get_last(datalog_entry_t *buf, int n);

/** Ring buffer capacity: 24 h × 60 samples/h = 1440 samples. */
#define DATALOG_CAPACITY 1440

#ifdef __cplusplus
}
#endif

#endif /* DATALOG_H */
