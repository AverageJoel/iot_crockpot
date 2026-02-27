# UI Redesign — Main Screen Overhaul

## Context

The current main screen wastes space on a decorative arc (140×140px) that only shows
the current heat state — the same information already shown by the 4 heat buttons below
it. Presets require 2 taps to reach (status bar → schedules screen). The temperature
font is modest (28pt). Real usage: glance at temp, change heat state, start a preset.

**Goal:** Remove the arc, make the temperature large and dominant, bring the 3 preset
schedules onto the main screen as 1-tap buttons, and morph that preset row into a
schedule progress display when a schedule is running.

User chose: **Option A — All on main screen** (see layout below).

---

## New Main Screen Layout (480×320)

```
y=0   ┌─────────────────────────────────────────────────────┐
      │ ≋ WiFi           00:42                    ⚙  ≡      │ h=28  top strip
y=28  ├─────────────────────────────────────────────────────┤
      │                                                     │
      │                  142.5°F                            │ h=112  temp panel
      │                  ● LOW                              │        (40pt temp, 20pt state)
      │                                                     │
y=140 ├─────────────────────────────────────────────────────┤
      │  (8px gap)                                          │
y=148 │ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐│
      │ │   OFF    │ │   WARM   │ │ ✓ LOW    │ │   HIGH   ││ h=72  heat buttons
      │ └──────────┘ └──────────┘ └──────────┘ └──────────┘│
y=220 ├─────────────────────────────────────────────────────┤
      │  (8px gap)                                          │
y=228 │ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐│
      │ │Slow Cook │ │Quick Warm│ │ All Day  │ │ Custom   ││ h=72  preset row (normal)
      │ └──────────┘ └──────────┘ └──────────┘ └──────────┘│
y=300 │  (20px bottom padding)                              │
y=320 └─────────────────────────────────────────────────────┘
```

**When a schedule is running**, the preset row morphs:
```
y=228 │ ┌────────┐  Slow Cook  •  Step 2/3  •  2h 30m left  │
      │ │■ Stop  │                                           │ h=72
      │ └────────┘                                           │
```

---

## Files to Modify

| File | Change |
|------|--------|
| `firmware/sdkconfig.defaults` | Add `CONFIG_LV_FONT_MONTSERRAT_40=y` |
| `firmware/main/gui.c` | Main screen rewrite + update_timer_cb + new preset callbacks |

`gui.h` does not change (all enum values kept).

---

## Detailed Changes

### 1. `sdkconfig.defaults`
Add one line to the LVGL fonts section:
```
CONFIG_LV_FONT_MONTSERRAT_40=y
```
40pt adds ~70KB to binary. Current binary is 1.43MB in a 2MB partition — 570KB headroom. Safe.

---

### 2. `gui.c` — Layout Constants

Replace current arc/temp/button/statusbar constants with:
```c
#define STRIP_H          28    // top status strip height
#define TEMP_PANEL_Y     28    // top of temperature panel
#define TEMP_PANEL_H     112   // temperature panel height
#define HEAT_BTN_Y       148   // 28 + 112 + 8 gap
#define HEAT_BTN_H       72    // heat button height (was 60)
#define PRESET_ROW_Y     228   // 148 + 72 + 8 gap
#define PRESET_ROW_H     72    // preset row height
// BTN_W=110, BTN_GAP=8 unchanged — 4×110 + 5×8 = 480 ✓
```

Remove: `ARC_SIZE`, `ARC_WIDTH`, `ARC_Y_OFFSET`, `STATE_LBL_Y`,
`TEMP_Y`, `BTN_Y`, `BTN_H`, `STATUSBAR_Y`, `STATUSBAR_H`.

---

### 3. `gui.c` — Widget Variables

**Remove** (no longer needed):
```c
static lv_obj_t *s_arc;            // decorative arc — gone
static lv_obj_t *s_dot_relay_m;    // relay M dot — too technical
static lv_obj_t *s_dot_relay_a;    // relay A dot — too technical
static lv_obj_t *s_sched_bar;      // old schedule progress bar
static lv_obj_t *s_sched_bar_lbl;
```

**Add** (preset row machinery):
```c
// Preset row — two sub-containers, one shown at a time
static lv_obj_t *s_preset_normal;     // container: 4 preset/custom buttons
static lv_obj_t *s_preset_active;     // container: stop + progress info
static lv_obj_t *s_preset_btns[3];    // Slow Cook, Quick Warm, All Day
static lv_obj_t *s_preset_active_lbl; // "Slow Cook • Step 2/3 • 2h 30m left"
```

`s_lbl_state` and `s_lbl_temp` are kept but repositioned into the temp panel.

---

### 4. `gui.c` — New Callbacks

```c
// Tap a preset → start schedule + toast
static void preset_sched_cb(lv_event_t *e);   // user_data = crockpot_schedule_t*

// Tap Stop in active row → stop schedule + toast
static void preset_stop_cb(lv_event_t *e);

// Tap Custom → navigate to schedule builder
static void nav_custom_sched_cb(lv_event_t *e);
```

`preset_sched_cb` calls `crockpot_schedule_start()` and shows a 2s toast.
`preset_stop_cb` calls `crockpot_schedule_stop()` and shows a 2s toast.
Both call `wake()`.

---

### 5. `gui.c` — `create_main_screen()` Rewrite

**Top status strip** (y=0, h=28, full width):
- Background: COL_SURFACE
- WiFi icon left (tap → WiFi screen), colored by connection status
- Uptime label center (Montserrat 14, COL_TEXT_DIM)
- Settings button right (-48 offset, 28×28)
- Info button right (-8 offset, 28×28)

**Temperature panel** (y=28, h=112, full width):
- Background: COL_BG (same as screen, no box needed — clean look)
- `s_lbl_temp`: `lv_font_montserrat_40`, LV_ALIGN_CENTER offset (0, -16)
- `s_lbl_state`: `lv_font_montserrat_20`, LV_ALIGN_CENTER offset (0, +24), state color

**Heat buttons** (y=148, h=72): identical to current but taller (72 vs 60).
BTN_W=110, BTN_GAP=8, same active/inactive coloring. All 4 `heat_btn_cb` callbacks kept.

**Preset row container** (y=228, h=72, w=480):
Two absolutely-positioned child containers, same size:

*`s_preset_normal`* — 4 buttons at BTN_W=110, BTN_GAP=8:
- "Slow Cook" / "Quick Warm" / "All Day" / "Custom"
- Default: COL_SURFACE. Active preset (running): COL_ACCENT border highlight
- Callbacks: `preset_sched_cb` for first 3, `nav_custom_sched_cb` for "Custom"
- Font: Montserrat 14 (fits in 110px)

*`s_preset_active`* — initially HIDDEN:
- Stop button: 90×72px left, COL_ERROR background, "■ Stop", `preset_stop_cb`
- Progress label: fills remaining 390px, Montserrat 14, COL_TEXT
  Format: `"Name  •  Step X/Y  •  Xh YYm left"` (or no time if indefinite)

---

### 6. `gui.c` — `update_timer_cb()` Changes

**Remove:**
- Arc color update (arc gone)
- Relay dot color updates (dots gone)
- `s_sched_bar` show/hide logic

**Update (new logic):**
```c
// Preset row: toggle normal ↔ active
if (st.schedule_active) {
    lv_obj_add_flag(s_preset_normal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_preset_active, LV_OBJ_FLAG_HIDDEN);
    // Build label: "Slow Cook  •  Step 2/3  •  2h 30m left"
    // or "Slow Cook  •  Step 3/3" if remaining == 0 (indefinite)
    lv_label_set_text(s_preset_active_lbl, buf);
} else {
    lv_obj_clear_flag(s_preset_normal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_preset_active, LV_OBJ_FLAG_HIDDEN);
}

// Highlight the active preset button (if one of the 3 known presets)
// Compare st.schedule_name against preset names to find which btn to highlight
for (int i = 0; i < 3; i++) {
    bool active = st.schedule_active &&
                  strcmp(st.schedule_name, k_preset_names[i]) == 0;
    lv_obj_set_style_border_color(s_preset_btns[i],
        active ? COL_ACCENT : COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_preset_btns[i],
        active ? 2 : 1, LV_PART_MAIN);
}
```

Temperature and state label updates stay the same (just different alignment).
Heat button highlight logic stays the same.
WiFi / uptime / settings remain on the strip — update logic same as before.

---

### 7. Schedules Screen

`create_schedules_screen()` is kept and still created in `gui_init()` (no null pointer
risk from `gui_set_screen(GUI_SCREEN_SCHEDULES)`). It's just no longer reachable from
the main screen. All other screens (Settings, WiFi, Info, History, Schedule Builder)
remain unchanged and keep their navigation paths.

`GUI_SCREEN_SCHEDULES` stays in the enum — no API breakage.

---

### 8. Screensaver Screen

No layout changes needed — it's already minimal (centered temp + state, no controls).

---

## What Goes Away

| Removed | Reason |
|---------|--------|
| Arc indicator (140×140) | Pure decoration; state shown by buttons |
| Relay M/A dots | Too technical for kitchen use |
| Old schedule progress bar (between temp and buttons) | Replaced by preset row morphing |
| Schedules nav button in status bar | Presets now live on main screen |

---

## Verification

1. Build: `idf.py build` — check binary fits in 2MB partition (expect ~1.51MB)
2. Flash and confirm:
   - Temperature displays large (40pt) and centered
   - State label in correct color below temp
   - OFF/WARM/LOW/HIGH buttons all tappable, active one highlights
   - Tapping "Slow Cook" starts schedule and shows toast
   - Preset row morphs to stop+progress when schedule runs
   - Stop button stops schedule and row reverts to presets
   - "Custom" button opens schedule builder
   - Screensaver still dims/wakes correctly
   - WiFi/Settings/Info/History still reachable
