# IoT Crockpot Power Board

Custom PCB for the IoT Crockpot controller. Single-board design — the ESP32-S3
handles WiFi, display, thermocouple, relay, and USB directly with no secondary MCU.

## Architecture

```
AC Mains ──► Fuse ──► MOV ──► HLK-5M05 ──► P-FET ──► 5V Rail
                                           (AO3401)      │
                                         Gate: 100K→GND  │
                                         G-S: 100nF cap  │
                                                         │
                              ┌──────────────────────────┘
                              │
                              ├──► AP2112K-3.3 LDO ──► 3.3V Rail
                              │                           │
                              │              ┌────────────┘
                              │              │
                              │         ESP32-S3-WROOM-1-N4R2
                              │              │
                              │              ├──► ST7796 Display (SPI)
                              │              ├──► FT6336U Touch (I2C)
                              │              ├──► MAX31855 TC (SPI)
                              │              ├──► Relay driver (GPIO → 2N7002)
                              │              └──► USB-C (GPIO19/20, native)
                              │
                              └──► Relay coil supply (5V)

AC Mains ──► Relay NO contact ──► Crockpot heating element
```

## Project Files

- `iot_crockpot.kicad_pro` — KiCad project file
- `iot_crockpot.kicad_sch` — Top-level schematic (hierarchical sheets)
- `MCU.kicad_sch` — ESP32-S3 MCU sheet
- `power_relays_tempsens.kicad_sch` — Power supply, relay, thermocouple sheet
- `iot_crockpot.kicad_pcb` — PCB layout

## Schematic Status

| Section | Status |
|---------|--------|
| AC input (fuse, MOV, HLK-5M05) | Done |
| 5V backfeed protection (P-FET AO3401) | Pending |
| AP2112K-3.3 LDO | Pending |
| ESP32-S3-WROOM-1-N4R2 | In progress |
| USB-C connector (GCT USB4085 / HRO TYPE-C-31-M-12) | Pending |
| 14-pin display header (ST7796 + FT6336U) | Pending |
| MAX31855 thermocouple | Pending |
| Relay driver (2N7002 MOSFET) | Done |
| Relay (G5LE-1) | Done |
| Connectors (AC screw terminals, thermocouple) | Done |

See [docs/schematic_rework_plan.md](../docs/schematic_rework_plan.md) for the
step-by-step schematic rework guide.

## Components

### MCU
| Part | Value | Package | LCSC # |
|------|-------|---------|--------|
| ESP32-S3-WROOM-1-N4R2 | 4MB flash, 2MB PSRAM | Module | C2913203 |

### Display Interface
| Part | Value | Notes |
|------|-------|-------|
| J_DISPLAY | 14-pin 2.54mm header | ST7796 SPI display + FT6336U touch |

Display modules (all electrically equivalent, 14-pin 2.54mm interface):
- Hosyond (Amazon B0CMD7Y55M) ~$15
- Waveshare 3.5" Capacitive Touch LCD ~$19
- Elecrow 3.5" IPS SPI LCD ~$17

**Note:** Use ST7796 controller only. Avoid ILI9488 — no 16-bit SPI color and
broken MISO tristate cause SPI bus conflicts.

### 3.3V LDO
| Part | Value | Package | LCSC # |
|------|-------|---------|--------|
| AP2112K-3.3TRG1 | 3.3V 600mA | SOT-23-5 | (search) |

### Power Supply
| Part | Value | Notes |
|------|-------|-------|
| HLK-5M05 | 5V 5W | AC-DC isolated PSU module |

### USB
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| USB-C receptacle | GCT USB4085 or HRO TYPE-C-31-M-12 | C2765186 / C165948 | USB 2.0 only |
| R_D- | 22Ω 0402 | — | GPIO19 to D- |
| R_D+ | 22Ω 0402 | — | GPIO20 to D+ |
| R_CC1 | 5.1K 0402 | — | CC1 to GND |
| R_CC2 | 5.1K 0402 | — | CC2 to GND |

### AC Input Protection
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| Fuse holder | 5x20mm clips | C3130 | Xucheng pair |
| Fuse | 0.5A 250V slow-blow | C142839 | Littelfuse 0215.500MXP |
| MOV | 10D561K | (search) | Per HLK datasheet |

### Temperature Sensing
| Part | Value | Notes |
|------|-------|-------|
| MAX31855KASA | K-type interface | SPI, cold junction compensated |
| Thermocouple | K-type probe | High temp |

### Relay
| Part | Value | Notes |
|------|-------|-------|
| Omron G5LE-1 | SPDT 10A | 5V coil |
| 2N7002 | N-ch MOSFET | Relay driver (x2) |
| 1N4148WQ-13-F | Flyback diode | SOD-123 |
| 10K resistor | Gate pull-down | Keeps relay off when GPIO floating |

### Protection
| Part | Value | Notes |
|------|-------|-------|
| P-FET (AO3401) | SOT-23 | Blocks USB-C VBUS backfeed into HLK-5M05 |
| 100K resistor | Gate to GND | P-FET gate bias |
| 100nF cap | Gate-source | Miller coupling suppression |

### Connectors
| Part | Value | LCSC # | Notes |
|------|-------|--------|-------|
| Screw terminal (x2) | Phoenix 1935161 | (search) | AC mains in, relay out |

## GPIO Assignments

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI SCK | 12 | Shared: display, MAX31855 |
| SPI MOSI | 11 | |
| SPI MISO | 13 | |
| LCD CS | 10 | |
| LCD DC | 9 | |
| LCD RST | 8 | |
| LCD BL | 47 | PWM backlight |
| SD CS | 14 | SD card on display module |
| Touch SDA | 5 | I2C |
| Touch SCL | 4 | I2C |
| Touch RST | 6 | |
| Touch INT | 7 | |
| MAX31855 CS | 16 | |
| Relay | 17 | Via 2N7002 driver |
| USB D- | 19 | Fixed, native USB |
| USB D+ | 20 | Fixed, native USB |

## ESP32-S3 Strapping Pins

| Pin | Default | Action |
|-----|---------|--------|
| GPIO0 | Pull-up to 3V3 | Ensures SPI Boot (normal) on power-up |
| GPIO46 | Float (internal pull-down) | Leave unconnected |
| EN | 10K to 3V3 + 1µF to GND | RC filter for clean power-on reset |

BOOT jumper: 2-pin 2.54mm header (GPIO0 to GND). Install to enter download mode on
next reset. Remove for normal boot. Not needed for routine flashing — native USB
handles auto-reset via esptool.py.

## Safety Notes

- **Mains Isolation**: HLK-5M05 provides isolation; maintain clearance on PCB
- **Creepage/Clearance**: Follow IPC-2221 for mains voltage traces
- **Fuse**: Must be rated for AC mains (250VAC)
- **Relay Wiring**: Use NO contact for fail-safe (de-energized = OFF)

## Fabrication Notes

1. Generate Gerbers to `production/gerbers/`
2. 2-layer board, 1.6mm thickness
3. HASL or ENIG finish
4. Minimum 2oz copper for mains traces

## References

- [Hardware Decisions](../docs/hardware_decisions.md) — Component selection rationale
- [Schematic Rework Plan](../docs/schematic_rework_plan.md) — Step-by-step rework guide
- [ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
- [ST7796 Display Module](https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796)
- [Omron G5LE-1 Datasheet](http://www.omron.com/ecb/products/pdf/en-g5le.pdf)
