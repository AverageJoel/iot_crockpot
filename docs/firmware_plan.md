# Firmware Plan

*Written February 2026 — covers remaining work to get the single-board ESP32-S3 design to a working state.*

> **Status: ALL PHASES COMPLETE** (February 2026)
> See [firmware_architecture.md](firmware_architecture.md) for documentation of the implemented system.

---

## Final State

All 7 phases implemented. Firmware is feature-complete pending hardware bring-up.

| File | State | Notes |
|------|-------|-------|
| `crockpot.c` | **Done** | State machine, mutex, safety shutoff, sensor watchdog, alert callback, TWDT |
| `temperature.c` | **Done** | MAX31855 SPI driver, fault decoding |
| `relay.c` | **Done** | GPIO relay control, active-high/low configurable via Kconfig |
| `wifi.c` | **Done** | STA mode, event-driven, retry logic, NVS credentials |
| `telegram.c` | **Done** | Long-polling, commands, NVS token, chat ID whitelist, alert queue |
| `nvs_config.c` | **Done** | Shared NVS helpers for wifi + telegram |
| `spi_bus.c` | **Done** | Shared SPI2 bus init |
| `display_driver.c` | **Done** | ST7796 SPI driver via esp_lcd |
| `touch_driver.c` | **Done** | FT6336U I2C driver via esp_lcd_touch_ft5x06 |
| `display.c` | **Done** | LVGL init, lvgl_port setup, display + touch registration |
| `gui.c` | **Done** | Full LVGL UI: 4 screens, arc indicator, buttons, status bar, toast overlay |
| `Kconfig.projbuild` | **Done** | All GPIO and credential config options |
| `idf_component.yml` | **Done** | LVGL v9, esp_lvgl_port v2, esp_lcd_st7796, esp_lcd_touch_ft5x06 |
| `display_hal.c/.h` | **Deleted** | Replaced by LVGL + esp_lcd |
| `touch_hal.c/.h` | **Deleted** | Replaced by esp_lcd_touch |

---

## Original State Assessment (at plan time)

### What was implemented and working

| File | State | Notes |
|------|-------|-------|
| `crockpot.c` | **Done** | State machine, mutex, safety shutoff, sensor watchdog |
| `temperature.c` | **Done** | Full MAX31855 SPI driver, fault decoding |
| `relay.c` | **Done** | GPIO relay control, active-high/low configurable |
| `wifi.c` | **Done** | STA mode, event-driven, retry logic, IP retrieval |
| `telegram.c` | **Done** | Long-polling, all commands (/status /off /warm /low /high), sendMessage |
| `gui.c` | **Good structure** | Full UI logic: 4 screens, touch zones, dimming, themes — but calls stub HAL |
| `display.c` | **Partial** | Physical button ISR works; display rendering is a stub |
| `display_hal.c` | **Stub** | All TODO — no real SPI driver |
| `touch_hal.c` | **Stub** | All TODO — returns no events |

### Known TODOs / gaps

1. **NVS storage** — WiFi SSID/password and Telegram token are both commented `// TODO: Load from NVS`. Currently falls back to `sdkconfig` defaults, so first-time provisioning requires a reflash.
2. **GPIO pin assignments are stale** — `relay.h` and `temperature.h` reference "XIAO ESP32-C3" pin numbers. Need updating for the ESP32-S3 single-board design.
3. **Display stack is completely unimplemented** — `display_hal.c` and `touch_hal.c` are empty stubs. No pixels will appear on screen.
4. **`uart_master.c` was never written** — the STM32 UART layer from the old two-board design was never implemented. This is fine; the architecture has moved to single-board and that layer is no longer needed.
5. **WARM/LOW/HIGH are all identical** — `relay_apply_state()` turns one relay ON for all three heat states. Differentiation (e.g., duty-cycle PWM) is a future enhancement.
6. **No watchdog configured** — `esp_task_wdt` is imported in `main.c` but never initialized. Safety relies solely on software checks in the control loop.

---

## Architecture Decision: Adopt LVGL

The existing `display_hal.h` is a custom drawing API that re-invents what LVGL already provides — and better. Rather than implementing that stub from scratch, replace it with LVGL.

**LVGL** (Light and Versatile Graphics Library) is the de-facto embedded UI library for ESP32, with first-class ESP-IDF support via Espressif's component ecosystem:

| Component | Role |
|-----------|------|
| `lvgl/lvgl` | Core graphics library |
| `espressif/esp_lvgl_port` | Bridges LVGL to ESP-IDF (tick timer, display flush, touch read) |
| `espressif/esp_lcd_st7796` (or built-in) | SPI display panel driver for ST7796 |
| `espressif/esp_lcd_touch_ft5x06` | I2C touch driver — FT6336U is FT5x06 family-compatible |

ESP-IDF v5.x includes `esp_lcd` natively. `esp_lcd_new_panel_st7796()` and the FT5x06 touch driver are available via `idf_component_manager` (`idf_component.yml`).

### What changes with LVGL

| Old | New |
|-----|-----|
| `display_hal.c` (stub) | Deleted — replaced by `esp_lcd` ST7796 flush driver |
| `touch_hal.c` (stub) | Deleted — replaced by `esp_lcd_touch` FT6336U driver |
| `display_hal.h` | Deleted — LVGL is the abstraction layer |
| `touch_hal.h` | Deleted |
| `gui.c` | Rewritten using LVGL widgets (`lv_label`, `lv_btn`, `lv_arc`, etc.) |
| `display.c` | Becomes the LVGL init + display task entry point |

`crockpot.c`, `temperature.c`, `relay.c`, `wifi.c`, and `telegram.c` are **not affected**.

---

## Proposed GPIO Assignments (ESP32-S3-WROOM-1-N4R2)

*Subject to PCB layout review — finalize when schematic is started.*

Constraints:
- GPIO19/20 are fixed for USB D-/D+
- GPIO26-32: PSRAM (N4R2 variant) — avoid
- GPIO33-37: Flash — avoid
- GPIO0, GPIO3, GPIO45, GPIO46: strapping pins — avoid or use carefully

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI SCK | 12 | Shared bus (LCD + SD + MAX31855) |
| SPI MOSI | 11 | Shared |
| SPI MISO | 13 | Shared |
| LCD_CS | 10 | ST7796 chip select |
| LCD_DC | 9 | Command/data select |
| LCD_RST | 8 | Can tie to EN via RC to save pin |
| LCD_BL | 47 | Backlight PWM (or tie to 3.3V, always on) |
| SD_CS | 2 | Optional (future use) |
| Touch SDA | 5 | I2C, FT6336U |
| Touch SCL | 4 | I2C, FT6336U |
| CTP_RST | 6 | Can tie high via 10K to save pin |
| CTP_INT | 7 | Touch interrupt (or poll, saves pin) |
| MAX31855_CS | 16 | Thermocouple SPI chip select |
| Relay | 17 | SSR/relay drive output |
| USB D- | 19 | Fixed by ESP32-S3 |
| USB D+ | 20 | Fixed by ESP32-S3 |
| BOOT | 0 | Standard ESP32-S3 boot button |

GPIO budget: 15 signals used, ~18 spare (1, 14, 15, 18, 21, 38-44, 48).

---

## Phase Plan

### Phase 1 — GPIO Pin Assignments & Kconfig ✓ DONE

**Goal:** Replace hardcoded XIAO ESP32-C3 pin numbers with correct ESP32-S3 values, moved to `Kconfig` so they're configurable without touching source code.

**Files to change:**
- `temperature.h` — replace hardcoded `MAX31855_PIN_*` defines with `CONFIG_*` Kconfig symbols
- `relay.h` — replace `RELAY_MAIN_GPIO` / `RELAY_AUX_GPIO` with Kconfig symbols
- New file: `Kconfig.projbuild` (or `main/Kconfig`) — defines all GPIO config options with sensible defaults

**Kconfig sections needed:**
```
menu "IoT Crockpot Hardware"

    menu "SPI Bus"
        config CROCKPOT_SPI_SCK ... default 12
        config CROCKPOT_SPI_MOSI ... default 11
        config CROCKPOT_SPI_MISO ... default 13
    endmenu

    menu "Display (ST7796)"
        config CROCKPOT_LCD_CS ... default 10
        config CROCKPOT_LCD_DC ... default 9
        config CROCKPOT_LCD_RST ... default 8
        config CROCKPOT_LCD_BL ... default 47
    endmenu

    menu "Touch (FT6336U)"
        config CROCKPOT_TOUCH_SDA ... default 5
        config CROCKPOT_TOUCH_SCL ... default 4
        config CROCKPOT_TOUCH_RST ... default 6
        config CROCKPOT_TOUCH_INT ... default 7
    endmenu

    menu "MAX31855 Thermocouple"
        config CROCKPOT_TC_CS ... default 16
    endmenu

    menu "Relay"
        config CROCKPOT_RELAY_GPIO ... default 17
    endmenu

endmenu
```

---

### Phase 2 — NVS Provisioning ✓ DONE

**Goal:** WiFi credentials and Telegram token loaded from NVS at boot, with a provisioning flow when not set.

**Files to change:**
- `wifi.c` — `wifi_connect()` reads SSID/password from NVS; `wifi_set_credentials()` writes to NVS
- `telegram.c` — `telegram_init()` reads token from NVS; `telegram_set_token()` writes to NVS

**Provisioning flow options (pick one):**

| Option | Pros | Cons |
|--------|------|------|
| **Serial provisioning** | Simple — send JSON over USB serial at first boot | Requires USB cable |
| **Touchscreen provisioning** | No cable needed, on-device UI | Needs keyboard widget (complex) |
| **Hardcoded in sdkconfig** | Fastest to get running | Credentials in source tree, need reflash to change |

**Recommendation for now:** Hardcode in `sdkconfig` via `menuconfig` (already partially working), and add NVS write/read as a Phase 2 polish item. The TODOs are already in place.

**New file:** `nvs_config.c/.h` — shared NVS helper (open namespace, read/write string wrappers) used by both `wifi.c` and `telegram.c`.

---

### Phase 3 — ST7796 SPI Display Driver ✓ DONE

**Goal:** Implement a real display driver using `esp_lcd`.

**Approach:** Use `esp_lcd_new_panel_st7796()` from `espressif/esp_lcd_st7796` IDF component. This provides the init sequence and pixel write commands; we just configure the SPI bus and panel.

**Key steps:**
1. Create SPI bus (`spi_bus_initialize`) — shared with MAX31855
2. Create `esp_lcd_io_handle_t` for the ST7796 panel over SPI
3. Call `esp_lcd_new_panel_st7796()` to get a `esp_lcd_panel_handle_t`
4. Run panel init sequence, set orientation (landscape: 480×320)
5. Expose a `flush_cb` function that LVGL calls when it wants to push pixels

**File:** `display_hal.c` is deleted. A new `display_driver.c` (or the logic moves into `display.c`) handles the above.

**ST7796 SPI config notes:**
- Clock: up to 80 MHz supported, start at 40 MHz for stability
- Mode: SPI Mode 0 (CPOL=0, CPHA=0)
- Shares SPI bus with MAX31855 (different CS pins, no conflict)
- MAX31855 is SPI Mode 0 as well — no mode-switching needed

---

### Phase 4 — FT6336U Touch Driver ✓ DONE

**Goal:** Read touch coordinates from the FT6336U over I2C.

**Approach:** Use `espressif/esp_lcd_touch_ft5x06` IDF component. The FT6336U is register-compatible with the FT5x06 family.

**Key steps:**
1. Create I2C bus (`i2c_master_init`) on SDA/SCL pins
2. Call `esp_lcd_touch_new_i2c_ft5x06()` to get a `esp_lcd_touch_handle_t`
3. Expose a `read_cb` function that LVGL calls for touch input

**FT6336U I2C notes:**
- Address: 0x38
- CTP_INT pin: configure as GPIO input interrupt for efficient polling (vs. polling in a tight loop)
- CTP_RST: pulse low to reset on init

---

### Phase 5 — LVGL Integration ✓ DONE

**Goal:** Wire the ST7796 display driver and FT6336U touch driver into LVGL using `esp_lvgl_port`.

**Steps:**
1. Add `idf_component.yml` to declare dependencies:
   ```yaml
   dependencies:
     lvgl/lvgl: "^9.0.0"
     espressif/esp_lvgl_port: "^2.0.0"
     espressif/esp_lcd_st7796: "*"
     espressif/esp_lcd_touch_ft5x06: "*"
   ```
2. Init LVGL via `lvgl_port_init()`
3. Register display: `lvgl_port_add_disp()` with the ST7796 panel handle and a DMA draw buffer
4. Register touch: `lvgl_port_add_touch()` with the FT6336U touch handle
5. LVGL tick is handled automatically by `esp_lvgl_port`

**Draw buffer sizing:**
- Full-screen buffer (480×320×2 = 300KB) is too large for internal RAM
- Use partial buffer: 1/10 screen = 480×32×2 = ~30KB (fits in internal SRAM)
- PSRAM option: 2MB PSRAM on N4R2 — could use full double-buffer for tear-free rendering

**File changes:**
- `display.c` — becomes the LVGL init + `lvgl_port` setup; starts LVGL task
- `display_hal.h/.c` — deleted
- `touch_hal.h/.c` — deleted
- `CMakeLists.txt` — remove `display_hal.c`, `touch_hal.c`; add `idf_component.yml`

---

### Phase 6 — GUI Rewrite with LVGL ✓ DONE

**Goal:** Rewrite `gui.c` using LVGL widgets, replacing the custom drawing primitives.

**Main screen layout (480×320, landscape):**

```
┌─────────────────────────────────────────────────────┐
│                                                     │
│              ◉  LOW                                 │
│         (state indicator arc)                       │
│                                                     │
│              142.5 °F                               │
│                                                     │
│   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐   │
│   │  OFF   │  │  WARM  │  │  LOW   │  │  HIGH  │   │
│   └────────┘  └────────┘  └────────┘  └────────┘   │
│                                                     │
│  WiFi ●           00:42          ⚙  ℹ              │
└─────────────────────────────────────────────────────┘
```

**LVGL widgets to use:**

| UI Element | LVGL Widget |
|------------|-------------|
| State display | `lv_label` + `lv_arc` (color coded) |
| Temperature | `lv_label` (large font) |
| Heat buttons (OFF/WARM/LOW/HIGH) | `lv_btn` array, highlight active |
| Status bar | `lv_label` (WiFi, uptime) |
| Settings icon | `lv_btn` → navigate to settings screen |
| Screen transitions | `lv_screen_load_anim()` |
| Message toasts | `lv_msgbox` or custom `lv_obj` popup |

**Screens:**
1. **Main** — temperature + state display + heat level buttons
2. **Settings** — C/F toggle, brightness slider, screen timeout
3. **WiFi** — connection status, SSID, IP address
4. **Info** — uptime, firmware version

**Theme:** Dark background, color-coded state (gray=OFF, yellow=WARM, orange=LOW, red=HIGH).

**File changes:**
- `gui.c` — complete rewrite using LVGL API
- `gui.h` — simplify public API (mostly `gui_init`, `gui_start`, `gui_show_message`)
- `display.c` — remove old button ISR if physical buttons not needed; or keep as an input device registered with LVGL

---

### Phase 7 — Watchdog & Hardening ✓ DONE

**Goal:** Add remaining safety infrastructure.

1. **Task watchdog timer** — enable `esp_task_wdt`, register control task. If control loop hangs for >5s, trigger restart.
2. **Error counts / Telegram alerts** — send a Telegram message on auto-shutoff event (temperature limit hit, sensor error shutoff). Already have the infrastructure; just need to hook it up in `crockpot.c`.
3. **Relay active-low option** — verify `RELAY_ACTIVE_HIGH` matches actual hardware (SSR modules are often active-low).
4. **Boot log** — log chip info, IDF version, firmware version, and all configured GPIO pins at startup. Helpful for debugging.

---

## Summary: Files Affected

| File | Action |
|------|--------|
| `main/Kconfig` | **New** — GPIO config options |
| `nvs_config.c/.h` | **New** — NVS helpers for wifi + telegram |
| `display_driver.c/.h` | **New** — ST7796 SPI init via esp_lcd |
| `touch_driver.c/.h` | **New** — FT6336U I2C init via esp_lcd_touch |
| `idf_component.yml` | **New** — LVGL + esp_lvgl_port + driver deps |
| `display.c` | **Rewrite** — LVGL init, lvgl_port setup, task |
| `gui.c` | **Rewrite** — LVGL widgets instead of display_hal calls |
| `gui.h` | **Simplify** — trim public API |
| `temperature.h` | **Update** — GPIO defines → Kconfig symbols |
| `relay.h` | **Update** — GPIO defines → Kconfig symbols |
| `wifi.c` | **Update** — NVS credential load/store |
| `telegram.c` | **Update** — NVS token load/store |
| `CMakeLists.txt` | **Update** — add new files, remove deleted files |
| `display_hal.c/.h` | **Delete** — replaced by LVGL |
| `touch_hal.c/.h` | **Delete** — replaced by esp_lcd_touch |
| `interface_blynk.c/.h` | **Defer** — Blynk interface, not needed for MVP |

---

## Open Questions

1. **GPIO pin assignments** — Proposed above, but finalize against the PCB schematic when started. Kconfig makes it easy to change without source edits.
2. **LVGL version** — v8 vs v9. v9 has API changes. `esp_lvgl_port` v2.x supports LVGL v9. Check latest stable version at implementation time.
3. **PSRAM for display buffer** — The N4R2 has 2MB PSRAM. Using it for a full double-buffer (2 × 300KB) would eliminate tearing. Trade-off: slightly more complex init. Recommended to try.
4. **Physical buttons** — `display.c` has a working GPIO ISR for up/down/select buttons. With a touchscreen these are redundant, but keeping them as a fallback is low cost. Decide at PCB layout time whether to include them.
5. **WiFi provisioning UX** — How should a user configure WiFi credentials on a fresh board? Options: serial JSON, BLE provisioning, or touchscreen keyboard. Defer to after MVP is working.
6. **Telegram security** — Currently any chat ID can control the crockpot. A whitelist of allowed chat IDs should be added (store in NVS alongside the token).
