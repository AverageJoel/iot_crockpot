/**
 * @file gui.c
 * @brief Crockpot touchscreen GUI using LVGL
 *
 * Layout (480×320 landscape):
 *
 *   y=0   ┌─────────────────────────────────────────────────────┐
 *         │ ≋ WiFi      00:42                       ⚙    ≡      │ h=36  top strip
 *   y=36  ├─────────────────────────────────────────────────────┤
 *         │              142.5°F                                 │ h=88  temp panel
 *         │              ● LOW                                   │
 *   y=124 ├─────────────────────────────────────────────────────┤
 *         │  [    Manual      ]  [    Schedule    ]              │ h=36  tab bar
 *   y=160 ├─────────────────────────────────────────────────────┤
 *         │                                                      │
 *         │  (button area — contents swap per active tab)        │ h=144
 *         │                                                      │
 *   y=304 └────────────────(16px bottom margin)──────────────────┘
 *
 * The LVGL task is owned by esp_lvgl_port — no separate FreeRTOS task.
 * Timer callbacks and event callbacks run in the LVGL task; public API
 * functions acquire the LVGL port lock for external callers.
 */

#include "gui.h"
#include "display.h"
#include "display_driver.h"
#include "crockpot.h"
#include "wifi.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "gui";

// ============================================================================
// Color palette
// ============================================================================

#define COL_BG          lv_color_hex(0x1a1a2e)   // dark navy
#define COL_SURFACE     lv_color_hex(0x2a2a3e)   // slightly lighter navy
#define COL_TEXT        lv_color_hex(0xffffff)   // white
#define COL_TEXT_DIM    lv_color_hex(0x888888)   // gray
#define COL_ACCENT      lv_color_hex(0x4a9eff)   // blue
#define COL_OFF         lv_color_hex(0x555555)   // gray (OFF state)
#define COL_WARM        lv_color_hex(0xffaa00)   // amber (WARM)
#define COL_LOW         lv_color_hex(0xff6600)   // orange (LOW)
#define COL_HIGH        lv_color_hex(0xff2200)   // red (HIGH)
#define COL_SUCCESS     lv_color_hex(0x00cc44)   // green
#define COL_ERROR       lv_color_hex(0xff3333)   // red (error/alert)

// ============================================================================
// Layout constants (480×320 landscape)
// ============================================================================

#define STRIP_H         36    // top status strip height
#define TEMP_PANEL_Y    36    // top of temperature panel
#define TEMP_PANEL_H    88    // temperature panel height
#define TAB_BAR_Y       124   // 36 + 88
#define TAB_BAR_H       36    // tab bar height
#define BTN_AREA_Y      160   // 124 + 36
#define BTN_AREA_H      144   // unified button area height
#define BTN_GAP         8     // gap between / around buttons
// Button width: (480 - 5 gaps) / 4 = (480 - 40) / 4 = 110
#define BTN_W           110

// Heat state button ordering
static const crockpot_state_t k_heat_states[4] = {
    CROCKPOT_OFF, CROCKPOT_WARM, CROCKPOT_LOW, CROCKPOT_HIGH
};
static const char * const k_heat_labels[4] = { "OFF", "WARM", "LOW", "HIGH" };

// Preset schedules (main screen row)
static const char * const k_preset_names[3] = {
    "Slow Cook", "Quick Warm", "All Day"
};
static const char * const k_preset_labels[4] = {
    "Slow Cook", "Quick Warm", "All Day", "Custom"
};
static const crockpot_schedule_t * const k_preset_scheds[3] = {
    &CROCKPOT_SCHED_SLOW_COOK,
    &CROCKPOT_SCHED_QUICK_WARM,
    &CROCKPOT_SCHED_ALL_DAY,
};

// ============================================================================
// Module state
// ============================================================================

static bool          s_initialized   = false;
static gui_screen_t  s_current       = GUI_SCREEN_MAIN;
static gui_config_t  s_config = {
    .show_temperature_c = false,
    .show_wifi_status   = true,
    .screen_timeout_s   = 60,
};

// Screen root objects
static lv_obj_t *s_scr_main;
static lv_obj_t *s_scr_settings;
static lv_obj_t *s_scr_wifi;
static lv_obj_t *s_scr_info;
static lv_obj_t *s_scr_history;
static lv_obj_t *s_scr_schedules;
static lv_obj_t *s_scr_schedule_build;

// Main screen — widgets that need periodic updates
static lv_obj_t *s_lbl_state;      // "OFF" / "WARM" / "LOW" / "HIGH"
static lv_obj_t *s_lbl_temp;       // "142.5°F"
static lv_obj_t *s_lbl_wifi;       // WiFi symbol (color changes)
static lv_obj_t *s_lbl_uptime;     // "01:23"
static lv_obj_t *s_heat_btns[4];   // OFF, WARM, LOW, HIGH buttons

// Main screen tab bar
static lv_obj_t *s_tab_btn_manual;
static lv_obj_t *s_tab_btn_sched;

// Main screen button area containers (one visible at a time)
static lv_obj_t *s_cont_manual;           // heat buttons (may be dimmed)
static lv_obj_t *s_cont_sched_idle;       // preset buttons + optional resume
static lv_obj_t *s_cont_sched_active;     // progress label + stop button

// Schedule/resume widgets
static lv_obj_t *s_btn_resume;            // resume button in sched_idle
static lv_obj_t *s_lbl_resume;            // label inside resume button
static lv_obj_t *s_sched_active_lbl;      // progress label in sched_active
static lv_obj_t *s_stop_dialog;           // confirmation overlay (NULL when hidden)
static lv_obj_t *s_preset_sched_btns[4];  // Slow Cook, Quick Warm, All Day, Custom

// Tab and schedule tracking state
static bool s_tab_manual         = true;
static bool s_prev_sched_active  = false;
static const crockpot_schedule_t *s_last_schedule = NULL;

// Settings screen
static lv_obj_t *s_cf_lbl;         // "Units: Fahrenheit (°F)"

// WiFi screen — dynamic
static lv_obj_t *s_wifi_lbl_status; // "Connected" / "Disconnected"
static lv_obj_t *s_wifi_lbl_ip;     // "192.168.1.42"

// Info screen — dynamic
static lv_obj_t *s_info_lbl_uptime; // "Uptime: 0d 00:00"

// History screen
#define HISTORY_POINTS 120
static lv_obj_t           *s_chart;
static lv_chart_series_t  *s_chart_series;
static lv_obj_t           *s_lbl_hist_min;
static lv_obj_t           *s_lbl_hist_current;
static lv_obj_t           *s_lbl_hist_max;
static lv_obj_t           *s_lbl_hist_y_top;  // Y-axis top label
static lv_obj_t           *s_lbl_hist_y_mid;  // Y-axis mid label
static lv_obj_t           *s_lbl_hist_y_bot;  // Y-axis bottom label
static float               s_hist_min      = 9999.0f;
static float               s_hist_max      = -9999.0f;
static uint32_t            s_hist_ticks    = 0;   // incremented each timer call

// Schedule screen
static lv_obj_t *s_sched_stop_btn;     // "Stop Schedule" — shown only when active

// Schedule builder data
#define BUILD_MAX_STEPS 6
typedef struct {
    crockpot_state_t state;
    uint8_t          hours;       // 0–23
    uint8_t          minutes;     // 0, 15, 30, or 45
} build_step_t;

static build_step_t  s_build_steps[BUILD_MAX_STEPS];
static uint8_t       s_build_step_count = 2;

// Persistent custom schedule (pointers stay valid while schedule runs)
static crockpot_schedule_step_t s_custom_sched_steps[BUILD_MAX_STEPS];
static crockpot_schedule_t      s_custom_schedule = {
    .name      = "Custom",
    .steps     = s_custom_sched_steps,
    .num_steps = 0,
    .repeat    = false,
};

// Builder row widgets (state label + duration label per step)
static lv_obj_t *s_build_state_lbl[BUILD_MAX_STEPS];
static lv_obj_t *s_build_dur_lbl[BUILD_MAX_STEPS];
static lv_obj_t *s_build_scroll;   // scrollable container for step rows

// Toast overlay (created on lv_layer_top, lives above all screens)
static lv_obj_t   *s_toast       = NULL;
static lv_timer_t *s_toast_timer = NULL;

// Status update timer
static lv_timer_t *s_update_timer = NULL;

// Backlight dimming / screensaver
static uint32_t    s_last_interaction_ms     = 0;
static bool        s_dimmed                  = false;
static lv_obj_t   *s_scr_screensaver         = NULL;
static lv_obj_t   *s_lbl_ss_temp             = NULL;
static lv_obj_t   *s_lbl_ss_state            = NULL;
static lv_obj_t   *s_scr_before_screensaver  = NULL;

// ============================================================================
// Helpers
// ============================================================================

static lv_color_t get_state_color(crockpot_state_t state)
{
    switch (state) {
        case CROCKPOT_WARM: return COL_WARM;
        case CROCKPOT_LOW:  return COL_LOW;
        case CROCKPOT_HIGH: return COL_HIGH;
        default:            return COL_OFF;
    }
}

/** Mark a user interaction (wakes backlight, resets dim timer). */
static void wake(void)
{
    s_last_interaction_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_dimmed) {
        s_dimmed = false;
        display_set_brightness(100);
        if (s_scr_before_screensaver) {
            lv_screen_load(s_scr_before_screensaver);
        }
    }
}

/** LVGL indev event callback — wakes backlight on any screen touch. */
static void touch_wake_cb(lv_event_t *e)
{
    (void)e;
    wake();
}

// ============================================================================
// Toast internals (called only from LVGL task — no lock needed)
// ============================================================================

static void toast_dismiss_internal(void)
{
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    toast_dismiss_internal();
}

static void toast_clicked_cb(lv_event_t *e)
{
    (void)e;
    toast_dismiss_internal();
}

// ============================================================================
// LVGL event callbacks (run in LVGL task — no lock needed)
// ============================================================================

static void heat_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    crockpot_state_t new_state = (crockpot_state_t)(uintptr_t)lv_event_get_user_data(e);
    crockpot_set_state(new_state);
    wake();
}

static void nav_settings_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_SETTINGS;
    lv_screen_load_anim(s_scr_settings, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

static void nav_wifi_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_WIFI;
    lv_screen_load_anim(s_scr_wifi, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

static void nav_info_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_INFO;
    lv_screen_load_anim(s_scr_info, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

static void nav_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_OVER_RIGHT, 150, 0, false);
}

static void nav_history_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_HISTORY;
    lv_screen_load_anim(s_scr_history, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

static void nav_schedule_build_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_SCHEDULE_BUILD;
    lv_screen_load_anim(s_scr_schedule_build, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

static void cf_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_config.show_temperature_c = !s_config.show_temperature_c;
    lv_label_set_text(s_cf_lbl,
        s_config.show_temperature_c ? "Units: Celsius (°C)" : "Units: Fahrenheit (°F)");
}

// ── Main screen preset row callbacks ─────────────────────────────────────────

/** Tap a preset button on main screen → start schedule immediately */
static void preset_sched_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    const crockpot_schedule_t *sched =
        (const crockpot_schedule_t *)lv_event_get_user_data(e);
    s_last_schedule = sched;
    crockpot_schedule_start(sched);
    char msg[48];
    snprintf(msg, sizeof(msg), "Started: %s", sched->name);
    gui_show_message(msg, 2000);
}

/** Cancel button in stop confirmation dialog */
static void stop_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_stop_dialog) {
        lv_obj_delete(s_stop_dialog);
        s_stop_dialog = NULL;
    }
}

/** Confirm button in stop confirmation dialog */
static void stop_confirm_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_stop_dialog) {
        lv_obj_delete(s_stop_dialog);
        s_stop_dialog = NULL;
    }
    // Save last schedule before stopping
    crockpot_status_t st = crockpot_get_status();
    if (st.schedule_active) {
        s_last_schedule = NULL;
        for (int i = 0; i < 3; i++) {
            if (strcmp(st.schedule_name, k_preset_names[i]) == 0) {
                s_last_schedule = k_preset_scheds[i];
                break;
            }
        }
        if (!s_last_schedule && strcmp(st.schedule_name, "Custom") == 0) {
            s_last_schedule = &s_custom_schedule;
        }
    }
    crockpot_schedule_stop();
    gui_show_message("Schedule stopped", 2000);
}

/** Tap Stop in active row → open confirmation dialog */
static void preset_stop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    if (s_stop_dialog) return;  // already showing

    // Semi-transparent overlay — tap outside dialog cancels
    s_stop_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_stop_dialog, 480, 320);
    lv_obj_set_pos(s_stop_dialog, 0, 0);
    lv_obj_set_style_bg_color(s_stop_dialog, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_stop_dialog, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_stop_dialog, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_stop_dialog, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_stop_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_stop_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_stop_dialog, stop_cancel_cb, LV_EVENT_CLICKED, NULL);

    // Dialog box centered on overlay
    lv_obj_t *dlg = lv_obj_create(s_stop_dialog);
    lv_obj_set_size(dlg, 340, 180);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dlg, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(dlg, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(dlg, 12, LV_PART_MAIN);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(dlg);
    lv_label_set_text(title, "Stop Schedule?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *body = lv_label_create(dlg);
    lv_label_set_text(body, "The crockpot will hold its\ncurrent heat level.");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *cancel_btn = lv_button_create(dlg);
    lv_obj_set_size(cancel_btn, 120, 44);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_obj_set_style_bg_color(cancel_btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cancel_btn, COL_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(cancel_btn, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, stop_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_obj_t *stop_btn = lv_button_create(dlg);
    lv_obj_set_size(stop_btn, 120, 44);
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_set_style_bg_color(stop_btn, COL_ERROR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xcc0000),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stop_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(stop_btn, stop_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, "\xe2\x96\xa0 Stop");
    lv_obj_set_style_text_color(stop_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(stop_lbl);
}

/** Tap Custom → open schedule builder directly */
static void nav_custom_sched_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_SCHEDULE_BUILD;
    lv_screen_load_anim(s_scr_schedule_build, LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);
}

/** Tap Manual tab button */
static void tab_manual_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_tab_manual = true;
}

/** Tap Schedule tab button */
static void tab_sched_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_tab_manual = false;
}

/** Tap Resume → restart the last stopped schedule from step 1 */
static void resume_sched_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    if (!s_last_schedule) return;
    crockpot_schedule_start(s_last_schedule);
    char msg[48];
    snprintf(msg, sizeof(msg), "Resumed: %s", s_last_schedule->name);
    gui_show_message(msg, 2000);
}

// ── Schedule preset button callback (used by schedules screen) ────────────────

static void sched_preset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    const crockpot_schedule_t *sched =
        (const crockpot_schedule_t *)lv_event_get_user_data(e);
    crockpot_schedule_start(sched);

    // Show confirmation toast and return to main
    char msg[48];
    snprintf(msg, sizeof(msg), "Started: %s", sched->name);
    gui_show_message(msg, 3000);
    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_OVER_RIGHT, 150, 0, false);
}

static void sched_stop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    crockpot_schedule_stop();
    gui_show_message("Schedule stopped", 2000);
}

// ── Schedule builder callbacks ────────────────────────────────────────────────

static void build_refresh_step(int idx);   // forward declaration

static void build_state_cycle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_build_step_count) return;

    // Cycle OFF → WARM → LOW → HIGH → OFF
    s_build_steps[idx].state =
        (crockpot_state_t)((s_build_steps[idx].state + 1) % 4);
    build_refresh_step(idx);
}

static void build_hours_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_build_step_count - 1) return;  // last step is indefinite
    s_build_steps[idx].hours =
        (uint8_t)((s_build_steps[idx].hours + 1) % 24);
    build_refresh_step(idx);
}

static void build_hours_dn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_build_step_count - 1) return;
    s_build_steps[idx].hours =
        (uint8_t)((s_build_steps[idx].hours + 23) % 24);
    build_refresh_step(idx);
}

static void build_min_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_build_step_count - 1) return;
    s_build_steps[idx].minutes =
        (uint8_t)((s_build_steps[idx].minutes + 15) % 60);
    build_refresh_step(idx);
}

static void build_min_dn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_build_step_count - 1) return;
    s_build_steps[idx].minutes =
        (uint8_t)((s_build_steps[idx].minutes + 45) % 60);
    build_refresh_step(idx);
}

// Forward declaration — defined later in the file
static void create_schedule_build_screen(void);

static void build_add_step_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    if (s_build_step_count >= BUILD_MAX_STEPS) return;
    // Insert a new step before the last (indefinite) step, then shift last down
    int last = s_build_step_count - 1;
    s_build_steps[last + 1] = s_build_steps[last];     // copy last (indefinite)
    s_build_steps[last].state   = CROCKPOT_LOW;
    s_build_steps[last].hours   = 1;
    s_build_steps[last].minutes = 0;
    s_build_step_count++;
    // Delete the old screen and rebuild with the new step count
    lv_obj_delete(s_scr_schedule_build);
    s_scr_schedule_build = NULL;
    create_schedule_build_screen();
    s_current = GUI_SCREEN_SCHEDULE_BUILD;
    lv_screen_load(s_scr_schedule_build);
}

static void build_start_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    if (s_build_step_count == 0) return;

    // Build the crockpot_schedule_t from UI data
    for (int i = 0; i < s_build_step_count; i++) {
        s_custom_sched_steps[i].state = s_build_steps[i].state;
        // Last step is indefinite
        if (i == s_build_step_count - 1) {
            s_custom_sched_steps[i].duration_s = 0;
        } else {
            s_custom_sched_steps[i].duration_s =
                (uint32_t)s_build_steps[i].hours * 3600 +
                (uint32_t)s_build_steps[i].minutes * 60;
            // Ensure at least 60s per non-last step
            if (s_custom_sched_steps[i].duration_s < 60)
                s_custom_sched_steps[i].duration_s = 60;
        }
    }
    s_custom_schedule.num_steps = s_build_step_count;

    s_last_schedule = &s_custom_schedule;
    crockpot_schedule_start(&s_custom_schedule);
    gui_show_message("Custom schedule started", 3000);
    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_OVER_RIGHT, 150, 0, false);
}

// ============================================================================
// Screen builder helpers
// ============================================================================

/** Apply common dark background to a screen object. */
static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

/** Add a centered "← Back" button at the bottom of a secondary screen. */
static void add_back_button(lv_obj_t *scr)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 130, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, nav_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(lbl);
}

/** Add a colored accent title at the top of a secondary screen. */
static void add_screen_title(lv_obj_t *scr, const char *title)
{
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 14);
}

// ============================================================================
// Screensaver screen  (dim status: temp + state, no controls)
// ============================================================================

static void create_screensaver_screen(void)
{
    s_scr_screensaver = lv_obj_create(NULL);
    style_screen(s_scr_screensaver);   // dark COL_BG background

    // Large temperature reading, vertically centered
    s_lbl_ss_temp = lv_label_create(s_scr_screensaver);
    lv_label_set_text(s_lbl_ss_temp, "---.-\xC2\xB0""F");
    lv_obj_set_style_text_font(s_lbl_ss_temp, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_ss_temp, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_lbl_ss_temp, LV_ALIGN_CENTER, 0, -18);

    // Heat state label below temperature
    s_lbl_ss_state = lv_label_create(s_scr_screensaver);
    lv_label_set_text(s_lbl_ss_state, "OFF");
    lv_obj_set_style_text_font(s_lbl_ss_state, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_ss_state, COL_OFF, LV_PART_MAIN);
    lv_obj_align(s_lbl_ss_state, LV_ALIGN_CENTER, 0, 18);
}

// ============================================================================
// Main screen
// ============================================================================

static void create_main_screen(void)
{
    s_scr_main = lv_obj_create(NULL);
    style_screen(s_scr_main);

    // ── Top status strip (y=0, h=36) ─────────────────────────────────────────
    lv_obj_t *strip = lv_obj_create(s_scr_main);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_size(strip, 480, STRIP_H);
    lv_obj_set_style_bg_color(strip, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(strip, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(strip, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(strip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi icon — tap to go to WiFi screen
    s_lbl_wifi = lv_label_create(strip);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, COL_TEXT_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_flag(s_lbl_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_wifi, nav_wifi_cb, LV_EVENT_CLICKED, NULL);

    // Uptime — center
    s_lbl_uptime = lv_label_create(strip);
    lv_label_set_text(s_lbl_uptime, "00:00");
    lv_obj_set_style_text_color(s_lbl_uptime, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_uptime, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_uptime, LV_ALIGN_CENTER, 0, 0);

    // Settings button — 32×32
    {
        lv_obj_t *btn = lv_button_create(strip);
        lv_obj_set_size(btn, 32, 32);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -40, 0);
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, nav_settings_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    // Info button — 32×32
    {
        lv_obj_t *btn = lv_button_create(strip);
        lv_obj_set_size(btn, 32, 32);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, nav_info_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LIST);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    // ── Temperature panel (y=36, h=88) ───────────────────────────────────────
    s_lbl_temp = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_temp, "---.-\xC2\xB0""F");
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_temp, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_MID, 0, TEMP_PANEL_Y + 10);
    lv_obj_add_flag(s_lbl_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_temp, nav_history_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_state = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_state, "OFF");
    lv_obj_set_style_text_font(s_lbl_state, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_state, COL_OFF, LV_PART_MAIN);
    lv_obj_align(s_lbl_state, LV_ALIGN_TOP_MID, 0, TEMP_PANEL_Y + 56);

    // ── Tab bar (y=124, h=36) ─────────────────────────────────────────────────
    s_tab_btn_manual = lv_button_create(s_scr_main);
    lv_obj_set_pos(s_tab_btn_manual, 0, TAB_BAR_Y);
    lv_obj_set_size(s_tab_btn_manual, 232, TAB_BAR_H);
    lv_obj_set_style_bg_color(s_tab_btn_manual, COL_ACCENT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_tab_btn_manual, COL_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_tab_btn_manual, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tab_btn_manual, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_tab_btn_manual, tab_manual_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *lbl = lv_label_create(s_tab_btn_manual);
        lv_label_set_text(lbl, LV_SYMBOL_POWER "  Manual");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    s_tab_btn_sched = lv_button_create(s_scr_main);
    lv_obj_set_pos(s_tab_btn_sched, 248, TAB_BAR_Y);
    lv_obj_set_size(s_tab_btn_sched, 232, TAB_BAR_H);
    lv_obj_set_style_bg_color(s_tab_btn_sched, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_tab_btn_sched, COL_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_tab_btn_sched, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tab_btn_sched, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_tab_btn_sched, tab_sched_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *lbl = lv_label_create(s_tab_btn_sched);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH "  Schedule");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);
    }

    // ── Button area containers (y=160, h=144) ─────────────────────────────────
    // All three are the same position/size; only one is visible at a time.

    // A: s_cont_manual — Manual tab (default visible)
    s_cont_manual = lv_obj_create(s_scr_main);
    lv_obj_set_pos(s_cont_manual, 0, BTN_AREA_Y);
    lv_obj_set_size(s_cont_manual, 480, BTN_AREA_H);
    lv_obj_set_style_bg_opa(s_cont_manual, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cont_manual, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cont_manual, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_cont_manual, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        int32_t btn_x = BTN_GAP + i * (BTN_W + BTN_GAP);

        lv_obj_t *btn = lv_button_create(s_cont_manual);
        lv_obj_set_pos(btn, btn_x, 0);
        lv_obj_set_size(btn, BTN_W, BTN_AREA_H);
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, heat_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)k_heat_states[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_heat_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);

        s_heat_btns[i] = btn;
    }

    // B: s_cont_sched_idle — Schedule tab, no schedule active (hidden by default)
    s_cont_sched_idle = lv_obj_create(s_scr_main);
    lv_obj_set_pos(s_cont_sched_idle, 0, BTN_AREA_Y);
    lv_obj_set_size(s_cont_sched_idle, 480, BTN_AREA_H);
    lv_obj_set_style_bg_opa(s_cont_sched_idle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cont_sched_idle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cont_sched_idle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_cont_sched_idle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_sched_idle, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 4; i++) {
        int32_t btn_x = BTN_GAP + i * (BTN_W + BTN_GAP);

        lv_obj_t *btn = lv_button_create(s_cont_sched_idle);
        lv_obj_set_pos(btn, btn_x, 0);
        lv_obj_set_size(btn, BTN_W, BTN_AREA_H);  // height updated by update_main_button_area
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_preset_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);

        if (i < 3) {
            lv_obj_add_event_cb(btn, preset_sched_cb, LV_EVENT_CLICKED,
                                (void *)k_preset_scheds[i]);
        } else {
            lv_obj_add_event_cb(btn, nav_custom_sched_cb, LV_EVENT_CLICKED, NULL);
        }
        s_preset_sched_btns[i] = btn;
    }

    // Resume button — shown only after a schedule has been stopped
    s_btn_resume = lv_button_create(s_cont_sched_idle);
    lv_obj_set_pos(s_btn_resume, 9, 108);
    lv_obj_set_size(s_btn_resume, 462, 36);
    lv_obj_set_style_bg_color(s_btn_resume, COL_SUCCESS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_resume, lv_color_hex(0x009933),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_btn_resume, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_resume, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_resume, resume_sched_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_resume, LV_OBJ_FLAG_HIDDEN);

    s_lbl_resume = lv_label_create(s_btn_resume);
    lv_label_set_text(s_lbl_resume, "\xe2\x86\xba  Resume");
    lv_obj_set_style_text_font(s_lbl_resume, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_resume, COL_TEXT, LV_PART_MAIN);
    lv_obj_center(s_lbl_resume);

    // C: s_cont_sched_active — Schedule tab, schedule running (hidden by default)
    s_cont_sched_active = lv_obj_create(s_scr_main);
    lv_obj_set_pos(s_cont_sched_active, 0, BTN_AREA_Y);
    lv_obj_set_size(s_cont_sched_active, 480, BTN_AREA_H);
    lv_obj_set_style_bg_opa(s_cont_sched_active, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cont_sched_active, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cont_sched_active, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_cont_sched_active, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont_sched_active, LV_OBJ_FLAG_HIDDEN);

    s_sched_active_lbl = lv_label_create(s_cont_sched_active);
    lv_label_set_text(s_sched_active_lbl, "");
    lv_label_set_long_mode(s_sched_active_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_sched_active_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sched_active_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_sched_active_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_sched_active_lbl, 0, 8);
    lv_obj_set_width(s_sched_active_lbl, 480);

    lv_obj_t *stop_btn = lv_button_create(s_cont_sched_active);
    lv_obj_set_pos(stop_btn, 9, 40);
    lv_obj_set_size(stop_btn, 462, 104);
    lv_obj_set_style_bg_color(stop_btn, COL_ERROR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xcc0000),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stop_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(stop_btn, preset_stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, "\xe2\x96\xa0  Stop Schedule");
    lv_obj_set_style_text_color(stop_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(stop_lbl);
}

// ============================================================================
// Settings screen
// ============================================================================

static void create_settings_screen(void)
{
    s_scr_settings = lv_obj_create(NULL);
    style_screen(s_scr_settings);

    add_screen_title(s_scr_settings, LV_SYMBOL_SETTINGS "  Settings");

    // ── Temperature unit toggle ──────────────────────────────────────────────
    lv_obj_t *cf_btn = lv_button_create(s_scr_settings);
    lv_obj_set_size(cf_btn, 320, 52);
    lv_obj_align(cf_btn, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(cf_btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cf_btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(cf_btn, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(cf_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cf_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(cf_btn, cf_toggle_cb, LV_EVENT_CLICKED, NULL);

    s_cf_lbl = lv_label_create(cf_btn);
    lv_label_set_text(s_cf_lbl,
        s_config.show_temperature_c ? "Units: Celsius (°C)" : "Units: Fahrenheit (°F)");
    lv_obj_set_style_text_color(s_cf_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_cf_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(s_cf_lbl);

    // ── Safety note ──────────────────────────────────────────────────────────
    lv_obj_t *note = lv_label_create(s_scr_settings);
    lv_label_set_text(note, "Auto-shutoff at 300\xC2\xB0""F / 149\xC2\xB0""C");  // °
    lv_obj_set_style_text_color(note, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 130);

    add_back_button(s_scr_settings);
}

// ============================================================================
// WiFi screen
// ============================================================================

static void create_wifi_screen(void)
{
    s_scr_wifi = lv_obj_create(NULL);
    style_screen(s_scr_wifi);

    add_screen_title(s_scr_wifi, LV_SYMBOL_WIFI "  WiFi");

    s_wifi_lbl_status = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_lbl_status, "Disconnected");
    lv_obj_set_style_text_color(s_wifi_lbl_status, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_lbl_status, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_wifi_lbl_status, LV_ALIGN_TOP_MID, 0, 60);

    s_wifi_lbl_ip = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_lbl_ip, "");
    lv_obj_set_style_text_color(s_wifi_lbl_ip, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_lbl_ip, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_wifi_lbl_ip, LV_ALIGN_TOP_MID, 0, 100);

    add_back_button(s_scr_wifi);
}

// ============================================================================
// Info screen
// ============================================================================

static void create_info_screen(void)
{
    s_scr_info = lv_obj_create(NULL);
    style_screen(s_scr_info);

    add_screen_title(s_scr_info, LV_SYMBOL_LIST "  Device Info");

    s_info_lbl_uptime = lv_label_create(s_scr_info);
    lv_label_set_text(s_info_lbl_uptime, "Uptime: 0d 00:00");
    lv_obj_set_style_text_color(s_info_lbl_uptime, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_info_lbl_uptime, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_info_lbl_uptime, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *ver = lv_label_create(s_scr_info);
    lv_label_set_text(ver, "Firmware: v0.1.0");
    lv_obj_set_style_text_color(ver, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 96);

    lv_obj_t *hw = lv_label_create(s_scr_info);
    lv_label_set_text(hw, "ESP32-S3-WROOM-1-N4R2  4MB+2MB");
    lv_obj_set_style_text_color(hw, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(hw, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(hw, LV_ALIGN_TOP_MID, 0, 124);

    add_back_button(s_scr_info);
}

// ============================================================================
// History screen
// ============================================================================

static void create_history_screen(void)
{
    s_scr_history = lv_obj_create(NULL);
    style_screen(s_scr_history);

    add_screen_title(s_scr_history, "Temperature History");

    // Chart — 420×170, shifted right to leave room for Y-axis labels on the left
    s_chart = lv_chart_create(s_scr_history);
    lv_obj_set_size(s_chart, 420, 170);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 50);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HISTORY_POINTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 60, 320);
    lv_obj_set_style_bg_color(s_chart, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chart, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);

    s_chart_series = lv_chart_add_series(s_chart, COL_ACCENT,
                                         LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(s_chart, s_chart_series, LV_CHART_POINT_NONE);

    // Y-axis labels (left of chart; chart left edge ≈ x=30)
    s_lbl_hist_y_top = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_y_top, "320\xC2\xB0""F");
    lv_obj_set_style_text_color(s_lbl_hist_y_top, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_y_top, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_y_top, LV_ALIGN_TOP_LEFT, 2, 50);

    s_lbl_hist_y_mid = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_y_mid, "190\xC2\xB0""F");
    lv_obj_set_style_text_color(s_lbl_hist_y_mid, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_y_mid, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_y_mid, LV_ALIGN_TOP_LEFT, 2, 128);

    s_lbl_hist_y_bot = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_y_bot, "60\xC2\xB0""F");
    lv_obj_set_style_text_color(s_lbl_hist_y_bot, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_y_bot, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_y_bot, LV_ALIGN_TOP_LEFT, 2, 208);

    // X-axis labels (below chart — fixed 2-minute rolling window)
    lv_obj_t *x_left = lv_label_create(s_scr_history);
    lv_label_set_text(x_left, "2m ago");
    lv_obj_set_style_text_color(x_left, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(x_left, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(x_left, LV_ALIGN_TOP_LEFT, 30, 224);

    lv_obj_t *x_mid = lv_label_create(s_scr_history);
    lv_label_set_text(x_mid, "1m ago");
    lv_obj_set_style_text_color(x_mid, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(x_mid, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(x_mid, LV_ALIGN_TOP_MID, 0, 224);

    lv_obj_t *x_right = lv_label_create(s_scr_history);
    lv_label_set_text(x_right, "Now");
    lv_obj_set_style_text_color(x_right, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(x_right, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(x_right, LV_ALIGN_TOP_RIGHT, -30, 224);

    // Min / current / max session stats
    s_lbl_hist_min = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_min, "Min: --");
    lv_obj_set_style_text_color(s_lbl_hist_min, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_min, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_min, LV_ALIGN_TOP_LEFT, 10, 242);

    s_lbl_hist_current = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_current, "Now: --");
    lv_obj_set_style_text_color(s_lbl_hist_current, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_current, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_current, LV_ALIGN_TOP_MID, 0, 242);

    s_lbl_hist_max = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_max, "Max: --");
    lv_obj_set_style_text_color(s_lbl_hist_max, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_max, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_max, LV_ALIGN_TOP_RIGHT, -10, 242);

    add_back_button(s_scr_history);
}

// ============================================================================
// Schedules screen
// ============================================================================

static lv_obj_t *make_sched_btn(lv_obj_t *parent,
                                 const crockpot_schedule_t *sched,
                                 int32_t y)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 440, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, sched_preset_cb, LV_EVENT_CLICKED,
                        (void *)sched);

    // Build label: "▶ Name  N steps • STATE→..."
    char text[80];
    char steps_str[40] = "";
    for (int i = 0; i < sched->num_steps && i < 4; i++) {
        if (i > 0) {
            size_t len = strlen(steps_str);
            if (len < sizeof(steps_str) - 3)
                strcat(steps_str, "\xe2\x86\x92");  // UTF-8 →
        }
        size_t len = strlen(steps_str);
        if (len < sizeof(steps_str) - 5)
            strncat(steps_str,
                    crockpot_state_to_string(sched->steps[i].state),
                    sizeof(steps_str) - len - 1);
    }
    snprintf(text, sizeof(text), "\xe2\x96\xb6 %-14s  %d steps \xe2\x80\xa2 %s",
             sched->name, sched->num_steps, steps_str);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

    return btn;
}

static void create_schedules_screen(void)
{
    s_scr_schedules = lv_obj_create(NULL);
    style_screen(s_scr_schedules);

    add_screen_title(s_scr_schedules, LV_SYMBOL_BULLET "  Schedules");

    int32_t y = 52;
    make_sched_btn(s_scr_schedules, &CROCKPOT_SCHED_SLOW_COOK,  y); y += 56;
    make_sched_btn(s_scr_schedules, &CROCKPOT_SCHED_QUICK_WARM, y); y += 56;
    make_sched_btn(s_scr_schedules, &CROCKPOT_SCHED_ALL_DAY,    y); y += 56;

    // "Custom..." button → builder
    lv_obj_t *cust_btn = lv_button_create(s_scr_schedules);
    lv_obj_set_size(cust_btn, 440, 36);
    lv_obj_align(cust_btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(cust_btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cust_btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(cust_btn, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(cust_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cust_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(cust_btn, nav_schedule_build_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cust_lbl = lv_label_create(cust_btn);
    lv_label_set_text(cust_lbl, "Custom...");
    lv_obj_set_style_text_color(cust_lbl, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(cust_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(cust_lbl);
    y += 44;

    // "Stop Schedule" — hidden by default, shown when schedule active
    s_sched_stop_btn = lv_button_create(s_scr_schedules);
    lv_obj_set_size(s_sched_stop_btn, 200, 40);
    lv_obj_align(s_sched_stop_btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(s_sched_stop_btn, COL_ERROR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_sched_stop_btn, lv_color_hex(0xcc0000),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_sched_stop_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_sched_stop_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(s_sched_stop_btn, sched_stop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_sched_stop_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *stop_lbl = lv_label_create(s_sched_stop_btn);
    lv_label_set_text(stop_lbl, "Stop Schedule");
    lv_obj_set_style_text_color(stop_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(stop_lbl);

    add_back_button(s_scr_schedules);
}

// ============================================================================
// Schedule builder screen
// ============================================================================

// Forward: refresh a single step row's labels
static void build_refresh_step(int idx)
{
    if (idx < 0 || idx >= s_build_step_count) return;
    if (!s_build_state_lbl[idx] || !s_build_dur_lbl[idx]) return;

    lv_label_set_text(s_build_state_lbl[idx],
                      crockpot_state_to_string(s_build_steps[idx].state));
    lv_obj_set_style_text_color(s_build_state_lbl[idx],
        get_state_color(s_build_steps[idx].state), LV_PART_MAIN);

    // Last step is always indefinite
    if (idx == s_build_step_count - 1) {
        lv_label_set_text(s_build_dur_lbl[idx], "indefinite");
    } else {
        char dur[16];
        snprintf(dur, sizeof(dur), "%02uh %02um",
                 s_build_steps[idx].hours,
                 s_build_steps[idx].minutes);
        lv_label_set_text(s_build_dur_lbl[idx], dur);
    }
}

static lv_obj_t *make_small_btn(lv_obj_t *parent, const char *txt,
                                 int32_t x, int32_t y,
                                 lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 28, 28);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

static void create_schedule_build_screen(void)
{
    // Initialize default build steps on first call (or after reset)
    static bool initialized = false;
    if (!initialized) {
        s_build_step_count = 2;
        s_build_steps[0] = (build_step_t){ CROCKPOT_HIGH, 1, 0 };
        s_build_steps[1] = (build_step_t){ CROCKPOT_WARM, 0, 0 };
        initialized = true;
    }

    s_scr_schedule_build = lv_obj_create(NULL);
    style_screen(s_scr_schedule_build);

    // Title
    lv_obj_t *title = lv_label_create(s_scr_schedule_build);
    lv_label_set_text(title, "Custom Schedule");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COL_ACCENT, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

    // "▶ Start" button top-right
    lv_obj_t *start_btn = lv_button_create(s_scr_schedule_build);
    lv_obj_set_size(start_btn, 100, 36);
    lv_obj_align(start_btn, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_bg_color(start_btn, COL_SUCCESS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x009933),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(start_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(start_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(start_btn, build_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "\xe2\x96\xb6 Start");
    lv_obj_set_style_text_color(start_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(start_lbl);

    // Back button top-left of bottom area
    lv_obj_t *back_btn = lv_button_create(s_scr_schedule_build);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_set_style_bg_color(back_btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back_btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, nav_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // Scrollable step list
    s_build_scroll = lv_obj_create(s_scr_schedule_build);
    lv_obj_set_pos(s_build_scroll, 0, 48);
    lv_obj_set_size(s_build_scroll, 480, 220);
    lv_obj_set_style_bg_color(s_build_scroll, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_build_scroll, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_build_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_build_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_build_scroll, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_build_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_build_scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_build_scroll, LV_OBJ_FLAG_SCROLLABLE);

    // Create step rows
    for (int i = 0; i < s_build_step_count; i++) {
        // Row container
        lv_obj_t *row = lv_obj_create(s_build_scroll);
        lv_obj_set_size(row, 460, 40);
        lv_obj_set_style_bg_color(row, COL_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // Step number label
        char step_lbl_txt[8];
        snprintf(step_lbl_txt, sizeof(step_lbl_txt), "S%d", i + 1);
        lv_obj_t *step_num = lv_label_create(row);
        lv_label_set_text(step_num, step_lbl_txt);
        lv_obj_set_style_text_color(step_num, COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(step_num, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(step_num, 2, 10);

        // State cycle button
        lv_obj_t *state_btn = lv_button_create(row);
        lv_obj_set_size(state_btn, 56, 28);
        lv_obj_set_pos(state_btn, 28, 4);
        lv_obj_set_style_bg_color(state_btn, lv_color_hex(0x2a3a5e),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(state_btn, COL_ACCENT,
                                  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(state_btn, COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(state_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(state_btn, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(state_btn, build_state_cycle_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        s_build_state_lbl[i] = lv_label_create(state_btn);
        lv_obj_set_style_text_font(s_build_state_lbl[i],
                                   &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_center(s_build_state_lbl[i]);

        // Duration controls (hidden for last step)
        bool is_last = (i == s_build_step_count - 1);

        make_small_btn(row, "-", 92, 6,
                       build_hours_dn_cb, (void *)(uintptr_t)i);
        s_build_dur_lbl[i] = lv_label_create(row);
        lv_obj_set_style_text_color(s_build_dur_lbl[i], COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_build_dur_lbl[i],
                                   &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(s_build_dur_lbl[i], 124, 10);

        if (is_last) {
            // Hide +/- controls for indefinite step
            lv_obj_add_flag(lv_obj_get_child(row, -2),
                            LV_OBJ_FLAG_HIDDEN);  // hours-dn btn is second-to-last child
        }

        make_small_btn(row, "+", 200, 6,
                       build_hours_up_cb, (void *)(uintptr_t)i);
        make_small_btn(row, "-", 236, 6,
                       build_min_dn_cb, (void *)(uintptr_t)i);
        make_small_btn(row, "+", 270, 6,
                       build_min_up_cb, (void *)(uintptr_t)i);

        if (is_last) {
            lv_obj_add_flag(lv_obj_get_child(row, -1), LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lv_obj_get_child(row, -2), LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lv_obj_get_child(row, -3), LV_OBJ_FLAG_HIDDEN);
        }

        // Refresh labels now
        build_refresh_step(i);
    }

    // "+ Add Step" button
    lv_obj_t *add_row = lv_obj_create(s_build_scroll);
    lv_obj_set_size(add_row, 460, 40);
    lv_obj_set_style_bg_color(add_row, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(add_row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(add_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(add_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(add_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *add_btn = lv_button_create(add_row);
    lv_obj_set_size(add_btn, 140, 32);
    lv_obj_set_pos(add_btn, 160, 4);
    lv_obj_set_style_bg_color(add_btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(add_btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(add_btn, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(add_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(add_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(add_btn, build_add_step_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *add_lbl = lv_label_create(add_btn);
    lv_label_set_text(add_lbl, "+ Add Step");
    lv_obj_set_style_text_color(add_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(add_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(add_lbl);

    if (s_build_step_count >= BUILD_MAX_STEPS)
        lv_obj_add_flag(add_btn, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Periodic update timer (runs in LVGL task — no lock needed here)
// ============================================================================

static void update_heat_highlights(crockpot_state_t state)
{
    for (int i = 0; i < 4; i++) {
        bool active = (k_heat_states[i] == state);
        lv_color_t bg = active ? get_state_color(k_heat_states[i]) : COL_SURFACE;
        lv_obj_set_style_bg_color(s_heat_btns[i], bg,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void update_tab_highlights(void)
{
    lv_obj_set_style_bg_color(s_tab_btn_manual,
        s_tab_manual ? COL_ACCENT : COL_SURFACE,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_tab_btn_sched,
        s_tab_manual ? COL_SURFACE : COL_ACCENT,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void update_main_button_area(crockpot_status_t *st)
{
    // Auto-switch to Schedule tab when schedule first becomes active
    if (st->schedule_active && !s_prev_sched_active) {
        s_tab_manual = false;
    }
    update_tab_highlights();
    s_prev_sched_active = st->schedule_active;

    if (s_tab_manual) {
        lv_obj_clear_flag(s_cont_manual, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cont_sched_idle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cont_sched_active, LV_OBJ_FLAG_HIDDEN);
        // Dim/disable heat buttons while a schedule is running
        for (int i = 0; i < 4; i++) {
            if (st->schedule_active) {
                lv_obj_clear_flag(s_heat_btns[i], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_opa(s_heat_btns[i], LV_OPA_50, LV_PART_MAIN);
            } else {
                lv_obj_add_flag(s_heat_btns[i], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_opa(s_heat_btns[i], LV_OPA_COVER, LV_PART_MAIN);
            }
        }
    } else {
        lv_obj_add_flag(s_cont_manual, LV_OBJ_FLAG_HIDDEN);
        if (st->schedule_active) {
            lv_obj_clear_flag(s_cont_sched_active, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_cont_sched_idle, LV_OBJ_FLAG_HIDDEN);
            // Update progress label
            char sched_txt[80];
            if (st->schedule_step_remaining_s > 0) {
                uint32_t rem_h = st->schedule_step_remaining_s / 3600;
                uint32_t rem_m = (st->schedule_step_remaining_s % 3600) / 60;
                snprintf(sched_txt, sizeof(sched_txt),
                         "%s  \xe2\x80\xa2  Step %d/%d  \xe2\x80\xa2  %luh %02lum left",
                         st->schedule_name,
                         st->schedule_step + 1, st->schedule_total_steps,
                         (unsigned long)rem_h, (unsigned long)rem_m);
            } else {
                snprintf(sched_txt, sizeof(sched_txt),
                         "%s  \xe2\x80\xa2  Step %d/%d",
                         st->schedule_name,
                         st->schedule_step + 1, st->schedule_total_steps);
            }
            lv_label_set_text(s_sched_active_lbl, sched_txt);
        } else {
            lv_obj_clear_flag(s_cont_sched_idle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_cont_sched_active, LV_OBJ_FLAG_HIDDEN);
            // Show/hide resume button based on whether a schedule was ever stopped
            if (s_last_schedule) {
                // Resize preset buttons to 100px to make room for resume strip
                for (int i = 0; i < 4; i++) {
                    lv_obj_set_height(s_preset_sched_btns[i], 100);
                }
                lv_obj_clear_flag(s_btn_resume, LV_OBJ_FLAG_HIDDEN);
                char resume_txt[48];
                snprintf(resume_txt, sizeof(resume_txt),
                         "\xe2\x86\xba  Resume: %s", s_last_schedule->name);
                lv_label_set_text(s_lbl_resume, resume_txt);
            } else {
                // Full-height preset buttons, no resume strip
                for (int i = 0; i < 4; i++) {
                    lv_obj_set_height(s_preset_sched_btns[i], BTN_AREA_H);
                }
                lv_obj_add_flag(s_btn_resume, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Highlight the active preset button (border glow when its schedule is running)
    for (int i = 0; i < 3; i++) {
        if (s_preset_sched_btns[i]) {
            bool active = st->schedule_active &&
                          strcmp(st->schedule_name, k_preset_names[i]) == 0;
            lv_obj_set_style_border_color(s_preset_sched_btns[i],
                active ? COL_ACCENT : lv_color_hex(0x888888), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_preset_sched_btns[i],
                active ? 2 : 1, LV_PART_MAIN);
        }
    }
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    crockpot_status_t st = crockpot_get_status();

    // State label
    lv_color_t sc = get_state_color(st.state);
    lv_label_set_text(s_lbl_state, crockpot_state_to_string(st.state));
    lv_obj_set_style_text_color(s_lbl_state, sc, LV_PART_MAIN);

    // Temperature
    char temp[24];
    if (st.sensor_error) {
        snprintf(temp, sizeof(temp), "SENSOR ERR");
    } else if (s_config.show_temperature_c) {
        float c = (st.temperature_f - 32.0f) * 5.0f / 9.0f;
        snprintf(temp, sizeof(temp), "%.1f\xC2\xB0""C", c);
    } else {
        snprintf(temp, sizeof(temp), "%.1f\xC2\xB0""F", st.temperature_f);
    }
    lv_label_set_text(s_lbl_temp, temp);
    lv_obj_set_style_text_color(s_lbl_temp,
        st.sensor_error ? COL_ERROR : COL_TEXT, LV_PART_MAIN);

    // Heat button highlights
    update_heat_highlights(st.state);

    // WiFi indicator color
    lv_obj_set_style_text_color(s_lbl_wifi,
        st.wifi_connected ? COL_SUCCESS : COL_TEXT_DIM,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    // Uptime (HH:MM)
    char uptime[12];
    uint32_t h = st.uptime_seconds / 3600;
    uint32_t m = (st.uptime_seconds % 3600) / 60;
    snprintf(uptime, sizeof(uptime), "%02lu:%02lu", (unsigned long)h, (unsigned long)m);
    lv_label_set_text(s_lbl_uptime, uptime);

    // Main button area: tab containers, schedule state, heat button enable/dim
    update_main_button_area(&st);

    // History chart: update every tick (500 ms timer), but track data per call
    if (!st.sensor_error) {
        // Track min/max
        if (st.temperature_f < s_hist_min) s_hist_min = st.temperature_f;
        if (st.temperature_f > s_hist_max) s_hist_max = st.temperature_f;

        // Add to chart every 2 calls (~1 s)
        s_hist_ticks++;
        if (s_hist_ticks % 2 == 0 && s_chart) {
            lv_chart_set_next_value(s_chart, s_chart_series,
                                    (int32_t)st.temperature_f);
        }

        if (s_current == GUI_SCREEN_HISTORY) {
            char tmp[24];
            bool use_c = s_config.show_temperature_c;
            if (s_hist_min < 9000.0f) {
                if (use_c) {
                    float cv = (s_hist_min - 32.0f) * 5.0f / 9.0f;
                    snprintf(tmp, sizeof(tmp), "Min: %.1f\xC2\xB0""C", cv);
                } else {
                    snprintf(tmp, sizeof(tmp), "Min: %.1f\xC2\xB0""F", s_hist_min);
                }
                lv_label_set_text(s_lbl_hist_min, tmp);
            }
            if (use_c) {
                float cv = (st.temperature_f - 32.0f) * 5.0f / 9.0f;
                snprintf(tmp, sizeof(tmp), "Now: %.1f\xC2\xB0""C", cv);
            } else {
                snprintf(tmp, sizeof(tmp), "Now: %.1f\xC2\xB0""F", st.temperature_f);
            }
            lv_label_set_text(s_lbl_hist_current, tmp);
            if (s_hist_max > -9000.0f) {
                if (use_c) {
                    float cv = (s_hist_max - 32.0f) * 5.0f / 9.0f;
                    snprintf(tmp, sizeof(tmp), "Max: %.1f\xC2\xB0""C", cv);
                } else {
                    snprintf(tmp, sizeof(tmp), "Max: %.1f\xC2\xB0""F", s_hist_max);
                }
                lv_label_set_text(s_lbl_hist_max, tmp);
            }
            // Y-axis labels (scale markers)
            if (use_c) {
                lv_label_set_text(s_lbl_hist_y_top, "149\xC2\xB0""C");
                lv_label_set_text(s_lbl_hist_y_mid, "88\xC2\xB0""C");
                lv_label_set_text(s_lbl_hist_y_bot, "15\xC2\xB0""C");
            } else {
                lv_label_set_text(s_lbl_hist_y_top, "320\xC2\xB0""F");
                lv_label_set_text(s_lbl_hist_y_mid, "190\xC2\xB0""F");
                lv_label_set_text(s_lbl_hist_y_bot, "60\xC2\xB0""F");
            }
        }
    }

    // Update secondary screens if currently visible
    if (s_current == GUI_SCREEN_WIFI) {
        bool conn = wifi_is_connected();
        lv_label_set_text(s_wifi_lbl_status, conn ? "Connected" : "Disconnected");
        lv_obj_set_style_text_color(s_wifi_lbl_status,
            conn ? COL_SUCCESS : COL_ERROR, LV_PART_MAIN);
        char ip[20] = "";
        if (conn) {
            wifi_get_ip_string(ip, sizeof(ip));
        }
        lv_label_set_text(s_wifi_lbl_ip, ip);
    }

    if (s_current == GUI_SCREEN_SCHEDULES) {
        // Show/hide stop button based on schedule state
        if (s_sched_stop_btn) {
            if (st.schedule_active) {
                lv_obj_clear_flag(s_sched_stop_btn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_sched_stop_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (s_current == GUI_SCREEN_INFO) {
        char up_str[40];
        uint32_t days = st.uptime_seconds / 86400;
        uint32_t hrs  = (st.uptime_seconds % 86400) / 3600;
        uint32_t mins = (st.uptime_seconds % 3600) / 60;
        snprintf(up_str, sizeof(up_str), "Uptime: %lud %02lu:%02lu",
                 (unsigned long)days, (unsigned long)hrs, (unsigned long)mins);
        lv_label_set_text(s_info_lbl_uptime, up_str);
    }

    // Screensaver: dim to 15% and show status screen after idle timeout
    if (s_config.screen_timeout_s > 0) {
        uint32_t now_ms  = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t idle_ms = now_ms - s_last_interaction_ms;
        bool should_dim  = idle_ms > (uint32_t)s_config.screen_timeout_s * 1000;
        if (should_dim && !s_dimmed) {
            s_dimmed = true;
            s_scr_before_screensaver = lv_scr_act();
            lv_screen_load(s_scr_screensaver);
            display_set_brightness(15);
        }
        if (s_dimmed) {
            // Keep screensaver labels current
            char ss_temp[24];
            if (st.sensor_error) {
                snprintf(ss_temp, sizeof(ss_temp), "SENSOR ERR");
            } else if (s_config.show_temperature_c) {
                float c = (st.temperature_f - 32.0f) * 5.0f / 9.0f;
                snprintf(ss_temp, sizeof(ss_temp), "%.1f\xC2\xB0""C", c);
            } else {
                snprintf(ss_temp, sizeof(ss_temp), "%.1f\xC2\xB0""F", st.temperature_f);
            }
            lv_label_set_text(s_lbl_ss_temp, ss_temp);
            lv_label_set_text(s_lbl_ss_state, crockpot_state_to_string(st.state));
            lv_obj_set_style_text_color(s_lbl_ss_state,
                st.sensor_error ? COL_ERROR : get_state_color(st.state), LV_PART_MAIN);

        }
    }
}

// ============================================================================
// Public API
// ============================================================================

bool gui_init(void)
{
    if (s_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing GUI");

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return false;
    }

    create_screensaver_screen();
    create_main_screen();
    create_settings_screen();
    create_wifi_screen();
    create_info_screen();
    create_history_screen();
    create_schedules_screen();
    create_schedule_build_screen();

    lvgl_port_unlock();

    s_last_interaction_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_initialized = true;

    ESP_LOGI(TAG, "GUI screens created");
    return true;
}

bool gui_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "gui_start: call gui_init() first");
        return false;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return false;
    }

    lv_screen_load(s_scr_main);
    s_current = GUI_SCREEN_MAIN;

    // Wake backlight on any touch, not just registered button presses
    lv_indev_t *indev = display_get_lvgl_indev();
    if (indev) {
        lv_indev_add_event_cb(indev, touch_wake_cb, LV_EVENT_PRESSED, NULL);
    }

    // Status update every 500 ms (runs in LVGL task, repeats forever)
    s_update_timer = lv_timer_create(update_timer_cb, 500, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "GUI started");
    return true;
}

void gui_set_screen(gui_screen_t screen)
{
    if (!s_initialized || screen >= GUI_SCREEN_COUNT) return;

    lv_obj_t *targets[GUI_SCREEN_COUNT] = {
        [GUI_SCREEN_MAIN]            = s_scr_main,
        [GUI_SCREEN_SETTINGS]        = s_scr_settings,
        [GUI_SCREEN_WIFI]            = s_scr_wifi,
        [GUI_SCREEN_INFO]            = s_scr_info,
        [GUI_SCREEN_HISTORY]         = s_scr_history,
        [GUI_SCREEN_SCHEDULES]       = s_scr_schedules,
        [GUI_SCREEN_SCHEDULE_BUILD]  = s_scr_schedule_build,
    };

    if (!lvgl_port_lock(0)) return;

    s_current = screen;
    lv_screen_load_anim(targets[screen], LV_SCR_LOAD_ANIM_OVER_LEFT, 150, 0, false);

    lvgl_port_unlock();
}

void gui_back(void)
{
    if (!s_initialized) return;
    if (!lvgl_port_lock(0)) return;

    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_OVER_RIGHT, 150, 0, false);

    lvgl_port_unlock();
}

gui_screen_t gui_get_screen(void)
{
    return s_current;
}

static void show_toast(const char *text, lv_color_t bg_color, uint32_t duration_ms)
{
    // Destroy any existing toast first
    toast_dismiss_internal();

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast, 380, 56);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(s_toast, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_toast, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_toast, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_toast, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_toast, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_toast, toast_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(s_toast);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(lbl);

    if (duration_ms > 0) {
        s_toast_timer = lv_timer_create(toast_timer_cb, duration_ms, NULL);
        lv_timer_set_repeat_count(s_toast_timer, 1);
    }
}

void gui_show_message(const char *message, uint32_t duration_ms)
{
    if (!s_initialized || message == NULL) return;
    if (!lvgl_port_lock(0)) return;

    show_toast(message, COL_ACCENT, duration_ms);

    lvgl_port_unlock();
}

void gui_show_error(const char *error)
{
    if (!s_initialized || error == NULL) return;
    if (!lvgl_port_lock(0)) return;

    show_toast(error, COL_ERROR, 0);  // errors don't auto-dismiss

    lvgl_port_unlock();
}

void gui_dismiss_message(void)
{
    if (!lvgl_port_lock(0)) return;

    toast_dismiss_internal();

    lvgl_port_unlock();
}

void gui_set_config(const gui_config_t *config)
{
    if (config == NULL) return;
    s_config = *config;

    if (s_cf_lbl && lvgl_port_lock(0)) {
        lv_label_set_text(s_cf_lbl,
            s_config.show_temperature_c ? "Units: Celsius (°C)" : "Units: Fahrenheit (°F)");
        lvgl_port_unlock();
    }
}

gui_config_t gui_get_config(void)
{
    return s_config;
}
