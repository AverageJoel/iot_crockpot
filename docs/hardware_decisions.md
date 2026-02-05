# Hardware Architecture Decisions

Notes from design discussion - February 2026

## Two-Board Architecture

### CYD Display Board (off-the-shelf)
- **Model**: ESP32-3248S035C (Cheap Yellow Display)
- **Display**: 3.5" 320x480 TFT ST7796, capacitive touch (GT911)
- **MCU**: ESP32-WROOM-32
- **Role**: WiFi, Telegram, touchscreen UI, I2C master

### Power Board (custom PCB)
- **MCU**: STM32G031F6P6 (TSSOP-20)
- **Role**: I2C slave, relay control, temperature sensing, local safety logic

## Why STM32G031?
- STM32G031 vs G030: G031 has **125°C temperature rating** (important near heating element)
- Cortex-M0+, ~$1, TSSOP-20 easy to hand solder
- Built-in I2C bootloader for firmware updates via CYD

## CYD Connectors

### P1 (4-pin 1.25mm JST) - Power Input
| Pin | Function |
|-----|----------|
| VIN | 5V input from power board |
| TX | (unused) |
| RX | (unused) |
| GND | Ground |

### CN1 (4-pin 1.25mm JST) - I2C + Power Out
| Pin | Function |
|-----|----------|
| GND | Ground |
| IO22 | I2C SCL |
| IO27 | I2C SDA |
| 3.3V | Power output to STM32 |

Source: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md

## STM32G031F6P6 TSSOP-20 Pinout

```
        ┌──────────┐
  PB7 ──┤1       20├── PB3/PB4/PB5/PB6 (bonded)
  PB8 ──┤2       19├── PA15
  PB9 ──┤3       18├── PA14 (SWCLK/BOOT0)
  VDD ──┤4       17├── PA13 (SWDIO)
  VSS ──┤5       16├── PA12
 NRST ──┤6       15├── PA11/PB0/PB1/PB2/PA8 (bonded)
  PA0 ──┤7       14├── PA10
  PA1 ──┤8       13├── PA9
  PA2 ──┤9       12├── VDD
  PA3 ──┤10      11├── VSS
        └──────────┘
```

## STM32 Pin Assignments

### I2C1 (to CYD) - ACTIVE MODE
| Function | Pin | Package Pin |
|----------|-----|-------------|
| I2C1_SDA | PB7 | 1 |
| I2C1_SCL | PB6 | 20 (bonded) |

Note: These same pins are used by the system bootloader.

### I2C1 (BOOTLOADER MODE) - Fixed by ST
| Function | Pin |
|----------|-----|
| I2C1_SCL | PB6 |
| I2C1_SDA | PB7 |

Bootloader I2C address: **0x56** (7-bit)

### SPI1 (to MAX31855)
| Function | Pin | Package Pin |
|----------|-----|-------------|
| SPI1_SCK | PA1 | 8 |
| SPI1_MISO | PA11 | 15 |
| CS (GPIO) | PA0 | 7 |

Note: MAX31855 is read-only, no MOSI needed.

### Other
| Function | Pin | Package Pin |
|----------|-----|-------------|
| Relay/SSR | PA2 | 9 |
| SWDIO | PA13 | 17 |
| SWCLK | PA14 | 18 |
| BOOT0 | PA14 | 18 (shared with SWCLK) |

## Power Flow

```
AC Mains
    │
    ▼
┌─────────┐
│HLK-PM01 │ (AC-DC PSU)
└────┬────┘
     │ 5V
     ├──────────────────► CYD P1 VIN (powers CYD)
     │                         │
     │                         ▼
     │                    CYD internal LDO
     │                         │
     │                         ▼ 3.3V
     │                    CYD CN1 3.3V ──► STM32 VDD
     │
     └──► Power board 5V rail (if needed for relays)
```

## I2C Bootloader Programming

To update STM32 firmware from CYD over I2C:

1. **Enter bootloader mode**:
   - Set BOOT0 high on reset, OR
   - Configure option bytes for system memory boot

2. **I2C protocol**: See AN4221 for commands

3. **Address**: 0x56 (7-bit)

4. **Issue**: No spare CYD GPIO to control BOOT0
   - Solution: Use option bytes to enable software bootloader entry
   - Or: Add a 3rd wire from power board to CYD for BOOT0 control

## CYD Physical Dimensions

| Measurement | Size |
|-------------|------|
| PCB size | 101.5 × 54.9 mm |
| Display active area | 73.44 × 48.96 mm |
| Display diagonal | 3.5 inches |
| Resolution | 480 × 320 pixels |

## Relays

**Part**: Omron G5LE-1 (SPDT, 10A)

**Pinout**:
| Pin | Function |
|-----|----------|
| 1 | COM (Common) |
| 2 | Coil (-) |
| 3 | NC (Normally Closed) |
| 4 | NO (Normally Open) |
| 5 | Coil (+) |

**Wiring**: Use NO contact (Pin 4) for fail-safe operation - relay de-energizes → heater OFF.

**Flyback Diode**: 1N4148WQ-13-F (SOD-123, Diodes Inc, AEC-Q101 qualified)

Datasheet: http://www.omron.com/ecb/products/pdf/en-g5le.pdf

## AC Input Protection

Decision: **Fuse + MOV only** (skip X2 cap and EMI choke for hobby project)

| Component | Decision | Reason |
|-----------|----------|--------|
| Fuse | **Yes** | Safety - prevents fire on short |
| MOV | **Yes** | Cheap surge protection |
| X2 Cap | Skip | HLK-5M05 has internal filtering |
| EMI Choke | Skip | Only needed for EMC compliance |

### Fuse

**Spec**: 0.5A 250VAC slow-blow (per HLK-5M05 datasheet)

| Component | Part | LCSC # |
|-----------|------|--------|
| Fuse holder | Xucheng 5x20 clip (pair) | C3130 |
| Fuse | Littelfuse 0215.500MXP | C142839 |

KiCad footprint: `Fuse:Fuseholder_Clip-5x20mm_Littelfuse_111_Inline_P20.00x5.00mm_D1.05mm_Horizontal`

### MOV (Varistor)

**Part**: 10D561K (per HLK-5M05 datasheet recommendation)
- 10mm diameter
- 560V varistor voltage (for universal 100-240VAC input)

## Screw Terminals

**Part**: Phoenix Contact 1935161 (2-position, 5mm pitch)

| Type | Library | Name |
|------|---------|------|
| Symbol | Connector | Screw_Terminal_01x02 |
| Footprint | TerminalBlock_Phoenix | TerminalBlock_Phoenix_MKDS-1,5-2_1x02_P5.00mm_Horizontal |

## CYD Cable Connectors

CYD uses 1.25mm pitch JST GH-compatible connectors on P1 and CN1.

### PCB Header (for power board)

| Part | LCSC # | Notes |
|------|--------|-------|
| JST SM04B-GHS-TB(LF)(SN) | C189895 | SMD, right-angle, genuine JST |

KiCad:
- Symbol: `Connector:Conn_01x04_Pin`
- Footprint: `Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal`

### Cables

LCSC doesn't stock pre-made cables. Order from:
- AliExpress: "1.25mm 4pin JST cable" (cheapest, various lengths)
- Amazon: "JST GH 4 pin cable" or "PicoBlade 4 pin"

## BOM Summary (LCSC Parts)

| Component | Part Number | LCSC # | Qty |
|-----------|-------------|--------|-----|
| Fuse holder clips | Xucheng 5x20 | C3130 | 1 pair |
| Fuse 0.5A slow-blow | Littelfuse 0215.500MXP | C142839 | 1 |
| MOV | 10D561K | (search) | 1 |
| Flyback diode | 1N4148WQ-13-F | (search) | 2 |
| JST GH 4-pin header | SM04B-GHS-TB(LF)(SN) | C189895 | 2 |

## References

- CYD Pinout: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md
- CYD Specs: https://www.espboards.dev/esp32/cyd-esp32-3248s035/
- STM32G031 Datasheet: https://www.st.com/resource/en/datasheet/stm32g031c6.pdf
- AN2606 - STM32 boot modes: https://www.st.com/resource/en/application_note/cd00167594-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf
- AN4221 - I2C bootloader protocol: https://www.st.com/resource/en/application_note/cd00264321-i2c-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf
- Omron G5LE-1 Datasheet: http://www.omron.com/ecb/products/pdf/en-g5le.pdf
- macsbug CYD info: https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/
