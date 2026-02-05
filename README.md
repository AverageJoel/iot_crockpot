# IoT Crockpot Controller

An open-source, internet-enabled crockpot controller with local touchscreen interface and remote Telegram control. Features a two-board architecture with a CYD display module and custom power control PCB.

## Features

- **Remote Control via Telegram** - Control your crockpot from anywhere
- **3.5" Capacitive Touchscreen** - Full-color UI on ESP32-3248S035C (CYD)
- **K-Type Thermocouple** - Accurate temperature monitoring via MAX31855
- **Safety Features** - Independent power board MCU with auto-shutoff, watchdog
- **Modular Design** - Easy to add Blynk, Home Assistant, or custom app

## Architecture

The system uses a two-board design connected by two cables:

```
┌─────────────────────────────────────┐      ┌─────────────────────────────────┐
│         CYD Display Board           │      │         Power Board             │
│       (ESP32-3248S035C)             │      │                                 │
│                                     │      │  ┌─────────┐                    │
│  ┌─────────────────────────────┐    │      │  │ STM32   │◄── I2C Slave       │
│  │  3.5" Capacitive Touch LCD  │    │      │  │  MCU    │                    │
│  │        320x480 ST7796       │    │      │  └────┬────┘                    │
│  └─────────────────────────────┘    │      │       │                         │
│                                     │      │       ├──► Relay/SSR            │
│  ESP32-WROOM-32                     │      │       │                         │
│   • WiFi / Telegram                 │      │       └──► MAX31855 (SPI)       │
│   • UI / State Management           │      │            K-Type Thermocouple  │
│   • I2C Master                      │      │                                 │
│                                     │      │  ┌─────────┐                    │
│  P1: VIN ◄──────── 5V ──────────────│◄─────│──┤ HLK-PM01│◄── AC Mains        │
│      GND ◄──────── GND ─────────────│◄─────│──┤  PSU    │                    │
│                                     │      │  └─────────┘                    │
│  CN1: IO22 ◄─────── SCL ────────────│◄────►│                                 │
│       IO27 ◄─────── SDA ────────────│◄────►│       3.3V from CYD powers      │
│       3.3V ─────── 3.3V ────────────│─────►│       the STM32 MCU             │
│       GND ◄──────── GND ────────────│◄─────│                                 │
└─────────────────────────────────────┘      └─────────────────────────────────┘
```

### Cable Connections

| Cable | CYD Connector | Pins | Purpose |
|-------|---------------|------|---------|
| Power | P1 (4-pin 1.25mm JST) | VIN, GND | 5V power from power board to CYD |
| Control | CN1 (4-pin 1.25mm JST) | IO22, IO27, 3.3V, GND | I2C bus + 3.3V to power board MCU |

### Why Two MCUs?

The CYD (ESP32-3248S035C) has very limited GPIO - only IO22 and IO27 are usable. By adding an STM32 on the power board as an I2C slave:
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
- **Connectors**: P1 (power), CN1 (I2C)

### Power Board (Custom PCB)
- **MCU**: STM32G031F6P6 (TSSOP-20, I2C slave, 125°C rated)
- **Temperature**: MAX31855 + K-type thermocouple (SPI)
- **Output**: Omron G5LE-1 relay (SPDT, 10A)
- **Power Supply**: HLK-5M05 (AC-DC 5V 5W)
- **Input Protection**: 0.5A fuse + MOV (10D561K)
- **Connectors**: 1.25mm JST GH for CYD cables, Phoenix Contact screw terminals for mains
- **Provides**: 5V to CYD, receives 3.3V back for STM32

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

## I2C Command Protocol

The CYD communicates with the power board STM32 via I2C. Proposed register map:

| Register | R/W | Description |
|----------|-----|-------------|
| 0x00 | R | Status (state, error flags) |
| 0x01 | R | Temperature MSB |
| 0x02 | R | Temperature LSB |
| 0x03 | R/W | Relay state (OFF/WARM/LOW/HIGH) |
| 0x04 | R | Firmware version |
| 0x10 | W | Command register |

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
- [ ] STM32 I2C slave firmware
- [ ] MAX31855 SPI driver (STM32)
- [ ] I2C master integration (CYD)
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
