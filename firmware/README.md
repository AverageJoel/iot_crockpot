# IoT Crockpot Firmware

ESP-IDF firmware for the IoT Crockpot controller, targeting the custom single-board
ESP32-S3 PCB.

## Hardware Target

- **MCU**: ESP32-S3-WROOM-1-N4R2 (4MB flash, 2MB PSRAM)
- **Display**: 3.5" IPS SPI, ST7796 controller (480×320, capacitive touch)
- **Touch**: FT6336U I2C (on display module)
- **Temperature Sensor**: MAX31855 + K-type thermocouple (SPI)
- **Relay**: Omron G5LE-1 via 2N7002 MOSFET driver
- **USB**: Native USB Serial/JTAG (GPIO19/20, no bridge chip)

## Current Status

| Module | Status | Notes |
|--------|--------|-------|
| Core state machine | Done | OFF/WARM/LOW/HIGH, mutex-protected |
| MAX31855 thermocouple | Done | SPI driver, fault detection |
| Relay control | Done | Active-high/low configurable |
| WiFi | Done | NVS credentials, retry logic |
| Telegram bot | Done | Long-poll, commands, NVS token, chat ID whitelist |
| Telegram safety alerts | Done | Non-blocking queue, fires on auto-shutoff |
| NVS config | Done | Shared WiFi + Telegram storage |
| ST7796 display driver | Done | esp_lcd SPI driver |
| FT6336U touch driver | Done | esp_lcd_touch I2C driver |
| LVGL integration | Done | esp_lvgl_port |
| Touchscreen UI | Done | 4 screens, arc indicator, status bar, toast overlay |
| Task watchdog | Done | Control loop must tick every second |
| Boot log | Done | Chip info, IDF version, all GPIO pins |

## GPIO Assignments

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI SCK | 12 | Shared: display + MAX31855 |
| SPI MOSI | 11 | |
| SPI MISO | 13 | |
| LCD CS | 10 | ST7796 |
| LCD DC | 9 | |
| LCD RST | 8 | |
| LCD BL | 47 | PWM backlight |
| Touch SDA | 5 | FT6336U I2C |
| Touch SCL | 4 | |
| Touch RST | 6 | |
| Touch INT | 7 | |
| MAX31855 CS | 16 | Thermocouple |
| Relay | 17 | SSR/relay, active-high |
| USB D- | 19 | Fixed, native USB Serial/JTAG |
| USB D+ | 20 | Fixed, native USB Serial/JTAG |

All GPIO assignments are configurable via `idf.py menuconfig` → *IoT Crockpot*.

## Building

### Prerequisites

- [ESP-IDF v5.2+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)

### Build Commands

```bash
cd firmware

# Set target (one-time)
idf.py set-target esp32s3

# Configure GPIO pins, WiFi credentials, Telegram token
idf.py menuconfig

# Build
idf.py build

# Flash and monitor (native USB — no CH340 needed)
idf.py flash monitor
```

### Configuration (`idf.py menuconfig` → *IoT Crockpot*)

| Setting | Default |
|---------|---------|
| WiFi SSID / Password | (blank) |
| Telegram bot token | (blank) |
| Allowed Telegram chat ID | (blank = any) |
| All GPIO pin assignments | See table above |

Credentials are saved to NVS on first boot and persist across reflashes that don't erase NVS.

## Project Structure

```
firmware/
├── main/
│   ├── main.c              # Entry point, boot log, task creation
│   ├── crockpot.c/.h       # Core state machine (OFF/WARM/LOW/HIGH)
│   ├── temperature.c/.h    # MAX31855 SPI driver, thermocouple reading
│   ├── relay.c/.h          # GPIO relay/SSR control
│   ├── wifi.c/.h           # WiFi connection management
│   ├── telegram.c/.h       # Telegram bot (long-poll, commands, alerts)
│   ├── nvs_config.c/.h     # NVS read/write helpers
│   ├── display.c/.h        # LVGL init, lvgl_port setup
│   ├── display_driver.c/.h # ST7796 SPI driver (esp_lcd)
│   ├── touch_driver.c/.h   # FT6336U I2C driver (esp_lcd_touch)
│   ├── spi_bus.c/.h        # Shared SPI2 bus init
│   ├── gui.c/.h            # LVGL screens and widgets
│   ├── Kconfig.projbuild   # GPIO and WiFi/Telegram config options
│   └── idf_component.yml   # LVGL + esp_lcd component dependencies
├── CMakeLists.txt
└── sdkconfig.defaults      # Default config (fonts, watchdog, etc.)
```

## Telegram Commands

| Command | Description |
|---------|-------------|
| `/status` | Current state, temperature, uptime, WiFi |
| `/off` | Turn off |
| `/warm` | Set to warm |
| `/low` | Set to low |
| `/high` | Set to high |
| `/help` | List commands |

Safety events (temperature limit, sensor error) automatically send an alert to the
configured chat ID.

See [docs/telegram_setup.md](../docs/telegram_setup.md) for bot creation steps.

## Safety Features

- Auto-shutoff above 300°F (configurable in source)
- Persistent sensor error shutoff (10 consecutive bad readings while heating)
- Relay wired normally-open — de-energizes to OFF on any fault or power loss
- Task watchdog timer: control loop must tick every second or chip resets
- Safety alerts sent via Telegram on any automatic shutoff
