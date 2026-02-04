# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Internet-enabled crockpot controller with:
- Remote control via Telegram bot
- 3.5" capacitive touchscreen (CYD - ESP32-3248S035C)
- Two-board architecture: CYD display + custom power control PCB
- Modular architecture for future expansion (Blynk, Home Assistant)

## Hardware Architecture

Two-board design connected by two cables:

```
CYD Display Board (ESP32-3248S035C)     Power Board (Custom PCB)
┌─────────────────────────────────┐     ┌─────────────────────────────┐
│  3.5" Capacitive Touch LCD      │     │  STM32 MCU (I2C Slave)      │
│  ESP32-WROOM-32                 │     │    ├── Relay/SSR            │
│   • WiFi / Telegram             │     │    └── MAX31855 (SPI)       │
│   • UI / State Management       │     │        K-Type Thermocouple  │
│   • I2C Master                  │     │                             │
│                                 │     │  HLK-PM01 PSU (AC→5V)       │
│  P1: VIN, GND ◄──── 5V power ───│◄────│                             │
│  CN1: IO22 (SCL), IO27 (SDA),   │◄───►│  3.3V powered from CYD      │
│       3.3V, GND                 │     │                             │
└─────────────────────────────────┘     └─────────────────────────────┘
```

### CYD Connectors
- **P1** (4-pin 1.25mm JST): VIN, TX, RX, GND - Power input (5V)
- **CN1** (4-pin 1.25mm JST): GND, IO22, IO27, 3.3V - I2C bus + power out

### Why Two MCUs?
CYD has limited GPIO (only IO22, IO27 usable). STM32 on power board:
- Handles relay control, temperature sensing, local safety logic
- Can fail-safe independently if CYD crashes
- Clean isolation between mains and display electronics

## Tech Stack

### CYD Firmware
- **Framework**: ESP-IDF (Espressif official SDK)
- **Language**: C
- **Target**: ESP32-WROOM-32 (on CYD)

### Power Board Firmware
- **Framework**: STM32 HAL or bare-metal
- **Language**: C
- **Target**: STM32 (specific variant TBD)

### PCB Design
- **Tool**: KiCad 7.0+

## Build Commands

```bash
# CYD Firmware (ESP-IDF)
cd firmware
idf.py set-target esp32
idf.py menuconfig  # Configure WiFi, Telegram token
idf.py build
idf.py flash monitor
```

## Project Structure

```
firmware/main/           # ESP-IDF firmware for CYD
├── main.c               # Entry point, FreeRTOS task creation
├── wifi.c/.h            # WiFi connection management
├── crockpot.c/.h        # Core state machine (OFF/WARM/LOW/HIGH)
├── i2c_master.c/.h      # I2C communication with power board
├── telegram.c/.h        # Telegram bot interface
└── display.c/.h         # Touchscreen UI (LVGL)

hardware/
├── iot_crockpot.kicad_* # KiCad project (power board)
├── cyd_enclosure.scad   # 3D printable enclosure
├── symbols/             # Custom schematic symbols
├── footprints/          # Custom PCB footprints
└── production/          # Generated Gerbers, BOM

simulator/               # Python simulator for development
```

## I2C Protocol (CYD ↔ Power Board)

| Register | R/W | Description |
|----------|-----|-------------|
| 0x00 | R | Status (state, error flags) |
| 0x01 | R | Temperature MSB |
| 0x02 | R | Temperature LSB |
| 0x03 | R/W | Relay state (OFF/WARM/LOW/HIGH) |
| 0x04 | R | Firmware version |
| 0x10 | W | Command register |

## CYD GPIO Usage

| GPIO | Function | Notes |
|------|----------|-------|
| IO22 | I2C SCL | CN1 connector |
| IO27 | I2C SDA | CN1 connector |

All other GPIOs are used by the display, touch, SD card, or are input-only.

## Power Board Components

- **MCU**: STM32 (I2C slave, SPI master)
- **PSU**: HLK-PM01 (AC-DC 5V)
- **Temp Sensor**: MAX31855 + K-type thermocouple (SPI)
- **Output**: SSR/Relay for heating element

## Safety Notes

- Power board STM32 runs independent watchdog
- Auto-shutoff at 300°F (configurable)
- Relay fails to OFF state on any error
- Mains isolation between power board and CYD
