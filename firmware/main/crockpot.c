/**
 * @file crockpot.c
 * @brief Core crockpot state machine implementation
 */

#include "crockpot.h"
#include "temperature.h"
#include "relay.h"
#include "wifi.h"

#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

static const char* TAG = "crockpot";

// State protection mutex
static SemaphoreHandle_t s_state_mutex = NULL;

// Current state
static crockpot_status_t s_status = {
    .state = CROCKPOT_OFF,
    .temperature_f = 0.0f,
    .uptime_seconds = 0,
    .wifi_connected = false,
    .sensor_error = false,
    .relay_main = false,
    .relay_aux  = false,
};

// Boot timestamp for uptime calculation
static int64_t s_boot_time_us = 0;

// Safety alert callback (set by caller, fired on auto-shutoff)
static crockpot_alert_cb_t s_alert_cb = NULL;

// Custom temperature source (NULL → use real sensor)
static crockpot_temp_source_cb_t s_temp_source_cb  = NULL;
static void                     *s_temp_source_ctx  = NULL;

// ── Schedule state ────────────────────────────────────────────────────────
static const crockpot_schedule_t *s_schedule              = NULL;
static uint8_t                    s_sched_step             = 0;
static uint32_t                   s_sched_step_elapsed_s   = 0;

// ── Built-in schedule preset definitions ─────────────────────────────────

static const crockpot_schedule_step_t k_slow_cook_steps[] = {
    { CROCKPOT_HIGH, 3600        },   // 1 h HIGH
    { CROCKPOT_LOW,  6 * 3600    },   // 6 h LOW
    { CROCKPOT_WARM, 0           },   // indefinite WARM
};
const crockpot_schedule_t CROCKPOT_SCHED_SLOW_COOK = {
    .name      = "Slow Cook",
    .steps     = k_slow_cook_steps,
    .num_steps = 3,
    .repeat    = false,
};

static const crockpot_schedule_step_t k_quick_warm_steps[] = {
    { CROCKPOT_HIGH, 30 * 60  },   // 30 min HIGH
    { CROCKPOT_WARM, 0        },   // indefinite WARM
};
const crockpot_schedule_t CROCKPOT_SCHED_QUICK_WARM = {
    .name      = "Quick Warm",
    .steps     = k_quick_warm_steps,
    .num_steps = 2,
    .repeat    = false,
};

static const crockpot_schedule_step_t k_all_day_steps[] = {
    { CROCKPOT_LOW,  8 * 3600 },   // 8 h LOW
    { CROCKPOT_WARM, 0        },   // indefinite WARM
};
const crockpot_schedule_t CROCKPOT_SCHED_ALL_DAY = {
    .name      = "All Day",
    .steps     = k_all_day_steps,
    .num_steps = 2,
    .repeat    = false,
};

// ── Helper: update relay_main/aux in s_status to match current state ──────
// Called inside the mutex.
static void update_relay_status_locked(void)
{
    s_status.relay_main = (s_status.state == CROCKPOT_LOW  ||
                           s_status.state == CROCKPOT_HIGH);
    s_status.relay_aux  = (s_status.state == CROCKPOT_WARM ||
                           s_status.state == CROCKPOT_HIGH);
}

void crockpot_set_alert_callback(crockpot_alert_cb_t cb)
{
    s_alert_cb = cb;
}

void crockpot_set_temp_source(crockpot_temp_source_cb_t cb, void *ctx)
{
    s_temp_source_cb  = cb;
    s_temp_source_ctx = ctx;
}

bool crockpot_init(void)
{
    ESP_LOGI(TAG, "Initializing crockpot control system");

    // Create state mutex
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }

    // Initialize temperature sensor
    if (!temperature_init()) {
        ESP_LOGE(TAG, "Failed to initialize temperature sensor");
        // Continue anyway - sensor might work later
    }

    // Initialize relay control
    if (!relay_init()) {
        ESP_LOGE(TAG, "Failed to initialize relay control");
        return false;
    }

    // Ensure we start in OFF state
    relay_all_off();

    // Record boot time
    s_boot_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Crockpot control system initialized");
    return true;
}

crockpot_status_t crockpot_get_status(void)
{
    crockpot_status_t status;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = s_status;

        // Populate schedule fields from schedule state
        status.schedule_active      = (s_schedule != NULL);
        status.schedule_step        = s_sched_step;
        status.schedule_total_steps = s_schedule ? s_schedule->num_steps : 0;

        if (s_schedule) {
            strncpy(status.schedule_name, s_schedule->name,
                    sizeof(status.schedule_name) - 1);
            status.schedule_name[sizeof(status.schedule_name) - 1] = '\0';

            const crockpot_schedule_step_t *step =
                &s_schedule->steps[s_sched_step];
            if (step->duration_s > 0) {
                uint32_t elapsed = s_sched_step_elapsed_s;
                status.schedule_step_remaining_s =
                    (elapsed < step->duration_s)
                    ? (step->duration_s - elapsed) : 0;
                status.schedule_step_progress =
                    (float)elapsed / step->duration_s;
            } else {
                status.schedule_step_remaining_s = 0;
                status.schedule_step_progress    = 0.0f;
            }
        } else {
            status.schedule_name[0]             = '\0';
            status.schedule_step_remaining_s    = 0;
            status.schedule_step_progress       = 0.0f;
        }

        xSemaphoreGive(s_state_mutex);
    } else {
        // Return last known state on timeout
        status = s_status;
    }

    return status;
}

bool crockpot_set_state(crockpot_state_t state)
{
    ESP_LOGI(TAG, "Setting state to: %s", crockpot_state_to_string(state));

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire state mutex");
        return false;
    }

    // Apply state to relay
    if (!relay_apply_state(state)) {
        ESP_LOGE(TAG, "Failed to apply state to relay");
        xSemaphoreGive(s_state_mutex);
        return false;
    }

    s_status.state = state;
    update_relay_status_locked();
    xSemaphoreGive(s_state_mutex);

    ESP_LOGI(TAG, "State changed to: %s", crockpot_state_to_string(state));
    return true;
}

const char* crockpot_state_to_string(crockpot_state_t state)
{
    switch (state) {
        case CROCKPOT_OFF:  return "OFF";
        case CROCKPOT_WARM: return "WARM";
        case CROCKPOT_LOW:  return "LOW";
        case CROCKPOT_HIGH: return "HIGH";
        default:            return "UNKNOWN";
    }
}

bool crockpot_state_from_string(const char* str, crockpot_state_t* out)
{
    if (str == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(str, "off") == 0) {
        *out = CROCKPOT_OFF;
        return true;
    }
    if (strcasecmp(str, "warm") == 0) {
        *out = CROCKPOT_WARM;
        return true;
    }
    if (strcasecmp(str, "low") == 0) {
        *out = CROCKPOT_LOW;
        return true;
    }
    if (strcasecmp(str, "high") == 0) {
        *out = CROCKPOT_HIGH;
        return true;
    }

    return false;
}

// ============================================================================
// Schedule API
// ============================================================================

void crockpot_schedule_start(const crockpot_schedule_t *sched)
{
    if (!sched || sched->num_steps == 0) return;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_schedule            = sched;
        s_sched_step          = 0;
        s_sched_step_elapsed_s = 0;
        xSemaphoreGive(s_state_mutex);
    }

    crockpot_set_state(sched->steps[0].state);
    ESP_LOGI(TAG, "Schedule '%s' started", sched->name);
}

void crockpot_schedule_stop(void)
{
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_schedule = NULL;
        xSemaphoreGive(s_state_mutex);
    }
    ESP_LOGI(TAG, "Schedule stopped");
}

bool crockpot_schedule_is_active(void)
{
    return s_schedule != NULL;
}

void crockpot_tick_s(void)
{
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    if (!s_schedule) {
        xSemaphoreGive(s_state_mutex);
        return;
    }

    s_sched_step_elapsed_s++;

    const crockpot_schedule_step_t *step = &s_schedule->steps[s_sched_step];

    // Indefinite step — never advance
    if (step->duration_s == 0) {
        xSemaphoreGive(s_state_mutex);
        return;
    }

    if (s_sched_step_elapsed_s >= step->duration_s) {
        s_sched_step++;
        s_sched_step_elapsed_s = 0;

        if (s_sched_step >= s_schedule->num_steps) {
            if (s_schedule->repeat) {
                s_sched_step = 0;
                crockpot_state_t next_state = s_schedule->steps[0].state;
                xSemaphoreGive(s_state_mutex);
                crockpot_set_state(next_state);
                return;
            } else {
                s_schedule = NULL;
                xSemaphoreGive(s_state_mutex);
                ESP_LOGI(TAG, "Schedule completed");
                return;
            }
        }

        crockpot_state_t next_state = s_schedule->steps[s_sched_step].state;
        xSemaphoreGive(s_state_mutex);
        crockpot_set_state(next_state);
        ESP_LOGI(TAG, "Schedule step -> %d (%s)",
                 s_sched_step, crockpot_state_to_string(next_state));
        return;
    }

    xSemaphoreGive(s_state_mutex);
}

// ============================================================================
// Control task
// ============================================================================

void crockpot_control_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Control task started");

    // Register with the task watchdog timer (auto-initialized via sdkconfig).
    // The loop runs every 1 s; watchdog timeout is 10 s — plenty of headroom.
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "TWDT registration failed: %s — watchdog inactive for control task",
                 esp_err_to_name(wdt_err));
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    static int sensor_error_count = 0;

    while (1) {
        // Read temperature: use hook if registered, otherwise hardware sensor
        temperature_reading_t reading;
        if (s_temp_source_cb) {
            reading.temperature_f = s_temp_source_cb(s_temp_source_ctx);
            reading.valid = true;
        } else {
            // Read before taking the mutex (SPI read, may take ~1 ms)
            reading = temperature_read();
        }

        // Flags set inside mutex, callbacks fired after releasing it
        bool temp_shutoff   = false;
        bool sensor_shutoff = false;

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool was_heating = (s_status.state != CROCKPOT_OFF);

            // Update temperature
            if (reading.valid) {
                s_status.temperature_f = reading.temperature_f;
                s_status.sensor_error  = false;
                sensor_error_count     = 0;
            } else {
                s_status.sensor_error = true;
            }

            // Update uptime
            int64_t now_us = esp_timer_get_time();
            s_status.uptime_seconds = (uint32_t)((now_us - s_boot_time_us) / 1000000);

            // Update WiFi status
            s_status.wifi_connected = wifi_is_connected();

            // Safety check: auto-shutoff on high temperature
            if (was_heating && reading.valid &&
                reading.temperature_f > CROCKPOT_SAFETY_TEMP_F) {
                ESP_LOGW(TAG, "SAFETY: Temperature %.1f F exceeds limit, shutting off",
                         reading.temperature_f);
                s_status.state = CROCKPOT_OFF;
                s_schedule = NULL;   // cancel active schedule
                relay_all_off();
                update_relay_status_locked();
                temp_shutoff = true;
            }

            // Safety check: shut off on persistent sensor errors while heating
            if (was_heating && s_status.sensor_error) {
                sensor_error_count++;
                if (sensor_error_count > 10) {
                    ESP_LOGW(TAG, "SAFETY: Persistent sensor error (%d consecutive), shutting off",
                             sensor_error_count);
                    s_status.state = CROCKPOT_OFF;
                    s_schedule = NULL;   // cancel active schedule
                    relay_all_off();
                    update_relay_status_locked();
                    sensor_error_count = 0;
                    sensor_shutoff = true;
                }
            }

            xSemaphoreGive(s_state_mutex);
        }

        // Fire alert callbacks outside the mutex — safe to call crockpot_get_status()
        if (temp_shutoff && s_alert_cb) {
            s_alert_cb(CROCKPOT_ALERT_TEMP_LIMIT, reading.temperature_f);
        }
        if (sensor_shutoff && s_alert_cb) {
            s_alert_cb(CROCKPOT_ALERT_SENSOR_ERROR, reading.temperature_f);
        }

        // Advance schedule clock
        crockpot_tick_s();

        // Feed the task watchdog
        esp_task_wdt_reset();

        // Wait for next cycle
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CROCKPOT_CONTROL_INTERVAL_MS));
    }
}
