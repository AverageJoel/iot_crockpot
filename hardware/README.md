# IoT Crockpot Power Board

Custom PCB for the IoT Crockpot controller. This board handles AC-DC conversion, relay control, and temperature sensing, communicating with the CYD display board via UART over a single 4-pin JST cable.

## Project Files

- `iot_crockpot.kicad_pro` - KiCad project file
- `iot_crockpot.kicad_sch` - Schematic
- `iot_crockpot.kicad_pcb` - PCB layout
- `cyd_enclosure.scad` - 3D printable enclosure for CYD

## Directory Structure

```
hardware/
├── iot_crockpot.kicad_pro    # Project file
├── iot_crockpot.kicad_sch    # Schematic
├── iot_crockpot.kicad_pcb    # PCB layout
├── cyd_enclosure.scad        # OpenSCAD enclosure
├── symbols/                   # Custom schematic symbols
├── footprints/                # Custom component footprints
├── 3dmodels/                  # 3D models for visualization
├── production/                # Generated output files
│   ├── gerbers/              # Gerber files for fabrication
│   ├── bom/                  # Bill of materials
│   └── assembly/             # Assembly drawings
└── README.md                  # This file
```

## Board Architecture

```
AC Mains ──► Fuse ──► MOV ──► HLK-5M05 ──► P-FET ──► 5V Rail
                                            (AO3401)     │
                                          Gate: 100K→GND │
                                          G-S: 100nF cap │
                                                         │
                                ┌────────────────────────┘
                                │
                                ├──► CYD P1 (5V power + UART)
                                │
                                ├──► AP2112K-3.3 LDO ──► 3.3V Rail
                                │                          │
                                │                          ├──► STM32 VDD
                                │                          └──► MAX31855 VCC
                                │
                                └──► Relay coil supply

CYD P1 (UART) ◄──1K──► STM32G031 ◄──► MAX31855 ◄──► Thermocouple
                            │
                            └──► Relay control
```

## Components

### MCU
| Part | Value | Package | LCSC # |
|------|-------|---------|--------|
| STM32G031F6P6 | Cortex-M0+ | TSSOP-20 | (search) |

- 125°C temperature rating (important near heating element)
- Built-in UART bootloader for firmware updates via CYD (PA9/PA10)
- UART to CYD, SPI master to MAX31855

### 3.3V LDO Regulator
| Part | Value | Package | LCSC # |
|------|-------|---------|--------|
| AP2112K-3.3TRG1 | 3.3V 600mA | SOT-23-5 | (search) |

- Input: 5V rail from HLK-5M05
- Output: 3.3V for STM32 + MAX31855 (~55mA max draw)
- Decoupling: 1uF ceramic input, 1uF ceramic output

### Power Supply
| Part | Value | Notes |
|------|-------|-------|
| HLK-5M05 | 5V 5W | AC-DC isolated PSU module |

### Input Protection
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| Fuse holder | 5x20mm clips | C3130 | Xucheng pair |
| Fuse | 0.5A 250V slow-blow | C142839 | Littelfuse 0215.500MXP |
| MOV | 10D561K | (search) | Per HLK datasheet |

### Temperature Sensing
| Part | Value | Notes |
|------|-------|-------|
| MAX31855KASA | K-type interface | SPI, cold junction compensated |
| Thermocouple | K-type | High temp probe |

### Relay
| Part | Value | Notes |
|------|-------|-------|
| Omron G5LE-1 | SPDT 10A | 5V coil |
| 1N4148WQ-13-F | Flyback diode | SOD-123 |

**Relay Pinout:**
| Pin | Function |
|-----|----------|
| 1 | COM (Common) |
| 2 | Coil (-) |
| 3 | NC (Normally Closed) |
| 4 | NO (Normally Open) - **Use this for fail-safe** |
| 5 | Coil (+) |

### Protection Circuits
| Part | Value | Package | Notes |
|------|-------|---------|-------|
| P-FET | AO3401 or SI2301 | SOT-23 | High-side pass transistor, blocks USB 5V backfeed into HLK-5M05 |
| Gate resistor | 100K | 0402/0603 | Pulls P-FET gate to GND (turns FET on when HLK powered) |
| Gate-source cap | 100nF | 0402/0603 | Suppresses Miller coupling during USB contact bounce |
| UART series resistors | 1K (x2) | 0402/0603 | On TX (PA9→P1) and RX (PA10←P1), limits bus contention current |

### Connectors
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| JST GH 4-pin | SM04B-GHS-TB | C189895 | CYD cable connector (x1) |
| Screw terminal | Phoenix 1935161 | (search) | Mains input, relay output |

## KiCad Libraries

### Symbols (built-in)
- `Device:Fuse`
- `Connector:Screw_Terminal_01x02`
- `Connector:Conn_01x04_Pin`

### Footprints (built-in)
| Component | Footprint |
|-----------|-----------|
| Fuse clips | `Fuse:Fuseholder_Clip-5x20mm_Littelfuse_111_Inline_P20.00x5.00mm_D1.05mm_Horizontal` |
| Screw terminal | `TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2_1x02_P5.00mm_Horizontal` |
| JST GH 4-pin | `Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal` |
| Relay | `Relay_THT:Relay_SPDT_Omron-G5LE-1` |
| Flyback diode | `Diode_SMD:D_SOD-123` |

## CYD Interface

The power board connects to the CYD via a single 1.25mm JST cable (P1):

### P1 Cable (Power + UART)
| CYD Pin | Function | Power Board | Notes |
|---------|----------|-------------|-------|
| VIN | 5V | From HLK-5M05 via P-FET | P-FET blocks USB backfeed |
| TX | UART TX | STM32 PA10 (USART1_RX) | Via 1K series resistor |
| RX | UART RX | STM32 PA9 (USART1_TX) | Via 1K series resistor |
| GND | Ground | GND | |

Note: PA9/PA10 are also the STM32 system bootloader UART pins, enabling firmware updates over the same cable. The 1K series resistors protect against bus contention when the CYD is programmed via USB (CH340 and STM32 both driving the same UART0 lines).

## CYD Dimensions

For enclosure design:
| Measurement | Size |
|-------------|------|
| PCB size | 101.5 x 54.9 mm |
| Display active area | 73.44 x 48.96 mm |

## Safety Notes

- **Mains Isolation**: HLK-5M05 provides isolation; maintain clearance on PCB
- **Creepage/Clearance**: Follow IPC-2221 for mains voltage traces
- **Fuse**: Must be rated for AC mains (250VAC)
- **Relay Wiring**: Use NO contact for fail-safe (de-energized = OFF)
- **Grounding**: Connect earth ground to enclosure if metal

## Fabrication Notes

When ordering PCBs:
1. Generate Gerbers to `production/gerbers/`
2. 2-layer board, 1.6mm thickness
3. HASL or ENIG finish
4. Minimum 2oz copper for mains traces
5. Consider conformal coating for humidity resistance

## References

- [Hardware Decisions](../docs/hardware_decisions.md) - Component selection rationale
- [Omron G5LE-1 Datasheet](http://www.omron.com/ecb/products/pdf/en-g5le.pdf)
- [STM32G031 Datasheet](https://www.st.com/resource/en/datasheet/stm32g031c6.pdf)
- [CYD Pinout](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md)

## Version History

- **v0.1** - Initial schematic with power supply section
- **v0.2** - Added STM32G031 MCU, two-board architecture
- **v0.3** - Switched from I2C (2 cables) to UART (1 cable), added AP2112K-3.3 LDO
