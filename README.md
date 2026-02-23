# IoT Crockpot Controller

An open-source, internet-enabled crockpot controller with a local touchscreen UI and remote Telegram control. Built on a custom single-board design around the ESP32-S3-WROOM-1-N4R2 and a 3.5" IPS capacitive touch display.

## Features

- **Remote Control via Telegram** — Control and monitor from anywhere; automatic safety alerts sent to Telegram on shutoff events
- **3.5" IPS Capacitive Touchscreen** — Full-color LVGL UI with OFF/WARM/LOW/HIGH controls, temperature arc, and multi-screen navigation
- **K-Type Thermocouple** — Accurate temperature monitoring via MAX31855
- **Safety System** — Auto-shutoff at 300°F, persistent sensor-error shutoff, task watchdog timer, relay fails to OFF on any fault
- **NVS Configuration** — WiFi credentials and Telegram token stored in non-volatile flash; configurable via `idf.py menuconfig`

## Architecture

Single custom PCB — ESP32-S3 handles everything directly:

```
                    ┌───────────────────────────────────────┐
                    │        ESP32-S3-WROOM-1-N4R2          │
                    │                                       │
   USB-C ──────────►│ GPIO19/20 (USB Serial/JTAG)           │
                    │                                       │
   3.5" Display ───►│ SPI2: SCK/MOSI/MISO + LCD_CS/DC/RST  │
   FT6336U Touch ──►│ I2C:  SDA/SCL + CTP_RST/INT          │
   Backlight PWM ───►│ GPIO47 (LCD_BL)                      │
                    │                                       │
   MAX31855 TC ─────►│ SPI2: MISO/SCK + TC_CS (GPIO16)      │
   Relay/SSR ───────►│ GPIO17                               │
                    │                                       │
                    │  WiFi 802.11n ──────────────► Internet│
                    └───────────────────────────────────────┘

Power: AC Mains → HLK-5M05 (5V) → AP2112K (3.3V) → ESP32 + Display + TC
```

See [docs/hardware_decisions.md](docs/hardware_decisions.md) for the full design rationale and component selection.

## Project Structure

```
iot_crockpot/
├── firmware/
│   ├── main/
│   │   ├── main.c              # Entry point, boot log, task creation
│   │   ├── crockpot.c/.h       # Core state machine (OFF/WARM/LOW/HIGH)
│   │   ├── temperature.c/.h    # MAX31855 SPI driver, thermocouple reading
│   │   ├── relay.c/.h          # GPIO relay/SSR control
│   │   ├── wifi.c/.h           # WiFi connection management
│   │   ├── telegram.c/.h       # Telegram bot (long-poll, commands, alerts)
│   │   ├── nvs_config.c/.h     # NVS read/write helpers
│   │   ├── display.c/.h        # LVGL init, lvgl_port setup
│   │   ├── display_driver.c/.h # ST7796 SPI driver (esp_lcd)
│   │   ├── touch_driver.c/.h   # FT6336U I2C driver (esp_lcd_touch)
│   │   ├── spi_bus.c/.h        # Shared SPI2 bus init
│   │   ├── gui.c/.h            # LVGL screens and widgets
│   │   ├── Kconfig.projbuild   # GPIO and WiFi/Telegram config options
│   │   └── idf_component.yml   # LVGL + esp_lcd component dependencies
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults      # Default config (fonts, watchdog, etc.)
├── hardware/                   # KiCad PCB design (in progress)
│   ├── *.kicad_sch
│   ├── *.kicad_pcb
│   └── production/
├── docs/                       # Design docs and guides
│   ├── firmware_architecture.md
│   ├── firmware_plan.md
│   ├── hardware_decisions.md
│   ├── telegram_setup.md
│   ├── wiring.md
│   └── assembly.md
└── simulator/                  # Python simulator (partial)
```

## Hardware

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-S3-WROOM-1-N4R2 | 4MB flash, 2MB PSRAM, LCSC C2913203 |
| Display | 3.5" IPS SPI, ST7796 controller | 480×320, 16.7M colors |
| Touch | FT6336U (on display module) | I2C, 2-point capacitive |
| Thermocouple | MAX31855 + K-type probe | SPI, ±2°C accuracy |
| Relay | Omron G5LE-1 (10A SPDT) | NO contact, fail-safe OFF |
| Power supply | HLK-5M05 | AC-DC, 5V 5W |
| 3.3V LDO | AP2112K-3.3TRG1 | 5V→3.3V for ESP32 and peripherals |
| Input protection | 0.5A slow-blow fuse + MOV | Per HLK-5M05 datasheet |

## Getting Started

### Prerequisites

- [ESP-IDF v5.2+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)

### Build and Flash

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig        # Configure WiFi, Telegram token, GPIO pins
idf.py build
idf.py flash monitor
```

### Configuration (`idf.py menuconfig` → *IoT Crockpot*)

| Setting | Where | Default |
|---------|-------|---------|
| WiFi SSID / Password | IoT Crockpot → WiFi | (blank) |
| Telegram bot token | IoT Crockpot → Telegram | (blank) |
| Allowed Telegram chat ID | IoT Crockpot → Telegram | (blank = any) |
| All GPIO pin assignments | IoT Crockpot → SPI Bus / Display / Touch / Relay | See table below |

Credentials are saved to NVS on first boot and persist across reflashes that don't erase NVS.

### Default GPIO Assignments

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
| USB D- | 19 | Fixed, native USB |
| USB D+ | 20 | Fixed, native USB |

## Telegram Commands

| Command | Description |
|---------|-------------|
| `/status` | Current state, temperature, uptime, WiFi |
| `/off` | Turn off |
| `/warm` | Set to warm |
| `/low` | Set to low |
| `/high` | Set to high |
| `/help` | List commands |

Safety events (temperature limit, sensor error) automatically send an alert to the configured chat ID without requiring a command.

See [docs/telegram_setup.md](docs/telegram_setup.md) for bot creation and configuration steps.

## Safety

- Auto-shutoff above 300°F (configurable in source)
- Persistent sensor error shutoff (10 consecutive bad readings while heating)
- Relay wired normally-open — de-energizes to OFF on any fault, power loss, or firmware crash
- Task watchdog timer: control loop must tick every second or the chip resets
- Safety alerts sent via Telegram on any automatic shutoff

**WARNING:** This project involves mains voltage. Only attempt if you have relevant electrical experience. Always follow local electrical codes and safety practices.

## Development Status

### Firmware
- [x] Core state machine (OFF/WARM/LOW/HIGH, mutex-protected)
- [x] MAX31855 thermocouple SPI driver
- [x] Relay/SSR control (active-high/low configurable)
- [x] WiFi connection management (NVS credentials, retry logic)
- [x] Telegram bot (long-poll, commands, NVS token, chat ID whitelist)
- [x] Telegram safety alerts (non-blocking queue, fires on auto-shutoff)
- [x] NVS config helpers (shared wifi + telegram storage)
- [x] ST7796 SPI display driver (esp_lcd)
- [x] FT6336U I2C touch driver (esp_lcd_touch)
- [x] LVGL integration (esp_lvgl_port)
- [x] Full touchscreen UI (4 screens, arc indicator, status bar, toast overlay)
- [x] Task watchdog timer (esp_task_wdt)
- [x] Boot log (chip info, IDF version, all GPIO pins)

### Hardware (PCB)
- [x] Architecture finalized (single-board ESP32-S3, ST7796 SPI display)
- [x] AC supply section (fuse, MOV, HLK-5M05)
- [x] Relay section (G5LE-1, 2N7002 driver, flyback diode)
- [ ] ESP32-S3 MCU section (in progress)
- [ ] USB-C connector
- [ ] AP2112K-3.3 LDO
- [ ] Display header (14-pin)
- [ ] MAX31855 thermocouple
- [ ] PCB layout
- [ ] Gerber generation
- [ ] Prototype build and validation

## Documentation

| Document | Description |
|----------|-------------|
| [firmware_architecture.md](docs/firmware_architecture.md) | Firmware module breakdown, task structure, LVGL stack |
| [hardware_decisions.md](docs/hardware_decisions.md) | Component selection rationale, ESP32-S3 vs alternatives |
| [schematic_rework_plan.md](docs/schematic_rework_plan.md) | Step-by-step KiCad schematic rework guide |
| [telegram_setup.md](docs/telegram_setup.md) | Bot creation and configuration guide |
| [wiring.md](docs/wiring.md) | Bench wiring guide |
| [assembly.md](docs/assembly.md) | Assembly instructions |

## License

This project is open source. License TBD.
