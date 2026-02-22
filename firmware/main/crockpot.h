/**
 * @file crockpot.h
 * @brief Core crockpot state machine and control API
 *
 * Interface-agnostic API for controlling crockpot state.
 * Used by Telegram, local display, and any future interfaces.
 */

#ifndef CROCKPOT_H
#define CROCKPOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crockpot operating states
 */
typedef enum {
    CROCKPOT_OFF = 0,
    CROCKPOT_WARM,
    CROCKPOT_LOW,
    CROCKPOT_HIGH
} crockpot_state_t;

/**
 * @brief Complete crockpot status
 */
typedef struct {
    crockpot_state_t state;
    float            temperature_f;
    uint32_t         uptime_seconds;
    bool             wifi_connected;
    bool             sensor_error;
    bool             relay_main;               // main heating relay
    bool             relay_aux;                // aux relay
    bool             schedule_active;
    char             schedule_name[32];
    uint8_t          schedule_step;            // 0-based current step
    uint8_t          schedule_total_steps;
    uint32_t         schedule_step_remaining_s; // 0 = indefinite
    float            schedule_step_progress;    // 0.0–1.0
} crockpot_status_t;

/**
 * @brief Initialize the crockpot control system
 *
 * Must be called before any other crockpot functions.
 * Initializes the state machine, temperature sensor, and relay.
 *
 * @return true on success, false on initialization failure
 */
bool crockpot_init(void);

/**
 * @brief Get current crockpot status
 *
 * Thread-safe function to retrieve complete status.
 *
 * @return Current status structure
 */
crockpot_status_t crockpot_get_status(void);

/**
 * @brief Set crockpot operating state
 *
 * Thread-safe function to change crockpot state.
 * Updates relay output accordingly.
 *
 * @param state Desired operating state
 * @return true on success, false on failure
 */
bool crockpot_set_state(crockpot_state_t state);

/**
 * @brief Convert state enum to human-readable string
 *
 * @param state State to convert
 * @return Static string representation (e.g., "OFF", "WARM", "LOW", "HIGH")
 */
const char* crockpot_state_to_string(crockpot_state_t state);

/**
 * @brief Parse string to state enum
 *
 * Case-insensitive parsing of state strings.
 *
 * @param str String to parse (e.g., "off", "WARM", "Low")
 * @param out Pointer to store parsed state
 * @return true if parsing succeeded, false otherwise
 */
bool crockpot_state_from_string(const char* str, crockpot_state_t* out);

/**
 * @brief Main control loop task
 *
 * FreeRTOS task that runs the main control loop.
 * Handles temperature monitoring, safety checks, and relay control.
 *
 * @param pvParameters Task parameters (unused)
 */
void crockpot_control_task(void* pvParameters);

/**
 * @brief Safety temperature limit in Fahrenheit
 *
 * If temperature exceeds this value, crockpot auto-shuts off.
 */
#define CROCKPOT_SAFETY_TEMP_F 300.0f

/**
 * @brief Control loop interval in milliseconds
 */
#define CROCKPOT_CONTROL_INTERVAL_MS 1000

// ============================================================================
// Safety alert callback
// ============================================================================

/**
 * @brief Reason for an automatic safety shutoff
 */
typedef enum {
    CROCKPOT_ALERT_TEMP_LIMIT,   // temperature exceeded CROCKPOT_SAFETY_TEMP_F
    CROCKPOT_ALERT_SENSOR_ERROR, // persistent sensor errors while heating
} crockpot_alert_t;

/**
 * @brief Callback invoked on automatic safety shutoff
 *
 * Called from the control task after the relay has been turned off and
 * the state mutex has been released — safe to call other crockpot functions.
 *
 * @param reason  Why the shutoff happened
 * @param temp_f  Last known temperature in Fahrenheit
 */
typedef void (*crockpot_alert_cb_t)(crockpot_alert_t reason, float temp_f);

/**
 * @brief Register a safety alert callback.
 *
 * Only one callback is supported. Pass NULL to clear.
 *
 * @param cb  Callback function, or NULL
 */
void crockpot_set_alert_callback(crockpot_alert_cb_t cb);

// ============================================================================
// Temperature source hook
// ============================================================================

/**
 * @brief Callback that provides the current temperature in °F.
 *
 * If registered, the control loop calls this instead of the hardware sensor.
 * Useful for hardware-in-the-loop testing.  Pass NULL to use the real sensor.
 */
typedef float (*crockpot_temp_source_cb_t)(void *ctx);

/**
 * @brief Register a custom temperature source.
 *
 * @param cb   Callback, or NULL to use the hardware sensor
 * @param ctx  Opaque pointer passed back to cb
 */
void crockpot_set_temp_source(crockpot_temp_source_cb_t cb, void *ctx);

// ============================================================================
// Schedule engine
// ============================================================================

/**
 * @brief A single step in a cooking schedule.
 */
typedef struct {
    crockpot_state_t state;
    uint32_t         duration_s;  // 0 = indefinite (valid only for last step)
} crockpot_schedule_step_t;

/**
 * @brief A named cooking schedule.
 */
typedef struct {
    const char                     *name;
    const crockpot_schedule_step_t *steps;
    uint8_t                         num_steps;
    bool                            repeat;
} crockpot_schedule_t;

/** Start a schedule (begins at step 0, sets state immediately). */
void crockpot_schedule_start(const crockpot_schedule_t *sched);

/** Stop the active schedule (does not change current heat state). */
void crockpot_schedule_stop(void);

/** Returns true if a schedule is currently running. */
bool crockpot_schedule_is_active(void);

/**
 * @brief Advance the schedule clock by one second.
 *
 * Call this once per second from the FreeRTOS control task (firmware) or
 * from a 1 s LVGL timer (simulator).  Handles step transitions automatically.
 */
void crockpot_tick_s(void);

// ── Built-in schedule presets ──────────────────────────────────────────────
extern const crockpot_schedule_t CROCKPOT_SCHED_SLOW_COOK;
extern const crockpot_schedule_t CROCKPOT_SCHED_QUICK_WARM;
extern const crockpot_schedule_t CROCKPOT_SCHED_ALL_DAY;

#ifdef __cplusplus
}
#endif

#endif // CROCKPOT_H
