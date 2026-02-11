# Hardware Architecture Decisions

Notes from design discussion - February 2026

## Two-Board Architecture

### CYD Display Board (off-the-shelf)
- **Model**: ESP32-3248S035C (Cheap Yellow Display)
- **Display**: 3.5" 320x480 TFT ST7796, capacitive touch (GT911)
- **MCU**: ESP32-WROOM-32
- **Role**: WiFi, Telegram, touchscreen UI, UART master

### Power Board (custom PCB)
- **MCU**: STM32G031F6P6 (TSSOP-20)
- **Role**: UART to CYD, relay control, temperature sensing, local safety logic

## Why STM32G031?
- STM32G031 vs G030: G031 has **125°C temperature rating** (important near heating element)
- Cortex-M0+, ~$1, TSSOP-20 easy to hand solder
- Built-in UART bootloader for firmware updates via CYD (PA9/PA10)

## CYD Connector

### P1 (4-pin 1.25mm JST) - Power + UART
| Pin | Function |
|-----|----------|
| VIN | 5V input from power board |
| TX | UART TX → STM32 PA10 (USART1_RX) |
| RX | UART RX ← STM32 PA9 (USART1_TX) |
| GND | Ground |

CN1 connector is **not used** — the power board has a local 3.3V LDO (AP2112K-3.3) instead of receiving 3.3V from the CYD.

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

### USART1 (to CYD) - ACTIVE MODE
| Function | Pin | Package Pin |
|----------|-----|-------------|
| USART1_TX | PA9 | 13 |
| USART1_RX | PA10 | 14 |

Note: These same pins are used by the system UART bootloader.

### USART1 (BOOTLOADER MODE) - Fixed by ST
| Function | Pin |
|----------|-----|
| USART1_TX | PA9 |
| USART1_RX | PA10 |

Bootloader baud rate: auto-detected (send 0x7F sync byte)

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
│HLK-5M05 │ (AC-DC PSU)
└────┬────┘
     │ 5V
     ├──────────────────► CYD P1 VIN (powers CYD)
     │
     ├──► AP2112K-3.3 LDO ──► 3.3V Rail
     │                           ├──► STM32 VDD
     │                           └──► MAX31855 VCC
     │
     └──► Relay coil supply (5V)
```

## 3.3V LDO Regulator

The power board generates its own 3.3V locally instead of receiving it from the CYD:

| Parameter | Value |
|-----------|-------|
| Part | AP2112K-3.3TRG1 (Diodes Inc) |
| Package | SOT-23-5 |
| Input | 5V from HLK-5M05 |
| Output | 3.3V, 600mA max |
| Load | STM32 + MAX31855 (~55mA max) |
| Input cap | 1uF ceramic |
| Output cap | 1uF ceramic |

## UART Bootloader Programming

To update STM32 firmware from CYD over UART:

1. **Enter bootloader mode**:
   - **Software jump** (preferred): STM32 app firmware receives a "reboot to bootloader" UART command, then jumps to system memory. No extra GPIO needed.
   - **Hardware fallback**: Physical button on power board to force BOOT0 high during reset.

2. **UART protocol**: See AN3155 for commands

3. **Sync**: Send 0x7F byte, bootloader auto-detects baud rate

4. **Pins**: PA9 (TX) / PA10 (RX) — same pins used for application UART, so no rewiring needed

## Protection Circuits

### UART Series Resistors

**Problem**: The CYD's P1 connector shares UART0 (TX/RX) with the onboard CH340 USB-to-serial chip. When the CYD is programmed via USB, both the CH340 and STM32 can drive the same UART lines simultaneously, causing bus contention. Additionally, if the STM32 is unpowered while the CYD is connected, the CYD's TX signal can forward-bias the STM32's ESD clamp diodes and inject current into an unpowered chip.

**Solution**: Two 1K series resistors on the UART TX and RX lines, placed on the power board between the STM32 (PA9/PA10) and the P1 JST connector.

| Resistor | Location | Purpose |
|----------|----------|---------|
| R_TX (1K) | STM32 PA9 → P1 RX pin | Limits current during bus contention with CH340 |
| R_RX (1K) | STM32 PA10 ← P1 TX pin | Limits current through STM32 ESD clamp diodes when STM32 unpowered |

1K is low enough to not affect UART signal integrity at typical baud rates (115200 baud), but high enough to limit contention current to ~3.3mA.

### 5V Backfeed Protection (P-FET Pass Transistor)

**Problem**: When the CYD is powered via USB (for programming/debugging) and the power board is unplugged from mains, the CYD's 5V USB supply can backfeed through the P1 VIN line into the HLK-5M05 output. This is undesirable -- the HLK-5M05 is not designed to have voltage applied to its output.

**Solution**: A P-channel MOSFET (AO3401 or SI2301, SOT-23) as a high-side pass transistor between the HLK-5M05 output and the 5V rail.

**Circuit**:
```
HLK-5M05 Output ──► P-FET Source
                     P-FET Drain ──► 5V Rail (to P1 VIN, LDO, relay)
                     P-FET Gate ──► 100K resistor to GND
                     Gate-to-Source: 100nF cap (Miller coupling suppression)
```

**How it works**:
- **Normal operation** (mains powered): HLK-5M05 outputs 5V on the source pin. Gate is pulled to GND via 100K resistor, so Vgs = -5V, turning the P-FET fully on. Voltage drop is near-zero (~27mV at 500mA with AO3401).
- **USB backfeed** (mains off): 5V appears on the drain (from CYD P1 VIN). The body diode is reverse-biased (anode on source/HLK side which is 0V, cathode on drain/5V-rail side). Gate is at GND, but Vgs = 0V (source is at 0V), so the FET is off. Current is blocked.
- **100nF gate-to-source cap**: Suppresses Miller coupling during USB contact bounce, preventing momentary gate voltage spikes from turning the FET on during cable insertion/removal.

### Why P-FET Over Schottky Diode

A Schottky diode would be simpler, but the CYD uses AMS1117-3.3 LDOs which have a relatively high dropout voltage (~1.1V). A Schottky diode's ~0.3V forward drop would reduce the 5V supply to ~4.7V, leaving only ~0.3V of headroom above the LDO's minimum input (3.3V + 1.1V = 4.4V). This is too tight for reliable operation, especially with tolerance and temperature variation.

The P-FET gives the full 5V to the CYD with negligible voltage drop (~27mV), preserving the full margin for the AMS1117-3.3 LDO.

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

CYD uses 1.25mm pitch JST GH-compatible connectors. Only P1 is used (single cable).

### PCB Header (for power board)

| Part | LCSC # | Notes |
|------|--------|-------|
| JST SM04B-GHS-TB(LF)(SN) | C189895 | SMD, right-angle, genuine JST (x1) |

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
| JST GH 4-pin header | SM04B-GHS-TB(LF)(SN) | C189895 | 1 |
| LDO 3.3V | AP2112K-3.3TRG1 | (search) | 1 |
| P-FET (backfeed protection) | AO3401 or SI2301 | (search) | 1 |
| Gate resistor | 100K 0402/0603 | (search) | 1 |
| Gate-source cap | 100nF 0402/0603 | (search) | 1 |
| UART series resistors | 1K 0402/0603 | (search) | 2 |

## References

- CYD Pinout: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md
- CYD Specs: https://www.espboards.dev/esp32/cyd-esp32-3248s035/
- STM32G031 Datasheet: https://www.st.com/resource/en/datasheet/stm32g031c6.pdf
- AN2606 - STM32 boot modes: https://www.st.com/resource/en/application_note/cd00167594-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf
- AN3155 - UART bootloader protocol: https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf
- Omron G5LE-1 Datasheet: http://www.omron.com/ecb/products/pdf/en-g5le.pdf
- macsbug CYD info: https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/
