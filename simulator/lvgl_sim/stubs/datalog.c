/**
 * @file datalog.c
 * @brief Data logging implementation for the PC simulator.
 */

#include "datalog.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// ── Ring buffer ───────────────────────────────────────────────────────────────

static datalog_entry_t s_buf[DATALOG_CAPACITY];
static int             s_head   = 0;   // index of next write slot
static int             s_count  = 0;   // total samples stored (≤ DATALOG_CAPACITY)

// Per-second tick counter; we log every 60 s
static uint32_t s_tick     = 0;
static uint32_t s_start_ts = 0;  // uptime at first tick (set by first call)

void datalog_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head   = 0;
    s_count  = 0;
    s_tick   = 0;
    s_start_ts = 0;
    fprintf(stderr, "[datalog] initialized (capacity %d samples)\n",
            DATALOG_CAPACITY);
}

void datalog_tick(const crockpot_status_t *st)
{
    if (!st) return;

    if (s_start_ts == 0) s_start_ts = st->uptime_seconds;

    s_tick++;
    if (s_tick % 60 != 0) return;   // log every 60 s

    datalog_entry_t *e = &s_buf[s_head];
    e->timestamp_s    = st->uptime_seconds;
    e->temperature_f  = st->temperature_f;
    e->state          = (uint8_t)st->state;
    e->relay_main     = st->relay_main;
    e->relay_aux      = st->relay_aux;
    e->schedule_active = st->schedule_active;
    strncpy(e->schedule_name, st->schedule_name, sizeof(e->schedule_name) - 1);
    e->schedule_name[sizeof(e->schedule_name) - 1] = '\0';
    e->schedule_step  = st->schedule_step;

    s_head = (s_head + 1) % DATALOG_CAPACITY;
    if (s_count < DATALOG_CAPACITY) s_count++;
}

int datalog_count(void)
{
    return s_count;
}

int datalog_get_last(datalog_entry_t *buf, int n)
{
    if (!buf || n <= 0) return 0;
    if (n > s_count) n = s_count;

    // Oldest entry in the ring buffer:
    int oldest = (s_count < DATALOG_CAPACITY)
                 ? 0
                 : s_head;  // head points to the slot about to be overwritten

    int start = (oldest + (s_count - n) + DATALOG_CAPACITY) % DATALOG_CAPACITY;
    for (int i = 0; i < n; i++) {
        buf[i] = s_buf[(start + i) % DATALOG_CAPACITY];
    }
    return n;
}

bool datalog_export_csv(const char *path)
{
    if (!path) return false;
    if (s_count == 0) {
        fprintf(stderr, "[datalog] no data to export\n");
        return false;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[datalog] failed to open '%s' for writing\n", path);
        return false;
    }

    // Header
    fprintf(f,
            "uptime_s,temperature_f,state,relay_main,relay_aux,"
            "schedule_active,schedule_name,schedule_step\n");

    // Walk the ring in chronological order
    int oldest = (s_count < DATALOG_CAPACITY) ? 0 : s_head;
    for (int i = 0; i < s_count; i++) {
        const datalog_entry_t *e =
            &s_buf[(oldest + i) % DATALOG_CAPACITY];
        fprintf(f, "%lu,%.1f,%u,%d,%d,%d,\"%s\",%u\n",
                (unsigned long)e->timestamp_s,
                e->temperature_f,
                e->state,
                e->relay_main ? 1 : 0,
                e->relay_aux  ? 1 : 0,
                e->schedule_active ? 1 : 0,
                e->schedule_name,
                e->schedule_step);
    }

    fclose(f);
    fprintf(stderr, "[datalog] exported %d samples to '%s'\n", s_count, path);
    return true;
}
