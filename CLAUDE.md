# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Preferences

- **Git commits**: Do NOT include "Co-Authored-By: Claude" in commit messages for this project.

## Project Overview

Internet-enabled crockpot controller with:
- Remote control via Telegram bot
- 3.5" capacitive touchscreen (CYD - ESP32-3248S035C)
- Two-board architecture: CYD display + custom power control PCB
- Modular architecture for future expansion (Blynk, Home Assistant)

## Hardware Architecture

Two-board design connected by a single cable:

```
CYD Display Board (ESP32-3248S035C)     Power Board (Custom PCB)
┌─────────────────────────────────┐     ┌─────────────────────────────┐
│  3.5" Capacitive Touch LCD      │     │  STM32 MCU (UART)           │
│  ESP32-WROOM-32                 │     │    ├── Relay/SSR            │
│   • WiFi / Telegram             │     │    └── MAX31855 (SPI)       │
│   • UI / State Management       │     │        K-Type Thermocouple  │
│   • UART to power board         │     │                             │
│                                 │     │  HLK-5M05 PSU (AC→5V)      │
│  P1: VIN, TX, RX, GND           │     │  AP2112K-3.3 LDO (5V→3.3V) │
│       ◄──── 5V power ───────────│◄────│                             │
│       ────► UART TX → STM32 RX ─│────►│                             │
│       ◄──── UART RX ← STM32 TX ─│◄───│                             │
└─────────────────────────────────┘     └─────────────────────────────┘
```

### CYD Connector
- **P1** (4-pin 1.25mm JST): VIN, TX, RX, GND - Power + UART communication

### Why Two MCUs?
CYD has limited GPIO (only TX/RX on P1 available for inter-board communication). STM32 on power board:
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
├── uart_master.c/.h     # UART communication with power board
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

## UART Protocol (CYD ↔ Power Board)

Packet-based protocol over UART (P1 connector TX/RX pins):

```
[START] [CMD] [LEN] [PAYLOAD...] [CHECKSUM]
 0xAA    1B    1B    0-255 bytes    XOR
```

| Command | Direction | Description |
|---------|-----------|-------------|
| 0x00 | ESP→STM | Get status (state, error flags) |
| 0x01 | ESP→STM | Get temperature |
| 0x03 | ESP→STM | Set relay state (OFF/WARM/LOW/HIGH) |
| 0x04 | ESP→STM | Get firmware version |
| 0x10 | ESP→STM | Command (reboot, enter bootloader, etc.) |
| 0x80+ | STM→ESP | Response / async alert |

## CYD GPIO Usage

| GPIO | Function | Notes |
|------|----------|-------|
| TX | UART TX | P1 connector, to STM32 PA10 |
| RX | UART RX | P1 connector, from STM32 PA9 |

All other GPIOs are used by the display, touch, SD card, or are input-only.

## Power Board Components

- **MCU**: STM32G031F6P6 (UART to CYD, SPI to MAX31855)
- **PSU**: HLK-5M05 (AC-DC 5V)
- **LDO**: AP2112K-3.3TRG1 (5V→3.3V for STM32 + MAX31855)
- **Temp Sensor**: MAX31855 + K-type thermocouple (SPI)
- **Output**: SSR/Relay for heating element
- **Protection**: P-FET (AO3401) backfeed protection on 5V rail; 1K series resistors on UART TX/RX lines

## Safety Notes

- Power board STM32 runs independent watchdog
- Auto-shutoff at 300°F (configurable)
- Relay fails to OFF state on any error
- Mains isolation between power board and CYD
