/**
 * @file gui.c
 * @brief Crockpot touchscreen GUI using LVGL
 *
 * Layout (480×320 landscape):
 *
 *   ┌─────────────────────────────────────────────────────┐
 *   │           ╭───────────────────╮                     │
 *   │          ╱     state name      ╲                    │ arc (140×140)
 *   │         │      142.5°F          │                   │
 *   │          ╲                     ╱                    │
 *   │           ╰───────────────────╯                     │
 *   │  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐           │
 *   │  │ OFF  │  │ WARM │  │ LOW  │  │ HIGH │           │ buttons
 *   │  └──────┘  └──────┘  └──────┘  └──────┘           │
 *   │ ≋ WiFi        00:42       ⚙  ≡                     │ status bar
 *   └─────────────────────────────────────────────────────┘
 *
 * The LVGL task is owned by esp_lvgl_port — no separate FreeRTOS task.
 * Timer callbacks and event callbacks run in the LVGL task; public API
 * functions acquire the LVGL port lock for external callers.
 */

#include "gui.h"
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
#define COL_ARC_TRACK   lv_color_hex(0x3a3a4e)   // arc background track
#define COL_OFF         lv_color_hex(0x555555)   // gray (OFF state)
#define COL_WARM        lv_color_hex(0xffaa00)   // amber (WARM)
#define COL_LOW         lv_color_hex(0xff6600)   // orange (LOW)
#define COL_HIGH        lv_color_hex(0xff2200)   // red (HIGH)
#define COL_SUCCESS     lv_color_hex(0x00cc44)   // green
#define COL_ERROR       lv_color_hex(0xff3333)   // red (error/alert)

// ============================================================================
// Layout constants (480×320 landscape)
// ============================================================================

#define ARC_SIZE        140   // Arc indicator diameter in pixels
#define ARC_WIDTH       14    // Arc line thickness
#define ARC_Y_OFFSET    5     // Distance from screen top to arc top edge

// Arc center Y = ARC_Y_OFFSET + ARC_SIZE/2 = 75
#define STATE_LBL_Y     (ARC_Y_OFFSET + ARC_SIZE / 2 - 12)  // state name inside arc
#define TEMP_Y          152   // Top of temperature label
#define BTN_Y           220   // Top of heat button row
#define BTN_H           60    // Heat button height
#define BTN_GAP         8     // Gap between / around buttons
// Button width: (480 - 5 gaps) / 4 = (480 - 40) / 4 = 110
#define BTN_W           110
#define STATUSBAR_Y     283   // Top of status bar
#define STATUSBAR_H     37    // Status bar height

// Heat state button ordering
static const crockpot_state_t k_heat_states[4] = {
    CROCKPOT_OFF, CROCKPOT_WARM, CROCKPOT_LOW, CROCKPOT_HIGH
};
static const char * const k_heat_labels[4] = { "OFF", "WARM", "LOW", "HIGH" };

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
static lv_obj_t *s_arc;            // State indicator arc
static lv_obj_t *s_lbl_state;      // "OFF" / "WARM" / "LOW" / "HIGH"
static lv_obj_t *s_lbl_temp;       // "142.5°F"
static lv_obj_t *s_lbl_wifi;       // WiFi symbol (color changes)
static lv_obj_t *s_lbl_uptime;     // "01:23"
static lv_obj_t *s_heat_btns[4];   // OFF, WARM, LOW, HIGH buttons

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
static float               s_hist_min      = 9999.0f;
static float               s_hist_max      = -9999.0f;
static uint32_t            s_hist_ticks    = 0;   // incremented each timer call

// Main screen relay indicator dots
static lv_obj_t *s_dot_relay_m;
static lv_obj_t *s_dot_relay_a;

// Main screen schedule status bar
static lv_obj_t *s_sched_bar;
static lv_obj_t *s_sched_bar_lbl;

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

// Backlight dimming
static uint32_t    s_last_interaction_ms = 0;
static bool        s_dimmed              = false;

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
        display_driver_set_backlight(true);
    }
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
    lv_screen_load_anim(s_scr_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void nav_wifi_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_WIFI;
    lv_screen_load_anim(s_scr_wifi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void nav_info_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_INFO;
    lv_screen_load_anim(s_scr_info, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void nav_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

static void nav_history_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_HISTORY;
    lv_screen_load_anim(s_scr_history, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void nav_schedules_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_SCHEDULES;
    lv_screen_load_anim(s_scr_schedules, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void nav_schedule_build_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_current = GUI_SCREEN_SCHEDULE_BUILD;
    lv_screen_load_anim(s_scr_schedule_build, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void cf_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_config.show_temperature_c = !s_config.show_temperature_c;
    lv_label_set_text(s_cf_lbl,
        s_config.show_temperature_c ? "Units: Celsius (°C)" : "Units: Fahrenheit (°F)");
}

// ── Schedule preset button callback ──────────────────────────────────────────

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
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
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

    crockpot_schedule_start(&s_custom_schedule);
    gui_show_message("Custom schedule started", 3000);
    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
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

/** Create a small square icon button (used in status bar and nav). */
static lv_obj_t *make_icon_btn(lv_obj_t *parent, const char *symbol,
                                lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 34, 34);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_center(lbl);

    return btn;
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
// Main screen
// ============================================================================

static void create_main_screen(void)
{
    s_scr_main = lv_obj_create(NULL);
    style_screen(s_scr_main);

    // ── State indicator arc ──────────────────────────────────────────────────
    s_arc = lv_arc_create(s_scr_main);
    lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, ARC_Y_OFFSET);

    // 270° arc with gap at 6 o'clock:
    //   rotation=135 shifts the start point 135° clockwise from 3 o'clock.
    //   bg_angles(0, 270) means 270° sweep. Combined: 135° → 45° clockwise.
    //   Gap: 45° → 135° = 90° centered at 90° (6 o'clock = bottom). ✓
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);

    // Value = 100% → indicator fills the entire bg arc (changes color only)
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 100);

    // Style: background track
    lv_obj_set_style_arc_color(s_arc, COL_ARC_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);

    // Style: indicator (filled, color reflects heat state)
    lv_obj_set_style_arc_color(s_arc, COL_OFF, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);

    // Hide the drag knob and make the arc non-interactive
    lv_obj_set_style_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    // ── State name label (centered inside the arc) ───────────────────────────
    s_lbl_state = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_state, "OFF");
    lv_obj_set_style_text_font(s_lbl_state, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_state, COL_OFF, LV_PART_MAIN);
    lv_obj_align(s_lbl_state, LV_ALIGN_TOP_MID, 0, STATE_LBL_Y);

    // ── Relay indicator dots (top-right of screen, arc level) ────────────────
    // "M" = main relay,  "A" = aux relay
    // Label on left, colored 10×10 dot on right of label
    {
        int32_t rx = 450, ry = 12;
        lv_obj_t *lbl_m = lv_label_create(s_scr_main);
        lv_label_set_text(lbl_m, "M");
        lv_obj_set_style_text_color(lbl_m, COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(lbl_m, rx - 24, ry);

        s_dot_relay_m = lv_obj_create(s_scr_main);
        lv_obj_set_size(s_dot_relay_m, 10, 10);
        lv_obj_set_pos(s_dot_relay_m, rx - 8, ry + 2);
        lv_obj_set_style_bg_color(s_dot_relay_m, COL_OFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dot_relay_m, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dot_relay_m, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_dot_relay_m, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(s_dot_relay_m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_a = lv_label_create(s_scr_main);
        lv_label_set_text(lbl_a, "A");
        lv_obj_set_style_text_color(lbl_a, COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_a, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(lbl_a, rx - 24, ry + 20);

        s_dot_relay_a = lv_obj_create(s_scr_main);
        lv_obj_set_size(s_dot_relay_a, 10, 10);
        lv_obj_set_pos(s_dot_relay_a, rx - 8, ry + 22);
        lv_obj_set_style_bg_color(s_dot_relay_a, COL_OFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dot_relay_a, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dot_relay_a, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_dot_relay_a, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(s_dot_relay_a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    // ── Temperature label ────────────────────────────────────────────────────
    s_lbl_temp = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_temp, "---.-\xC2\xB0""F");   // ---.-°F
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_temp, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_MID, 0, TEMP_Y);
    // Tap temp label to navigate to history screen
    lv_obj_add_flag(s_lbl_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_temp, nav_history_cb, LV_EVENT_CLICKED, NULL);

    // ── Schedule status bar (between temp and buttons) ───────────────────────
    // y=190, height=24; hidden until a schedule is active
    s_sched_bar = lv_obj_create(s_scr_main);
    lv_obj_set_pos(s_sched_bar, 0, 190);
    lv_obj_set_size(s_sched_bar, 480, 24);
    lv_obj_set_style_bg_color(s_sched_bar, lv_color_hex(0x1e3a5f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sched_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sched_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_sched_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_sched_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_sched_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sched_bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_sched_bar, nav_schedules_cb, LV_EVENT_CLICKED, NULL);

    s_sched_bar_lbl = lv_label_create(s_sched_bar);
    lv_label_set_text(s_sched_bar_lbl, "");
    lv_obj_set_style_text_color(s_sched_bar_lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sched_bar_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_sched_bar_lbl, LV_ALIGN_CENTER, 0, 0);

    // ── Heat control buttons (OFF / WARM / LOW / HIGH) ───────────────────────
    for (int i = 0; i < 4; i++) {
        int32_t btn_x = BTN_GAP + i * (BTN_W + BTN_GAP);

        lv_obj_t *btn = lv_button_create(s_scr_main);
        lv_obj_set_pos(btn, btn_x, BTN_Y);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_set_style_bg_color(btn, COL_SURFACE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COL_ACCENT,  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, COL_TEXT_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Dirty trick: COL_TEXT_DIM is a macro, use it as a literal here
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);

        lv_obj_add_event_cb(btn, heat_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)k_heat_states[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_heat_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);

        s_heat_btns[i] = btn;
    }

    // ── Status bar ───────────────────────────────────────────────────────────
    lv_obj_t *sbar = lv_obj_create(s_scr_main);
    lv_obj_set_pos(sbar, 0, STATUSBAR_Y);
    lv_obj_set_size(sbar, 480, STATUSBAR_H);
    lv_obj_set_style_bg_color(sbar, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sbar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sbar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi icon — tap to go to WiFi screen
    s_lbl_wifi = lv_label_create(sbar);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, COL_TEXT_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_flag(s_lbl_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_wifi, nav_wifi_cb, LV_EVENT_CLICKED, NULL);

    // Uptime — center
    s_lbl_uptime = lv_label_create(sbar);
    lv_label_set_text(s_lbl_uptime, "00:00");
    lv_obj_set_style_text_color(s_lbl_uptime, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_uptime, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_uptime, LV_ALIGN_CENTER, 0, 0);

    // Settings button (⚙)
    lv_obj_t *set_btn = make_icon_btn(sbar, LV_SYMBOL_SETTINGS,
                                      LV_ALIGN_RIGHT_MID, -90, 0);
    lv_obj_add_event_cb(set_btn, nav_settings_cb, LV_EVENT_CLICKED, NULL);

    // Info button (≡)
    lv_obj_t *info_btn = make_icon_btn(sbar, LV_SYMBOL_LIST,
                                       LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_add_event_cb(info_btn, nav_info_cb, LV_EVENT_CLICKED, NULL);

    // Schedule button (calendar icon)
    lv_obj_t *sched_btn = make_icon_btn(sbar, LV_SYMBOL_BULLET,
                                        LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(sched_btn, nav_schedules_cb, LV_EVENT_CLICKED, NULL);
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

    // Chart — 480×220, below title
    s_chart = lv_chart_create(s_scr_history);
    lv_obj_set_size(s_chart, 460, 200);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 44);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HISTORY_POINTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 60, 320);
    lv_obj_set_style_bg_color(s_chart, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chart, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);

    s_chart_series = lv_chart_add_series(s_chart, COL_ACCENT,
                                         LV_CHART_AXIS_PRIMARY_Y);

    // Initialize all points to the "no data" value
    lv_chart_set_all_value(s_chart, s_chart_series, LV_CHART_POINT_NONE);

    // Min / current / max labels at bottom
    s_lbl_hist_min = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_min, "Min: --");
    lv_obj_set_style_text_color(s_lbl_hist_min, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_min, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_min, LV_ALIGN_BOTTOM_LEFT, 10, -8);

    s_lbl_hist_current = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_current, "Now: --");
    lv_obj_set_style_text_color(s_lbl_hist_current, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_current, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_current, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_lbl_hist_max = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_max, "Max: --");
    lv_obj_set_style_text_color(s_lbl_hist_max, COL_ERROR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_hist_max, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_hist_max, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
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

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    crockpot_status_t st = crockpot_get_status();

    // Arc + state label
    lv_color_t sc = get_state_color(st.state);
    lv_obj_set_style_arc_color(s_arc, sc, LV_PART_INDICATOR);
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

    // Relay indicator dots
    lv_obj_set_style_bg_color(s_dot_relay_m,
        st.relay_main ? COL_SUCCESS : COL_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dot_relay_a,
        st.relay_aux  ? COL_SUCCESS : COL_OFF, LV_PART_MAIN);

    // Schedule status bar
    if (st.schedule_active) {
        lv_obj_clear_flag(s_sched_bar, LV_OBJ_FLAG_HIDDEN);

        char sbar_txt[80];
        if (st.schedule_step_remaining_s > 0) {
            uint32_t rem_h = st.schedule_step_remaining_s / 3600;
            uint32_t rem_m = (st.schedule_step_remaining_s % 3600) / 60;
            snprintf(sbar_txt, sizeof(sbar_txt),
                     "%s  Step %d/%d  %luh %02lum left",
                     st.schedule_name,
                     st.schedule_step + 1, st.schedule_total_steps,
                     (unsigned long)rem_h, (unsigned long)rem_m);
        } else {
            snprintf(sbar_txt, sizeof(sbar_txt),
                     "%s  Step %d/%d",
                     st.schedule_name,
                     st.schedule_step + 1, st.schedule_total_steps);
        }
        lv_label_set_text(s_sched_bar_lbl, sbar_txt);
    } else {
        lv_obj_add_flag(s_sched_bar, LV_OBJ_FLAG_HIDDEN);
    }

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
            if (s_hist_min < 9000.0f) {
                snprintf(tmp, sizeof(tmp), "Min: %.1f\xC2\xB0""F", s_hist_min);
                lv_label_set_text(s_lbl_hist_min, tmp);
            }
            snprintf(tmp, sizeof(tmp), "Now: %.1f\xC2\xB0""F", st.temperature_f);
            lv_label_set_text(s_lbl_hist_current, tmp);
            if (s_hist_max > -9000.0f) {
                snprintf(tmp, sizeof(tmp), "Max: %.1f\xC2\xB0""F", s_hist_max);
                lv_label_set_text(s_lbl_hist_max, tmp);
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

    // Backlight dimming
    if (s_config.screen_timeout_s > 0) {
        uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t idle_ms  = now_ms - s_last_interaction_ms;
        bool     should   = idle_ms > (uint32_t)s_config.screen_timeout_s * 1000;
        if (should && !s_dimmed) {
            s_dimmed = true;
            display_driver_set_backlight(false);
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
    lv_screen_load_anim(targets[screen], LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);

    lvgl_port_unlock();
}

void gui_back(void)
{
    if (!s_initialized) return;
    if (!lvgl_port_lock(0)) return;

    s_current = GUI_SCREEN_MAIN;
    lv_screen_load_anim(s_scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);

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
