/**
 * @file crockpot.c
 * @brief Mock crockpot state machine for PC simulator.
 *
 * Implements the full crockpot.h API with simulated state and temperature.
 * Keyboard controls in main.c call sim_crockpot_* helpers to drive the state.
 *
 * Temperature drifts slowly toward a target based on the current heat state
 * so the UI arc and label update visibly during a session.
 */

#include "crockpot.h"
#include "wifi.h"        /* wifi_is_connected() */
#include "esp_timer.h"   /* esp_timer_get_time() */
#include <string.h>
#include <stdio.h>

/* ── Simulator state ─────────────────────────────────────────────────────── */

static crockpot_state_t      s_state        = CROCKPOT_OFF;
static float                 s_temperature  = 72.0f;   /* °F, room temperature */
static bool                  s_sensor_error = false;
static crockpot_alert_cb_t   s_alert_cb     = NULL;

/* Target temperatures for each heat state */
static const float k_target_temp[4] = {
    [CROCKPOT_OFF]  = 72.0f,
    [CROCKPOT_WARM] = 165.0f,
    [CROCKPOT_LOW]  = 195.0f,
    [CROCKPOT_HIGH] = 250.0f,
};

/* ── crockpot.h API ──────────────────────────────────────────────────────── */

bool crockpot_init(void)
{
    fprintf(stderr, "[sim] crockpot init\n");
    return true;
}

crockpot_status_t crockpot_get_status(void)
{
    /* Drift temperature 0.2°F per call toward the target for the current state */
    float target = k_target_temp[s_state];
    float diff   = target - s_temperature;
    if (diff > 0.2f)       s_temperature += 0.2f;
    else if (diff < -0.2f) s_temperature -= 0.2f;
    else                   s_temperature  = target;

    return (crockpot_status_t){
        .state           = s_state,
        .temperature_f   = s_temperature,
        /* Uptime in seconds from process start */
        .uptime_seconds  = (uint32_t)(esp_timer_get_time() / 1000000LL),
        .wifi_connected  = wifi_is_connected(),
        .sensor_error    = s_sensor_error,
    };
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
    if (strcmp(str, "off")  == 0 || strcmp(str, "OFF")  == 0) { *out = CROCKPOT_OFF;  return true; }
    if (strcmp(str, "warm") == 0 || strcmp(str, "WARM") == 0) { *out = CROCKPOT_WARM; return true; }
    if (strcmp(str, "low")  == 0 || strcmp(str, "LOW")  == 0) { *out = CROCKPOT_LOW;  return true; }
    if (strcmp(str, "high") == 0 || strcmp(str, "HIGH") == 0) { *out = CROCKPOT_HIGH; return true; }
    return false;
}

void crockpot_control_task(void *pvParameters)
{
    (void)pvParameters;
    /* Not used in simulator — GUI updates driven by LVGL timer */
}

void crockpot_set_alert_callback(crockpot_alert_cb_t cb)
{
    s_alert_cb = cb;
}

/* ── Simulator control helpers (declared in sim_controls.h) ─────────────── */

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
    fprintf(stderr, "[sim] sensor error: %s\n", s_sensor_error ? "ON" : "OFF");
}
