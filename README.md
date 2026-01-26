# IoT Crockpot Controller

An open-source, internet-enabled crockpot controller with local physical interface and remote Telegram control. Features a custom PCB designed in KiCad with modular architecture for future expansion.

## Features

- **Remote Control via Telegram** - Control your crockpot from anywhere
- **Local Display Interface** - OLED + buttons or touchscreen (TBD)
- **Temperature Monitoring** - Real-time temperature tracking
- **Safety Features** - Auto-shutoff on high temperature, watchdog timer
- **Modular Design** - Easy to add Blynk, Home Assistant, or custom app

## Project Structure

```
iot_crockpot/
├── firmware/          # ESP-IDF firmware source code
│   ├── main/          # Application source files
│   ├── CMakeLists.txt # Build configuration
│   └── sdkconfig.defaults
├── hardware/          # KiCad PCB design files
│   ├── *.kicad_pro    # KiCad project
│   ├── *.kicad_sch    # Schematic
│   ├── *.kicad_pcb    # PCB layout
│   └── production/    # Generated Gerbers, BOM
├── docs/              # Documentation
│   ├── wiring.md      # Development wiring guide
│   ├── telegram_setup.md
│   └── assembly.md    # PCB assembly instructions
└── README.md
```

## Hardware Requirements

- **MCU**: ESP32 (ESP32-WROOM-32 or similar)
- **Temperature Sensor**: TBD (DS18B20, thermocouple, or NTC)
- **Relay/SSR**: For controlling crockpot heating element
- **Display** (Optional): SSD1306 OLED or small TFT
- **Power**: 5V USB or external supply

## Getting Started

### Prerequisites

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) (v5.0+)
2. Install [KiCad](https://www.kicad.org/) (v7.0+) for hardware design

### Building Firmware

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

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              ESP32                                       │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                      Core Logic Layer                              │  │
│  │  crockpot.c - State machine, business logic                        │  │
│  │  temperature.c - Sensor driver                                     │  │
│  │  relay.c - Relay/SSR control                                       │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                ▲                                         │
│          ┌─────────────────────┼─────────────────────┐                   │
│          │                     │                     │                   │
│  ┌───────┴───────┐    ┌────────┴────────┐   ┌───────┴───────┐           │
│  │   Telegram    │    │  Local Display  │   │    Future:    │           │
│  │   (Remote)    │    │  OLED + Buttons │   │  Blynk / App  │           │
│  └───────────────┘    └─────────────────┘   └───────────────┘           │
└──────────────────────────────────────────────────────────────────────────┘
```

## Safety Considerations

- **Mains Isolation**: Relay/SSR handles high voltage, isolated from ESP32
- **Watchdog Timer**: Auto-reset on system hang
- **Temperature Ceiling**: Auto-shutoff above safe threshold (300°F default)
- **Fail-Safe**: Any error condition → state = OFF
- **Physical Override**: Local interface works without network

**WARNING**: This project involves mains voltage. Only attempt if you have electrical experience. Always follow local electrical codes and safety practices.

## Development Status

- [x] Project structure
- [x] Core state machine
- [x] Telegram bot interface
- [x] Local display framework
- [ ] Temperature sensor driver (hardware TBD)
- [ ] KiCad schematic (in progress)
- [ ] PCB layout
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
