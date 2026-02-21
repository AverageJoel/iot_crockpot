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

static void cf_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wake();
    s_config.show_temperature_c = !s_config.show_temperature_c;
    lv_label_set_text(s_cf_lbl,
        s_config.show_temperature_c ? "Units: Celsius (°C)" : "Units: Fahrenheit (°F)");
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

    // ── Temperature label ────────────────────────────────────────────────────
    s_lbl_temp = lv_label_create(s_scr_main);
    lv_label_set_text(s_lbl_temp, "---.-\xC2\xB0""F");   // ---.-°F
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_temp, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_MID, 0, TEMP_Y);

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
                                      LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_add_event_cb(set_btn, nav_settings_cb, LV_EVENT_CLICKED, NULL);

    // Info button (≡)
    lv_obj_t *info_btn = make_icon_btn(sbar, LV_SYMBOL_LIST,
                                       LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(info_btn, nav_info_cb, LV_EVENT_CLICKED, NULL);
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
        [GUI_SCREEN_MAIN]     = s_scr_main,
        [GUI_SCREEN_SETTINGS] = s_scr_settings,
        [GUI_SCREEN_WIFI]     = s_scr_wifi,
        [GUI_SCREEN_INFO]     = s_scr_info,
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
