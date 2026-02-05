# IoT Crockpot Power Board

Custom PCB for the IoT Crockpot controller. This board handles AC-DC conversion, relay control, and temperature sensing, communicating with the CYD display board via I2C.

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
AC Mains ──► Fuse ──► MOV ──► HLK-5M05 ──► 5V Rail
                                │
                                ├──► CYD P1 (5V power)
                                │
                                └──► Relay coil supply

CYD CN1 ◄──► STM32G031 ◄──► MAX31855 ◄──► Thermocouple
  │              │
  │              └──► Relay control
  │
  └──► 3.3V powers STM32
```

## Components

### MCU
| Part | Value | Package | LCSC # |
|------|-------|---------|--------|
| STM32G031F6P6 | Cortex-M0+ | TSSOP-20 | (search) |

- 125°C temperature rating (important near heating element)
- Built-in I2C bootloader for firmware updates via CYD
- I2C slave to CYD, SPI master to MAX31855

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

### Connectors
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| JST GH 4-pin | SM04B-GHS-TB | C189895 | CYD cable connectors (x2) |
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

The power board connects to the CYD via two 1.25mm JST cables:

### P1 Cable (Power)
| CYD Pin | Function | Power Board |
|---------|----------|-------------|
| VIN | 5V | From HLK-5M05 |
| TX | (unused) | NC |
| RX | (unused) | NC |
| GND | Ground | GND |

### CN1 Cable (I2C + 3.3V)
| CYD Pin | Function | Power Board |
|---------|----------|-------------|
| GND | Ground | GND |
| IO22 | I2C SCL | STM32 PB6 |
| IO27 | I2C SDA | STM32 PB7 |
| 3.3V | Power | STM32 VDD |

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
