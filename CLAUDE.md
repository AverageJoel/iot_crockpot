# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Internet-enabled crockpot controller with:
- Remote control via Telegram bot
- Local display interface (OLED + buttons or touchscreen)
- Custom PCB designed in KiCad
- Modular architecture for future expansion (Blynk, Home Assistant)

## Tech Stack

- **Framework**: ESP-IDF (Espressif official SDK)
- **Language**: C (ESP-IDF standard)
- **Target**: ESP32 (ESP32-WROOM-32 or similar)
- **PCB Design**: KiCad 7.0+

## Build Commands

```bash
# Set up ESP-IDF environment first (follow ESP-IDF installation guide)

cd firmware

# Configure target
idf.py set-target esp32

# Configure options (WiFi, Telegram token, GPIOs)
idf.py menuconfig

# Build
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor

# Build, flash, and monitor in one command
idf.py flash monitor
```

## Project Structure

```
firmware/main/
├── main.c           # Entry point, FreeRTOS task creation
├── wifi.c/.h        # WiFi connection management
├── crockpot.c/.h    # Core state machine (OFF/WARM/LOW/HIGH)
├── temperature.c/.h # Temperature sensor driver (stub - TBD)
├── relay.c/.h       # Relay/SSR control
├── telegram.c/.h    # Telegram bot interface (remote control)
├── display.c/.h     # Local UI (OLED/touchscreen)
└── interface_blynk.c/.h  # Blynk stub (future)

hardware/
├── iot_crockpot.kicad_* # KiCad project files
├── symbols/         # Custom schematic symbols
├── footprints/      # Custom PCB footprints
└── production/      # Generated Gerbers, BOM
```

## Architecture

The code follows a layered architecture:

1. **Core Layer** (interface-agnostic):
   - `crockpot.c` - State machine, temperature limits, safety logic
   - `temperature.c` - Sensor abstraction
   - `relay.c` - Output control

2. **Interface Layer** (uses Core API):
   - `telegram.c` - Remote control via Telegram
   - `display.c` - Local OLED/buttons UI

## Key APIs

```c
// Core state control
crockpot_status_t crockpot_get_status(void);
bool crockpot_set_state(crockpot_state_t state);

// States: CROCKPOT_OFF, CROCKPOT_WARM, CROCKPOT_LOW, CROCKPOT_HIGH
```

## GPIO Assignments (Default)

| Function | GPIO | Notes |
|----------|------|-------|
| Temperature Sensor | 4 | DS18B20 or similar |
| Relay Control | 5 | Active high |
| I2C SDA (Display) | 21 | OLED display |
| I2C SCL (Display) | 22 | OLED display |
| Button UP | 12 | Internal pull-up |
| Button DOWN | 13 | Internal pull-up |
| Button SELECT | 14 | Internal pull-up |

## Safety Notes

- Temperature sensor driver is a STUB - implement real sensor before use
- Safety auto-shutoff at 300°F - configurable in `crockpot.h`
- Relay fails to OFF state on any error
- Watchdog timer enabled by default

## Development Notes

- Temperature sensor hardware TBD (DS18B20, MAX31855, or NTC)
- Display hardware TBD (SSD1306 OLED vs small TFT)
- Telegram token must be configured before remote control works
- See `docs/wiring.md` for development wiring before PCB
