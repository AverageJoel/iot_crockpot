# Firmware Architecture

ESP-IDF v5.x firmware for the IoT Crockpot. Target: ESP32-S3-WROOM-1-N4R2.

---

## Module Overview

```
main.c
  ├── wifi.c/.h           WiFi STA connection, NVS credentials
  ├── crockpot.c/.h       Core state machine, safety shutoff, alert callback
  │     ├── temperature.c/.h   MAX31855 SPI driver (via spi_bus)
  │     └── relay.c/.h         GPIO relay/SSR output
  ├── telegram.c/.h       Telegram bot long-poll, commands, alert queue
  ├── nvs_config.c/.h     Shared NVS read/write helpers
  └── display.c/.h        LVGL init and lvgl_port setup
        ├── spi_bus.c/.h        Shared SPI2 bus (display + MAX31855)
        ├── display_driver.c/.h ST7796 panel via esp_lcd
        ├── touch_driver.c/.h   FT6336U touch via esp_lcd_touch_ft5x06
        └── gui.c/.h            LVGL screens and widgets
```

Dependencies are one-directional. `crockpot.c` does not import `telegram.h` or `gui.h` — it exposes a callback that `main.c` connects to both.

---

## FreeRTOS Tasks

| Task | Function | Stack | Priority | Notes |
|------|----------|-------|----------|-------|
| `app_main` | `app_main()` | ESP-IDF default | 1 | Runs init sequence, then periodic 30s status log |
| `control` | `crockpot_control_task()` | 4 KB | 5 | 1 Hz loop; reads temperature, runs safety checks, feeds TWDT |
| `telegram` | `telegram_task()` | 8 KB | 3 | Long-polls Telegram API, processes commands, drains alert queue |
| `display` | `display_task()` | 4 KB | 4 | LVGL port internal task (started by `lvgl_port_init`) |

The LVGL task is managed internally by `esp_lvgl_port`. External tasks (e.g., the control task via the alert callback) must acquire the LVGL lock (`lvgl_port_lock` / `lvgl_port_unlock`) before calling any LVGL API.

---

## Display Stack

```
spi_bus.c
  └── spi_bus_init()
        Initializes SPI2_HOST with SCK/MOSI/MISO.
        Called once; shared by display driver and MAX31855.

display_driver.c
  └── display_driver_init()
        Creates esp_lcd_io_handle_t (SPI IO) for ST7796.
        Creates esp_lcd_panel_handle_t via esp_lcd_new_panel_st7796().
        Resets, inits, and turns on panel.
        Configures landscape rotation (480×320).
        Sets up PWM backlight on LCD_BL GPIO.

touch_driver.c
  └── touch_driver_init()
        Initializes I2C master bus on SDA/SCL.
        Creates esp_lcd_touch_handle_t via esp_lcd_touch_new_i2c_ft5x06().

display.c
  └── display_init()
        Calls spi_bus_init(), display_driver_init(), touch_driver_init().
        Calls lvgl_port_init() — starts LVGL internal task.
        Registers display flush callback (lvgl_port_add_disp).
        Registers touch read callback (lvgl_port_add_touch).
        Returns esp_lcd_panel_handle_t and esp_lcd_touch_handle_t
        via display_get_lvgl_disp() / display_get_lvgl_indev().

gui.c
  └── gui_init()
        Builds all LVGL screen objects (main, settings, WiFi, info).
        Starts a 500ms lv_timer for status updates.
  └── gui_start()
        Loads the main screen.
```

---

## GUI Screens

Layout: 480×320 landscape, dark theme (`#1a1a2e` background).

### Main Screen

```
┌─────────────────────────────────────────────────────┐
│                                                     │
│              ◉  LOW                                 │
│         (270° arc, color-coded by state)            │
│                                                     │
│              185.4 °F                               │
│                                                     │
│   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐   │
│   │  OFF   │  │  WARM  │  │  LOW   │  │  HIGH  │   │
│   └────────┘  └────────┘  └────────┘  └────────┘   │
│                                                     │
│  WiFi ●      00:42:15              ⚙           ℹ   │
└─────────────────────────────────────────────────────┘
```

| State | Arc / button color |
|-------|--------------------|
| OFF | Gray `#555555` |
| WARM | Yellow `#ffaa00` |
| LOW | Orange `#ff6600` |
| HIGH | Red `#ff2200` |

- WiFi icon is tappable → navigates to WiFi screen
- Settings button → Settings screen
- Info button → Info screen
- Status updated every 500ms via `lv_timer`

### Settings Screen
- Celsius/Fahrenheit toggle

### WiFi Screen
- Connection state, SSID, IP address

### Info Screen
- Uptime (H:M:S), firmware version

### Toast Overlay
Displayed on `lv_layer_top()` so it floats over all screens.
- Blue background for normal messages (`gui_show_message`)
- Red background for errors (`gui_show_error`)
- Tap to dismiss; auto-dismisses after `duration_ms` for normal messages
- Error toasts persist until dismissed

---

## Core State Machine (`crockpot.c`)

States: `CROCKPOT_OFF`, `CROCKPOT_WARM`, `CROCKPOT_LOW`, `CROCKPOT_HIGH`

All state and status fields are protected by `s_state_mutex` (FreeRTOS mutex). `crockpot_get_status()` and `crockpot_set_state()` are both thread-safe.

### Control Loop (1 Hz)

Each iteration:
1. Read thermocouple (SPI, before taking mutex — ~1ms)
2. Acquire mutex
3. Update `s_status.temperature_f`, `.sensor_error`, `.uptime_seconds`, `.wifi_connected`
4. Safety check — temperature limit:
   - If `was_heating && temp > 300°F` → set state=OFF, `relay_all_off()`, set `temp_shutoff` flag
5. Safety check — sensor error:
   - If `was_heating && sensor_error` → increment `sensor_error_count`
   - If `sensor_error_count > 10` → set state=OFF, `relay_all_off()`, set `sensor_shutoff` flag, reset count
6. Release mutex
7. Fire alert callbacks outside mutex (safe to call `crockpot_get_status()` from handler)
8. Feed task watchdog (`esp_task_wdt_reset()`)
9. Wait until next 1s tick (`vTaskDelayUntil`)

### Safety Alert Callback

```c
// Register in main.c:
crockpot_set_alert_callback(on_safety_alert);

// Callback signature:
typedef void (*crockpot_alert_cb_t)(crockpot_alert_t reason, float temp_f);

// Reasons:
CROCKPOT_ALERT_TEMP_LIMIT    // temperature exceeded 300°F
CROCKPOT_ALERT_SENSOR_ERROR  // 10+ consecutive bad readings while heating
```

The callback fires only on the heating→OFF transition, not every loop iteration.
`main.c` wires it to both `telegram_queue_alert()` and `gui_show_error()`.

---

## Telegram Integration

### Long Polling

`telegram_task()` calls `getUpdates` with a 30-second timeout. After each response:
1. Parses JSON, processes any commands
2. Drains the alert queue (sends any pending safety alerts)
3. 100ms delay, then next poll

### Commands

Inbound commands (`/status`, `/off`, `/warm`, `/low`, `/high`, `/help`) are processed in `process_command()`. The chat ID whitelist (`CONFIG_CROCKPOT_TELEGRAM_ALLOWED_CHAT_ID`) is enforced on every incoming message; leave blank to allow any chat ID.

### Alert Queue

For outbound alerts from other tasks (e.g., safety shutoffs from the control task):

```c
// Non-blocking — safe from any FreeRTOS task:
telegram_queue_alert("SAFETY SHUTOFF: Temperature 305.2 F exceeded limit.");
```

- Queue depth: 4 messages × 128 bytes
- `xQueueSend` with zero timeout — drops silently if full
- Drained by `telegram_task()` after each getUpdates cycle
- Alert chat ID is the same as `CONFIG_CROCKPOT_TELEGRAM_ALLOWED_CHAT_ID`

### Token and Chat ID Storage

Priority at init: NVS → Kconfig default. `telegram_set_token()` writes to NVS for persistence.

---

## NVS Config (`nvs_config.c`)

Thin wrappers over `nvs_flash` for string read/write:

```c
nvs_config_read_str(NVS_NS_WIFI, NVS_KEY_WIFI_SSID, buf, len);
nvs_config_write_str(NVS_NS_TELEGRAM, NVS_KEY_TG_TOKEN, token);
```

Namespaces: `NVS_NS_WIFI` (`"wifi"`), `NVS_NS_TELEGRAM` (`"telegram"`).

---

## Task Watchdog

The control task registers with the ESP Task Watchdog Timer at startup:

```c
esp_task_wdt_add(NULL);   // register current task
// ... each loop iteration:
esp_task_wdt_reset();
```

TWDT is configured in `sdkconfig.defaults`:
```
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

If the control loop hangs for >10 seconds, the chip resets via panic handler.

---

## Build Configuration (`sdkconfig.defaults`)

Key non-default settings:

```
# Target
CONFIG_IDF_TARGET="esp32s3"

# PSRAM (OPI PSRAM on N4R2)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y

# LVGL fonts
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y

# Task watchdog
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

GPIO defaults are set in `Kconfig.projbuild` (not `sdkconfig.defaults`) so they appear in `menuconfig`.

---

## Component Dependencies (`idf_component.yml`)

```yaml
dependencies:
  lvgl/lvgl: "^9.0.0"
  espressif/esp_lvgl_port: "^2.0.0"
  espressif/esp_lcd_st7796: "*"
  espressif/esp_lcd_touch_ft5x06: "*"
```

`esp_lcd` and `esp_lcd_touch` are part of ESP-IDF v5 and do not need separate component entries.
