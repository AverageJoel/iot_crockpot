/**
 * @file crockpot.c
 * @brief Simulator crockpot implementation: thermal physics, relay mapping,
 *        safety shutoff, and full schedule engine.
 *
 * This file replaces firmware/main/crockpot.c in the simulator build.
 * It implements every symbol in crockpot.h plus the sim-only helpers declared
 * in sim_controls.h.
 */

#include "crockpot.h"
#include "wifi.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

// ── Simulator state ──────────────────────────────────────────────────────────

static crockpot_state_t    s_state          = CROCKPOT_OFF;
static float               s_temperature    = 72.0f;   // °F, room temperature
static bool                s_sensor_error   = false;
static int                 s_err_count      = 0;
static crockpot_alert_cb_t s_alert_cb       = NULL;

// Target temperatures for each heat state
static const float k_target_temp[4] = {
    [CROCKPOT_OFF]  = 72.0f,
    [CROCKPOT_WARM] = 165.0f,
    [CROCKPOT_LOW]  = 195.0f,
    [CROCKPOT_HIGH] = 250.0f,
};

// ── Schedule state ────────────────────────────────────────────────────────────

static const crockpot_schedule_t *s_schedule           = NULL;
static uint8_t                    s_sched_step          = 0;
static uint32_t                   s_sched_elapsed_s     = 0;

// ── Built-in schedule presets ─────────────────────────────────────────────────

static const crockpot_schedule_step_t k_slow_cook_steps[] = {
    { CROCKPOT_HIGH, 3600      },
    { CROCKPOT_LOW,  6 * 3600  },
    { CROCKPOT_WARM, 0         },
};
const crockpot_schedule_t CROCKPOT_SCHED_SLOW_COOK = {
    .name      = "Slow Cook",
    .steps     = k_slow_cook_steps,
    .num_steps = 3,
    .repeat    = false,
};

static const crockpot_schedule_step_t k_quick_warm_steps[] = {
    { CROCKPOT_HIGH, 30 * 60 },
    { CROCKPOT_WARM, 0       },
};
const crockpot_schedule_t CROCKPOT_SCHED_QUICK_WARM = {
    .name      = "Quick Warm",
    .steps     = k_quick_warm_steps,
    .num_steps = 2,
    .repeat    = false,
};

static const crockpot_schedule_step_t k_all_day_steps[] = {
    { CROCKPOT_LOW,  8 * 3600 },
    { CROCKPOT_WARM, 0        },
};
const crockpot_schedule_t CROCKPOT_SCHED_ALL_DAY = {
    .name      = "All Day",
    .steps     = k_all_day_steps,
    .num_steps = 2,
    .repeat    = false,
};

// ── crockpot.h API ────────────────────────────────────────────────────────────

bool crockpot_init(void)
{
    srand(42);
    fprintf(stderr, "[sim] crockpot init\n");
    return true;
}

crockpot_status_t crockpot_get_status(void)
{
    crockpot_status_t st;
    memset(&st, 0, sizeof(st));

    st.state          = s_state;
    st.temperature_f  = s_temperature;
    st.uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000LL);
    st.wifi_connected = wifi_is_connected();
    st.sensor_error   = s_sensor_error;

    // Relay mapping
    st.relay_main = (s_state == CROCKPOT_LOW  || s_state == CROCKPOT_HIGH);
    st.relay_aux  = (s_state == CROCKPOT_WARM || s_state == CROCKPOT_HIGH);

    // Schedule fields
    st.schedule_active      = (s_schedule != NULL);
    st.schedule_step        = s_sched_step;
    st.schedule_total_steps = s_schedule ? s_schedule->num_steps : 0;

    if (s_schedule) {
        strncpy(st.schedule_name, s_schedule->name,
                sizeof(st.schedule_name) - 1);
        st.schedule_name[sizeof(st.schedule_name) - 1] = '\0';

        const crockpot_schedule_step_t *step =
            &s_schedule->steps[s_sched_step];
        if (step->duration_s > 0) {
            uint32_t elapsed = s_sched_elapsed_s;
            st.schedule_step_remaining_s =
                (elapsed < step->duration_s)
                ? (step->duration_s - elapsed) : 0;
            st.schedule_step_progress =
                (float)elapsed / (float)step->duration_s;
        } else {
            st.schedule_step_remaining_s = 0;
            st.schedule_step_progress    = 0.0f;
        }
    }

    return st;
}

bool crockpot_set_state(crockpot_state_t state)
{
    fprintf(stderr, "[sim] crockpot → %s\n", crockpot_state_to_string(state));
    s_state = state;
    return true;
}

const char *crockpot_state_to_string(crockpot_state_t state)
{
    switch (state) {
        case CROCKPOT_OFF:  return "OFF";
        case CROCKPOT_WARM: return "WARM";
        case CROCKPOT_LOW:  return "LOW";
        case CROCKPOT_HIGH: return "HIGH";
        default:            return "UNKNOWN";
    }
}

bool crockpot_state_from_string(const char *str, crockpot_state_t *out)
{
    if (!str || !out) return false;
    if (strcasecmp(str, "off")  == 0) { *out = CROCKPOT_OFF;  return true; }
    if (strcasecmp(str, "warm") == 0) { *out = CROCKPOT_WARM; return true; }
    if (strcasecmp(str, "low")  == 0) { *out = CROCKPOT_LOW;  return true; }
    if (strcasecmp(str, "high") == 0) { *out = CROCKPOT_HIGH; return true; }
    return false;
}

void crockpot_control_task(void *pvParameters)
{
    (void)pvParameters;
    /* Not used in simulator — driven by crockpot_tick_s() LVGL timer */
}

void crockpot_set_alert_callback(crockpot_alert_cb_t cb)
{
    s_alert_cb = cb;
}

void crockpot_set_temp_source(crockpot_temp_source_cb_t cb, void *ctx)
{
    /* Not used in simulator — physics driven internally */
    (void)cb;
    (void)ctx;
}

// ── Schedule API ──────────────────────────────────────────────────────────────

void crockpot_schedule_start(const crockpot_schedule_t *sched)
{
    if (!sched || sched->num_steps == 0) return;
    s_schedule   = sched;
    s_sched_step = 0;
    s_sched_elapsed_s = 0;
    crockpot_set_state(sched->steps[0].state);
    fprintf(stderr, "[sim] schedule '%s' started\n", sched->name);
}

void crockpot_schedule_stop(void)
{
    s_schedule = NULL;
    fprintf(stderr, "[sim] schedule stopped\n");
}

bool crockpot_schedule_is_active(void)
{
    return s_schedule != NULL;
}

/**
 * @brief Per-second tick: thermal physics + safety + schedule advance.
 *
 * Called from a 1 s LVGL timer in main.c.
 *
 * Thermal physics:
 *   Heating: s_temperature approaches target at up to +2°F/s (linear).
 *   Cooling: exponential decay toward 70°F ambient (diff × 0.02/s).
 *   Noise:   ±0.3°F uniform random per tick.
 */
void crockpot_tick_s(void)
{
    // ── Thermal physics ──────────────────────────────────────────────────────
    float target = k_target_temp[s_state];
    float diff   = target - s_temperature;

    if (s_temperature < target) {
        // Heating: up to +2°F per second
        float delta = diff < 2.0f ? diff : 2.0f;
        s_temperature += delta;
    } else {
        // Cooling toward target (overshoot) or ambient (OFF)
        float cool_target = (s_state == CROCKPOT_OFF) ? 70.0f : target;
        s_temperature += (cool_target - s_temperature) * 0.02f;
    }

    // Noise: ±0.3°F
    s_temperature += ((float)(rand() % 601) - 300.0f) * 0.001f;

    // ── Safety checks ─────────────────────────────────────────────────────────
    if (s_state != CROCKPOT_OFF &&
        s_temperature > CROCKPOT_SAFETY_TEMP_F) {
        fprintf(stderr, "[sim] SAFETY: temp %.1f°F exceeded limit, shutting off\n",
                s_temperature);
        float last_temp = s_temperature;
        s_state    = CROCKPOT_OFF;
        s_schedule = NULL;
        if (s_alert_cb) s_alert_cb(CROCKPOT_ALERT_TEMP_LIMIT, last_temp);
    }

    if (s_state != CROCKPOT_OFF && s_sensor_error) {
        s_err_count++;
        if (s_err_count > 10) {
            fprintf(stderr, "[sim] SAFETY: persistent sensor error, shutting off\n");
            s_state     = CROCKPOT_OFF;
            s_schedule  = NULL;
            s_err_count = 0;
            if (s_alert_cb) s_alert_cb(CROCKPOT_ALERT_SENSOR_ERROR, s_temperature);
        }
    } else {
        s_err_count = 0;
    }

    // ── Schedule advance ──────────────────────────────────────────────────────
    if (!s_schedule) return;

    s_sched_elapsed_s++;

    const crockpot_schedule_step_t *step = &s_schedule->steps[s_sched_step];

    // Indefinite step — never advance
    if (step->duration_s == 0) return;

    if (s_sched_elapsed_s >= step->duration_s) {
        s_sched_step++;
        s_sched_elapsed_s = 0;

        if (s_sched_step >= s_schedule->num_steps) {
            if (s_schedule->repeat) {
                s_sched_step = 0;
                crockpot_set_state(s_schedule->steps[0].state);
            } else {
                fprintf(stderr, "[sim] schedule '%s' completed\n",
                        s_schedule->name);
                s_schedule = NULL;
            }
        } else {
            fprintf(stderr, "[sim] schedule step → %d (%s)\n",
                    s_sched_step,
                    crockpot_state_to_string(s_schedule->steps[s_sched_step].state));
            crockpot_set_state(s_schedule->steps[s_sched_step].state);
        }
    }
}

// ── Simulator control helpers (declared in sim_controls.h) ──────────────────

void sim_crockpot_set_temperature(float temp_f)
{
    s_temperature = temp_f;
    fprintf(stderr, "[sim] temperature set to %.1f°F\n", temp_f);
}

float sim_crockpot_get_temperature(void)
{
    return s_temperature;
}

void sim_crockpot_toggle_sensor_error(void)
{
    s_sensor_error = !s_sensor_error;
    if (!s_sensor_error) s_err_count = 0;
    fprintf(stderr, "[sim] sensor error: %s\n", s_sensor_error ? "ON" : "OFF");
}
