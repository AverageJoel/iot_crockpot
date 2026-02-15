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

## Architecture Revision Under Consideration (Feb 2026)

Considering replacing the two-board CYD + STM32 design with a single-board approach using a bare ESP32-S3-WROOM module and a standalone SPI display.

### Problem with CYD Approach

The CYD (ESP32-3248S035C) has almost no free GPIO — only TX/RX on the P1 connector are available. This forced a two-MCU architecture (CYD + STM32) with UART protocol, bootloader flashing, and bus contention protection — significant complexity for a simple relay + thermocouple project.

### Proposed New Architecture

Single custom PCB with:
- **MCU**: ESP32-S3-WROOM (bare module, ~33 usable GPIOs)
- **Display**: 3.5" IPS SPI module with ST7796 controller + capacitive touch (lcdwiki.com MSP3525/3526)
- **No STM32** — ESP32-S3 handles everything directly (WiFi, display, relay, thermocouple)

### Display Module: 3.5" IPS SPI ST7796 (MSP3525/3526)

14-pin interface:

| Pin | Label | Interface | Function |
|-----|-------|-----------|----------|
| 1 | VCC | Power | 3.3V |
| 2 | GND | Power | Ground |
| 3 | LCD_CS | SPI | LCD chip select |
| 4 | LCD_RST | GPIO | LCD reset |
| 5 | LCD_RS | GPIO | Command/data select (DC) |
| 6 | SDI(MOSI) | SPI | Shared SPI data in |
| 7 | SCK | SPI | Shared SPI clock |
| 8 | LED | GPIO | Backlight control |
| 9 | SDO(MISO) | SPI | Shared SPI data out |
| 10 | CTP_SCL | I2C | Touch I2C clock |
| 11 | CTP_RST | GPIO | Touch reset |
| 12 | CTP_SDA | I2C | Touch I2C data |
| 13 | CTP_INT | GPIO | Touch interrupt |
| 14 | SD_CS | SPI | SD card chip select |

LCD and SD card share the SPI bus. Touch uses a separate I2C bus.

**Key specs:** ST7796U/S controller, FT6336U capacitive touch (I2C, 2-point), 320x480 IPS, 16.7M colors, 300 cd/m², 5V input (onboard level shifters). Module size ~55.5 x 98.0 mm.

### Why ST7796 and Not ILI9488

Several similar 3.5" SPI displays use the ILI9488 controller instead. Avoid these:

| Feature | ST7796 | ILI9488 |
|---|---|---|
| **SPI 16-bit color (RGB565)** | Yes | **No** — 18/24-bit only over SPI |
| **Bytes per pixel (SPI)** | 2 | 3 (33% slower) |
| **MISO tristate when CS high** | Yes | **No** — causes SPI bus conflicts |
| **Shared SPI with SD card** | Works fine | Problematic without workaround |
| **Max SPI clock** | 80 MHz | ~60 MHz |

The ILI9488's lack of 16-bit SPI color and broken MISO tristate make it a poor choice for a shared SPI bus design (LCD + SD card + MAX31855 thermocouple all on one bus).

### Display Sourcing

All ST7796 + capacitive touch modules below share the same 14-pin interface and are electrically interchangeable. LCDWIKI (MSP3525/3526) is the OEM reference design; the others are resellers or clones.

LCSC does not stock assembled display modules.

| Source | Price | Shipping | Link |
|--------|-------|----------|------|
| **Hosyond (Amazon)** | ~$15-17 | Prime | [Amazon B0CMD7Y55M](https://www.amazon.com/Hosyond-320x480-Capacitive-ST7796U-Mega2560/dp/B0CMD7Y55M) |
| **Elecrow** | ~$16.90 | Varies | [elecrow.com](https://www.elecrow.com/3-5-ips-spi-lcd-capacitive-touch-module-st7796-driver-320-480-resolution.html) |
| **Waveshare** | ~$18.99 | Direct or Amazon | [waveshare.com](https://www.waveshare.com/3.5inch-capacitive-touch-lcd.htm) |
| **AliExpress generics** | ~$8-15 | 2-4 weeks | Search "3.5 ST7796 SPI capacitive touch" (verify not ILI9488) |
| **ProtoSupplies** | ~$19.95 | US domestic | [protosupplies.com](https://protosupplies.com/product/ips-35-st7796/) |

Documentation/examples:
- [Elecrow wiki](https://www.elecrow.com/wiki/3.5-inch_IPS_SPI_LCD_Capacitive_Touch_Display_Module_With_ST7796_Driver-320x480_Resolution_Arduino_Compatible.html) (ESP32, STM32, Arduino examples)
- [Waveshare wiki](https://www.waveshare.com/wiki/3.5inch_Capacitive_Touch_LCD) (ESP32, Pico, RPi examples)

### MCU Comparison

| | XIAO ESP32-C3 | XIAO ESP32-S3 | Bare ESP32-S3-WROOM |
|---|---|---|---|
| **Usable GPIOs** | 11 | 13 | ~33 |
| **Pins needed** | 15 | 15 | 15 |
| **Verdict** | Not enough (-4) | Barely fits with tricks (-2) | Plenty (+18 spare) |
| **UART debug** | Sacrificed | Sacrificed | Available |
| **WiFi** | 2.4GHz, single-core | 2.4GHz, dual-core | 2.4GHz, dual-core |
| **USB** | CH340 serial | Native USB-OTG | Native USB-OTG |
| **Cost** | ~$5 | ~$8 | ~$3 (module only) |
| **PCB effort** | Plug-in | Plug-in | Full breakout needed |

### ESP32 Module Comparison (Bare Modules)

Evaluated which ESP32 variant to use for the single-board design:

| | **ESP32-WROOM-32E** | **ESP32-S3-WROOM-1** | **ESP32-C6-WROOM-1** |
|---|---|---|---|
| **Usable GPIOs** | ~25 | ~36 | ~22 |
| **Spare after project** | ~10 | ~20 | ~7 |
| **CPU** | Dual-core Xtensa LX6 | Dual-core Xtensa LX7 | Single-core RISC-V |
| **Native USB** | No (needs CH340/CP2102) | Yes (OTG + Serial/JTAG) | Serial/JTAG only |
| **WiFi** | 802.11n | 802.11n | 802.11ax (WiFi 6) |
| **LCSC price (qty 1)** | ~$5 | ~$5.30 | ~$5 |
| **LCSC price (bulk)** | ~$2.55 | ~$3.46 | ~$3.24 |
| **Maturity** | Most mature | Mature, well supported | Newer, smaller community |

ESP32-C3 rejected — too tight on GPIOs (same problem as XIAO).
ESP32-S2 rejected — single-core (risky for WiFi + LVGL), being phased out.

**Decision: ESP32-S3-WROOM-1-N4R2** (4MB flash, 2MB PSRAM). Native USB eliminates the need for a USB-UART bridge chip. Dual-core gives headroom for WiFi + display. Plenty of spare GPIOs.

- LCSC: [ESP32-S3-WROOM-1-N4R2 (C2913203)](https://www.lcsc.com/product-detail/C2913203.html)

### ESP32-S3 USB & Debugging

The ESP32-S3 has a built-in USB Serial/JTAG controller — no external USB-UART bridge chip needed. GPIO19 (D-) and GPIO20 (D+) connect directly to a USB-C connector.

**USB circuit (per Espressif hardware design guidelines):**
```
ESP32-S3                          USB-C Connector
GPIO19 (D-) ──[22Ω]──┬────────── D-
                      (C to GND, optional footprint)
GPIO20 (D+) ──[22Ω]──┬────────── D+
                      (C to GND, optional footprint)
```

This provides:
- Serial programming/flashing
- Serial monitor (printf debugging)
- JTAG debugging (built-in, no external adapter needed)

ESP32 uses JTAG, not SWD (SWD is ARM Cortex-M specific). The built-in USB-JTAG is sufficient for most development. Dedicated JTAG pins (GPIO44/45/47/48) are also available for external adapters if needed.

Reference design: [ESP32-S3 Hardware Design Guidelines - Schematic Checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)

### Pin Budget (15 signals required)

| Signal | Bus | Notes |
|--------|-----|-------|
| SPI SCK | SPI (shared) | LCD + SD card + MAX31855 |
| SPI MOSI | SPI (shared) | |
| SPI MISO | SPI (shared) | |
| LCD_CS | GPIO | |
| LCD_RS/DC | GPIO | |
| LCD_RST | GPIO | Could tie to 3.3V via RC to save a pin |
| LED (backlight) | GPIO | Could tie to 3.3V (always on) to save a pin |
| SD_CS | GPIO | |
| I2C SDA | I2C | Capacitive touch |
| I2C SCL | I2C | Capacitive touch |
| CTP_RST | GPIO | Could tie to 3.3V to save a pin |
| CTP_INT | GPIO | Could poll instead to save a pin |
| MAX31855_CS | GPIO | Thermocouple SPI chip select |
| Relay | GPIO | SSR/relay drive |
| (spare) | — | ESP32-S3-WROOM has ~18 pins remaining |

### Benefits Over Current CYD Design

- **One MCU** instead of two — eliminates STM32, UART protocol, bootloader complexity
- **One board** instead of two — simpler assembly, lower cost
- **No bus contention** — no CH340 fighting STM32 on shared UART
- **No backfeed protection needed** — no P-FET circuit required (USB powers ESP32 directly)
- **Full GPIO flexibility** — 18+ spare pins for expansion (buzzer, LEDs, extra sensors)
- **Single firmware** — one codebase, one flash target

### Trade-offs

- Lose CYD's pre-built form factor and enclosure
- More PCB design work (ESP32 antenna keep-out, USB-C, boot/reset buttons)
- Lose independent fail-safe MCU (mitigated by ESP32 watchdog timer)

### Decision Status

**Decided: ESP32-S3-WROOM-1-N4R2 single-board design.**

### References

- Display (OEM reference): https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796
- ESP32-S3 hardware design guidelines: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html
- ESP32-S3 JTAG debugging: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/index.html
- ESP32-S3 USB Serial/JTAG: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html
- ESP32-S3 pin reference: https://www.atomic14.com/2023/11/21/esp32-s3-pins
- XIAO ESP32-C3: https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/
- XIAO ESP32-S3: https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/

## References

- CYD Pinout: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md
- CYD Specs: https://www.espboards.dev/esp32/cyd-esp32-3248s035/
- STM32G031 Datasheet: https://www.st.com/resource/en/datasheet/stm32g031c6.pdf
- AN2606 - STM32 boot modes: https://www.st.com/resource/en/application_note/cd00167594-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf
- AN3155 - UART bootloader protocol: https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf
- Omron G5LE-1 Datasheet: http://www.omron.com/ecb/products/pdf/en-g5le.pdf
- macsbug CYD info: https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/
