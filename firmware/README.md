# IoT Crockpot Firmware

ESP-IDF firmware for the IoT Crockpot controller.

## Hardware Target

- **MCU**: Seeed XIAO ESP32-C3
- **Temperature Sensor**: MAX31855 thermocouple IC (K-type)
- **Relays**: 2x G5LE-1 via 2N7002 MOSFETs

## Current Status

| Module | Status | Notes |
|--------|--------|-------|
| WiFi | Implemented | Connects to configured AP |
| Crockpot State Machine | Implemented | OFF/WARM/LOW/HIGH states |
| Temperature (MAX31855) | Implemented | SPI driver, fault detection |
| Relay Control | Implemented | 2 channels (main + aux) |
| Telegram Bot | Implemented | Remote control interface |
| Display (OLED) | Stub | I2C pins configured |

## GPIO Mapping (XIAO ESP32-C3)

| Function | GPIO | XIAO Pin | Notes |
|----------|------|----------|-------|
| Relay 1 (Main) | 4 | D2 | Active high, via 2N7002 |
| Relay 2 (Aux) | 5 | D3 | Active high, via 2N7002 |
| MAX31855 CS | 3 | D1 | 10k pull-up recommended |
| MAX31855 CLK | 8 | D8 | Strapping pin |
| MAX31855 MISO | 9 | D9 | Strapping pin |
| I2C SDA | 6 | D4 | For OLED display |
| I2C SCL | 7 | D5 | For OLED display |

**Note**: GPIO8/GPIO9 are strapping pins. The MAX31855 CS line should have a 10k pull-up to keep it high (inactive) during boot.

## Building

### Prerequisites

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/) (v5.0+ recommended)
2. Set up the ESP-IDF environment

### Build Commands

```bash
cd firmware

# Set target (one-time, or after cleaning)
idf.py set-target esp32c3

# Configure options (WiFi credentials, Telegram token)
idf.py menuconfig

# Build
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor

# Build, flash, and monitor
idf.py flash monitor
```

### Configuration

Run `idf.py menuconfig` to set:
- WiFi SSID and password
- Telegram bot token (for remote control)

## Project Structure

```
firmware/
├── main/
│   ├── CMakeLists.txt    # Component build config
│   ├── main.c            # Entry point, FreeRTOS tasks
│   ├── wifi.c/.h         # WiFi connection
│   ├── crockpot.c/.h     # State machine (core logic)
│   ├── temperature.c/.h  # MAX31855 SPI driver
│   ├── relay.c/.h        # Relay control (2 channels)
│   ├── telegram.c/.h     # Telegram bot interface
│   └── display.c/.h      # OLED display (stub)
├── CMakeLists.txt        # Top-level project file
├── sdkconfig.defaults    # Default build options
├── partitions.csv        # Flash partition table
└── README.md             # This file
```

## How It Works

### Boot Sequence

1. Connects to WiFi (credentials from menuconfig)
2. Initializes MAX31855 thermocouple sensor (SPI)
3. Initializes 2 relay outputs (GPIO 4, 5)
4. Starts 3 FreeRTOS tasks

### Running Tasks

| Task | What it does |
|------|--------------|
| **Control** | Reads temperature every 1s, safety checks, controls relay |
| **Telegram** | Long-polls Telegram API, responds to bot commands |
| **Display** | Reads buttons, updates OLED (stub - no real driver yet) |

### State Machine

- 4 states: `OFF`, `WARM`, `LOW`, `HIGH`
- Any heating state turns the main relay ON
- Starts in OFF on boot

### Safety Features

- Auto-shutoff at 300°F (configurable in `crockpot.h`)
- Auto-shutoff after 10 consecutive sensor read failures
- Relays default to OFF on init

### Telegram Commands

When configured with a bot token:
- `/status` - Get current state and temperature
- `/off`, `/warm`, `/low`, `/high` - Change state
- `/help` - List commands

## Known Limitations

- Display driver is a stub (no actual OLED output)
- WARM/LOW/HIGH all do the same thing (relay on) - no PWM or temperature targeting
- No persistent state storage (resets to OFF on reboot)

## Build System

See [docs/build-system.md](docs/build-system.md) for details on the ESP-IDF build process.
