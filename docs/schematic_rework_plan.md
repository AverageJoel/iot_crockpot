# Schematic Rework Plan: Two-Board → Single-Board ESP32-S3

Goal: Adapt the existing KiCad schematic (designed for CYD + STM32 two-board system) to the new
single-board ESP32-S3-WROOM-1-N4R2 design.

The AC power supply section is fully reusable. The MCU and inter-board interface sections need
to be replaced.

---

## Overview of Changes

| Action | What |
|--------|------|
| **Keep** | Fuse + MOV + HLK-5M05 AC supply |
| **Keep** | AP2112K-3.3 LDO (still needed — ESP32-S3 module takes 3.3V, no internal 5V reg) |
| **Keep** | Omron G5LE-1 relay + 1N4148 flyback diode |
| **Keep** | Screw terminals (AC mains in, relay out) |
| **Keep** | Backfeed P-FET (now protects HLK from USB-C VBUS instead of from CYD P1) |
| **Remove** | STM32G031F6P6 and all its passives |
| **Remove** | 1K UART series resistors (R_TX, R_RX) |
| **Remove** | JST GH SM04B P1 4-pin connector (CYD cable) |
| **Add** | ESP32-S3-WROOM-1-N4R2 module |
| **Add** | USB-C connector + 22Ω D+/D- resistors + 5.1K CC pull-downs |
| **Add** | BOOT button + EN/Reset button |
| **Add** | 14-pin display header (ST7796 + FT6336U) |
| **Add** | Relay driver circuit (NPN transistor replaces STM32 direct drive) |
| **Modify** | MAX31855 SPI CS now goes to ESP32-S3 GPIO |

---

## Step 1: Remove STM32 Section

Delete from schematic:
- STM32G031F6P6 symbol
- All decoupling caps on STM32 VDD pins
- NRST RC filter (100nF cap + any pull-up)
- BOOT0 pull-down resistor and optional button
- SWD header (if present)
- UART series resistors R_TX (1K, PA9 → P1) and R_RX (1K, PA10 ← P1)
- JST GH SM04B 4-pin header (P1) for CYD cable

After removal, the MAX31855 SPI lines (CS, SCK, MISO) will be dangling — leave them
temporarily, they get reconnected in Step 5.

---

## Step 2: Verify / Update Power Supply Section

### Keep as-is:
- Fuse holder + 0.5A slow-blow fuse (C3130, C142839)
- MOV 10D561K across AC input
- HLK-5M05 AC-DC module

### Backfeed protection (P-FET) — keep but update net labels:
The P-FET (AO3401) is still needed. In the old design it blocked USB 5V from backfeeding
through P1 VIN into the HLK-5M05. In the new design it does the same job, now blocking
USB-C VBUS from backfeeding into HLK-5M05 via the shared 5V rail.

Circuit is identical:
```
HLK-5M05 Out → P-FET Source
P-FET Drain  → 5V Rail (to LDO, display, relay coil)
P-FET Gate   → 100K to GND (turns FET on when HLK powered)
Gate-Source  → 100nF cap (Miller coupling suppression)
```

If the P-FET circuit was pending/not yet drawn, add it now between the HLK output and
the 5V power rail net.

### AP2112K-3.3 LDO — keep:
- Input: 5V rail
- Output: 3V3 rail
- Decoupling: 1uF ceramic on input, 1uF ceramic on output
- This powers ESP32-S3 module (3.3V) + MAX31855 (3.3V)

Note: The ST7796 display module takes 5V input directly (it has onboard level shifters),
so it comes off the 5V rail, not the 3V3 rail.

---

## Step 3: Add ESP32-S3-WROOM-1-N4R2

### Symbol
- KiCad built-in: `RF_Module:ESP32-S3-WROOM-1` (check Espressif KiCad library)
- If not available: create from module datasheet or use a generic 38-pin header

### Power connections
- All VDD / 3V3 pins → 3V3 power net
- All GND pins → GND
- EN pin → 10K pull-up to 3V3 + 100nF cap to GND (RC filter) + EN button to GND

### Decoupling
- 100nF ceramic cap on each VDD pin (place near pin)
- 10uF bulk cap on 3V3 rail (one instance is fine)

### GPIO pin assignments (see table below)

### Boot/Reset buttons
```
BOOT button:  GPIO0 → button → GND
              GPIO0 → 10K pullup → 3V3

EN button:    EN pin → button → GND
              EN pin → 10K pullup → 3V3 (already handled by RC above)
              100nF cap EN to GND
```

### Strapping pin notes
| Pin | Concern | Action |
|-----|---------|--------|
| GPIO0 | Low on boot → download mode | BOOT button pulls low; float = high (normal boot) |
| GPIO45 | Sets VDD_SPI (3.3V vs 1.8V flash) | Leave floating or pull to GND (3.3V flash) |
| GPIO46 | ROM log enable | Leave floating |
| GPIO19 | USB D- | See Step 4 |
| GPIO20 | USB D+ | See Step 4 |

---

## Step 4: Add USB-C Connector

### Purpose
Native USB Serial/JTAG on ESP32-S3 — no CH340 or CP2102 needed. Used for:
- Firmware flashing
- Serial monitor / printf debugging
- JTAG debugging

### Circuit
```
ESP32-S3                       USB-C Connector
GPIO19 (D-) ──[22Ω]──────────── D- (pin A7/B7)
GPIO20 (D+) ──[22Ω]──────────── D+ (pin A6/B6)

USB-C CC1 ──[5.1K]── GND      (identifies device to host: 5V/900mA)
USB-C CC2 ──[5.1K]── GND

USB-C VBUS ──────────────────── 5V rail
             (5V rail also fed by HLK-5M05 via P-FET — both sources share the rail)
```

### Symbol / Footprint
- Symbol: `Connector:USB_C_Receptacle_USB2.0` (KiCad built-in)
- Footprint: SMD USB-C receptacle (search KiCad footprint library for USB_C)

---

## Step 5: Add Display Header (14-pin)

The ST7796 display module (lcdwiki MSP3526 / Waveshare / Hosyond) uses a standard
14-pin header. Wire to ESP32-S3 GPIOs per the table in Step 7.

### Header pinout
| Pin | Label | Function | ESP32-S3 GPIO |
|-----|-------|----------|---------------|
| 1 | VCC | 3.3V | 3V3 rail |
| 2 | GND | Ground | GND |
| 3 | LCD_CS | LCD chip select | GPIO10 |
| 4 | LCD_RST | LCD reset | GPIO8 |
| 5 | LCD_RS/DC | Command/data select | GPIO9 |
| 6 | SDI (MOSI) | SPI data in | GPIO11 |
| 7 | SCK | SPI clock | GPIO12 |
| 8 | LED | Backlight | GPIO47 |
| 9 | SDO (MISO) | SPI data out | GPIO13 |
| 10 | CTP_SCL | Touch I2C clock | GPIO4 |
| 11 | CTP_RST | Touch reset | GPIO6 |
| 12 | CTP_SDA | Touch I2C data | GPIO5 |
| 13 | CTP_INT | Touch interrupt | GPIO7 |
| 14 | SD_CS | SD card chip select | GPIO14 |

Note: LCD and SD card share the SPI bus (SCK/MOSI/MISO). Touch uses separate I2C bus.
Note: Display module VCC is 3.3V (module has level shifters for 5V logic, but ESP32-S3
is 3.3V — use 3.3V input, the level shifters pass through fine).

### Symbol / Footprint
- Symbol: `Connector:Conn_01x14_Pin` or create custom symbol with signal names
- Footprint: 2.54mm pitch 14-pin single-row header
  (`Connector_PinHeader_2.54mm:PinHeader_1x14_P2.54mm_Vertical`)

---

## Step 6: Update MAX31855 Thermocouple Section

If MAX31855 was already partially drawn, update the CS net label to connect to the
ESP32-S3 GPIO5.

### MAX31855 SPI connections
| Signal | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| CS | GPIO5 | Active low |
| SCK | GPIO12 | Shared SPI bus |
| MISO | GPIO13 | Shared SPI bus |
| MOSI | N/A | MAX31855 is read-only |

### If not yet drawn, add:
- MAX31855KASA symbol (SPI thermocouple IC)
- VCC → 3V3, GND → GND
- 100nF decoupling cap on VCC
- K-type thermocouple connector (2-pin screw terminal or K-type jack)
- CS, SCK, MISO to ESP32-S3 GPIOs

---

## Step 7: Add Relay Driver Circuit

The STM32 previously drove the relay coil directly. The ESP32-S3 GPIO outputs 3.3V
and cannot drive a 5V relay coil directly. Add an NPN transistor driver.

### Circuit
```
ESP32-S3 GPIO4 ──[1K]── NPN Base
                         NPN Emitter → GND
                         NPN Collector → Relay Coil (-)
                                         Relay Coil (+) → 5V rail
                                         1N4148 flyback diode across coil (cathode to 5V)
```

### Component
- NPN transistor: 2N3904 (SOT-23, LCSC C20526) or 2N2222A
- Base resistor: 1K 0402/0603
- Flyback diode: 1N4148WQ-13-F (SOD-123, already in BOM)

Verification: Relay coil resistance = 56Ω, coil current = 5V/56Ω ≈ 89mA.
With 3.3V GPIO and 1K base resistor: Ib = (3.3-0.7)/1K = 2.6mA.
2N3904 hFE ≥ 100 → Ic_sat = 260mA >> 89mA. Transistor saturates fully. ✓

The existing relay (G5LE-1) and flyback diode symbol/footprint are unchanged.

---

## Step 8: GPIO Assignment Summary

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI SCK | GPIO12 | Shared: LCD, SD, MAX31855 |
| SPI MOSI | GPIO11 | Shared: LCD, SD |
| SPI MISO | GPIO13 | Shared: LCD, SD, MAX31855 |
| LCD_CS | GPIO10 | |
| LCD_DC | GPIO9 | |
| LCD_RST | GPIO8 | |
| LCD_BL | GPIO47 | Backlight (PWM capable) |
| SD_CS | GPIO14 | SD card on display module |
| CTP_SDA | GPIO5 | I2C bus |
| CTP_SCL | GPIO4 | I2C bus |
| CTP_RST | GPIO6 | |
| CTP_INT | GPIO7 | |
| MAX31855_CS | GPIO16 | |
| Relay | GPIO17 | Via 2N7002 MOSFET driver |
| USB D- | GPIO19 | Fixed |
| USB D+ | GPIO20 | Fixed |
| BOOT | GPIO0 | Strapping / BOOT jumper |
| UART0_TX | GPIO43 | Debug serial (optional header) |
| UART0_RX | GPIO44 | Debug serial (optional header) |

Spare GPIOs (≥15 remaining): GPIO1, GPIO2, GPIO3, GPIO15, GPIO18, GPIO21,
GPIO35–GPIO42, GPIO45, GPIO46, GPIO48

---

## Step 9: Optional — Debug UART Header

Add a 3-pin header (TX, RX, GND) for UART0 debug output. Useful before USB firmware
is working. 2.54mm pitch.

- Pin 1: GPIO43 (UART0_TX)
- Pin 2: GPIO44 (UART0_RX)
- Pin 3: GND

---

## Step 10: ERC + Final Check

After all changes:

1. Run KiCad ERC (Electrical Rules Check) and resolve errors
2. Verify all power pins have power flags where needed
3. Check net labels are consistent (no orphan nets)
4. Verify P-FET circuit is between HLK output and 5V rail net
5. Confirm GPIO0 has both the 10K pullup and the BOOT button
6. Confirm EN has 10K pullup + 100nF + button

---

## New BOM Additions

| Component | Part | Package | LCSC # |
|-----------|------|---------|--------|
| ESP32-S3-WROOM-1-N4R2 | MCU module | — | C2913203 |
| USB-C receptacle | SMD USB-C | SMD | (search) |
| 22Ω resistor (x2) | USB D+/D- | 0402 | (search) |
| 5.1K resistor (x2) | USB CC1/CC2 | 0402 | (search) |
| 10K resistor (x3) | GPIO0 pullup, EN pullup, NPN base | 0402 | (search) |
| 1K resistor (x1) | NPN base resistor | 0402 | (search) |
| 100nF cap (x3) | EN filter, NPN gate, USB D± | 0402 | (search) |
| 10uF cap (x1) | 3V3 bulk | 0402/0603 | (search) |
| NPN transistor | 2N3904 | SOT-23 | C20526 |
| 14-pin header | Display connector | 2.54mm | (search) |
| 3-pin header | Debug UART | 2.54mm | (search) |

### Removed from BOM

| Component | Reason |
|-----------|--------|
| STM32G031F6P6 | Replaced by ESP32-S3 |
| JST SM04B-GHS-TB 4-pin | CYD cable interface no longer needed |
| 1K resistors (UART protection) | No bus contention issue |
| STM32 passives (decoupling, NRST, BOOT0) | Removed with STM32 |

---

## References

- [ESP32-S3-WROOM-1 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
- [ST7796 Display Module 14-pin interface](https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796)
- [hardware_decisions.md](hardware_decisions.md) — full architecture rationale
