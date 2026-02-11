# IoT Crockpot Controller

An open-source, internet-enabled crockpot controller with local touchscreen interface and remote Telegram control. Features a two-board architecture connected by a single cable: a CYD display module and custom power control PCB communicating over UART.

## Features

- **Remote Control via Telegram** - Control your crockpot from anywhere
- **3.5" Capacitive Touchscreen** - Full-color UI on ESP32-3248S035C (CYD)
- **K-Type Thermocouple** - Accurate temperature monitoring via MAX31855
- **Safety Features** - Independent power board MCU with auto-shutoff, watchdog
- **Modular Design** - Easy to add Blynk, Home Assistant, or custom app

## Architecture

The system uses a two-board design connected by a single 4-pin JST cable (P1):

```
┌─────────────────────────────────────┐      ┌─────────────────────────────────┐
│         CYD Display Board           │      │         Power Board             │
│       (ESP32-3248S035C)             │      │                                 │
│                                     │      │  ┌─────────┐                    │
│  ┌─────────────────────────────┐    │      │  │ STM32   │◄── UART            │
│  │  3.5" Capacitive Touch LCD  │    │      │  │  MCU    │                    │
│  │        320x480 ST7796       │    │      │  └────┬────┘                    │
│  └─────────────────────────────┘    │      │       │                         │
│                                     │      │       ├──► Relay/SSR            │
│  ESP32-WROOM-32                     │      │       │                         │
│   • WiFi / Telegram                 │      │       └──► MAX31855 (SPI)       │
│   • UI / State Management           │      │            K-Type Thermocouple  │
│   • UART to power board             │      │                                 │
│                                     │      │  ┌─────────┐                    │
│  P1: VIN ◄──────── 5V ──────────────│◄─────│──┤HLK-5M05 │◄── AC Mains       │
│      TX  ────────► RX (PA10) ───────│─────►│──┤  PSU    │                    │
│      RX  ◄──────── TX (PA9) ────────│◄─────│  └─────────┘                    │
│      GND ◄──────── GND ─────────────│◄─────│                                 │
│                                     │      │  AP2112K-3.3 LDO (5V→3.3V)     │
│                                     │      │  Powers STM32 + MAX31855        │
└─────────────────────────────────────┘      └─────────────────────────────────┘
```

### Cable Connection

| Cable | CYD Connector | Pins | Purpose |
|-------|---------------|------|---------|
| P1 | P1 (4-pin 1.25mm JST) | VIN, TX, RX, GND | 5V power + UART communication |

### Why Two MCUs?

The CYD (ESP32-3248S035C) has very limited GPIO - only TX/RX on the P1 connector are available for inter-board communication. By adding an STM32 on the power board:
- CYD handles WiFi, Telegram, touchscreen UI
- STM32 handles relay control, temperature sensing, local safety logic
- Power board can fail-safe independently if CYD crashes
- Clean isolation between mains power and display electronics

## Project Structure

```
iot_crockpot/
├── firmware/          # ESP-IDF firmware for CYD (ESP32)
│   ├── main/          # Application source files
│   ├── CMakeLists.txt # Build configuration
│   └── sdkconfig.defaults
├── hardware/          # KiCad PCB design files
│   ├── *.kicad_sch    # Schematic (power board)
│   ├── *.kicad_pcb    # PCB layout
│   ├── cyd_enclosure.scad  # 3D printable enclosure
│   └── production/    # Generated Gerbers, BOM
├── simulator/         # Python simulator for development
├── docs/              # Documentation
└── README.md
```

## Hardware

### CYD Display Board (Off-the-shelf)
- **Module**: ESP32-3248S035C (Cheap Yellow Display)
- **Display**: 3.5" 320x480 TFT with capacitive touch (GT911)
- **MCU**: ESP32-WROOM-32
- **Connector**: P1 (power + UART)

### Power Board (Custom PCB)
- **MCU**: STM32G031F6P6 (TSSOP-20, UART to CYD, 125°C rated)
- **Temperature**: MAX31855 + K-type thermocouple (SPI)
- **Output**: Omron G5LE-1 relay (SPDT, 10A)
- **Power Supply**: HLK-5M05 (AC-DC 5V 5W)
- **LDO**: AP2112K-3.3TRG1 (5V→3.3V for STM32 + MAX31855)
- **Input Protection**: 0.5A fuse + MOV (10D561K)
- **5V Backfeed Protection**: P-FET (AO3401) pass transistor prevents USB 5V from backfeeding into HLK-5M05
- **UART Protection**: 1K series resistors on TX/RX lines limit bus contention current with CYD's CH340
- **Connectors**: 1.25mm JST GH for CYD cable, Phoenix Contact screw terminals for mains
- **Provides**: 5V to CYD; local 3.3V from onboard LDO

See [docs/hardware_decisions.md](docs/hardware_decisions.md) for component selection rationale and LCSC part numbers.

## Getting Started

### Prerequisites

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) (v5.0+)
2. Install [KiCad](https://www.kicad.org/) (v7.0+) for hardware design
3. STM32 toolchain (STM32CubeIDE or arm-none-eabi-gcc)

### Building CYD Firmware (ESP32)

```bash
cd firmware
idf.py set-target esp32
idf.py menuconfig  # Configure WiFi credentials
idf.py build
idf.py flash
```

### Configuration

Configure via `idf.py menuconfig`:
- WiFi SSID and password
- Telegram bot token (see [docs/telegram_setup.md](docs/telegram_setup.md))
- GPIO pin assignments

## Telegram Commands

| Command | Description |
|---------|-------------|
| `/status` | Current state and temperature |
| `/off` | Turn off |
| `/warm` | Set to warm |
| `/low` | Set to low |
| `/high` | Set to high |
| `/help` | List commands |

## UART Command Protocol

The CYD communicates with the power board STM32 via UART (P1 connector TX/RX pins). Packet format:

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

## Safety Considerations

- **Mains Isolation**: Relay/SSR handles high voltage, isolated from ESP32
- **Watchdog Timer**: Auto-reset on system hang
- **Temperature Ceiling**: Auto-shutoff above safe threshold (300°F default)
- **Fail-Safe**: Any error condition → state = OFF
- **Physical Override**: Local interface works without network

**WARNING**: This project involves mains voltage. Only attempt if you have electrical experience. Always follow local electrical codes and safety practices.

## Development Status

- [x] Project structure
- [ ] Core state machine
- [ ] Telegram bot interface
- [ ] CYD touchscreen UI
- [ ] Python simulator
- [ ] KiCad schematic (power board)
- [ ] CYD enclosure design (OpenSCAD)
- [ ] Power board PCB layout
- [ ] STM32 UART firmware
- [ ] MAX31855 SPI driver (STM32)
- [ ] UART integration (CYD)
- [ ] Testing and validation

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

This project is open source. License TBD.

## Acknowledgments

- ESP-IDF framework by Espressif
- KiCad EDA for hardware design
- Telegram Bot API
