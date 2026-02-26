# Wiring: XIAO ESP32-C3 → LCD Wiki 3.5" ST7796 Display

## Sources
- Display datasheet: [LCD Wiki 3.5" IPS SPI Module ST7796](https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796)
- MCU: [Seeed Studio XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- GPIO assignments: `firmware/main/Kconfig.projbuild` (branch `feature/xiao-esp32c3-display-test`)

---

## Voltage notes

The ST7796 module is rated **5V input** with an **onboard level conversion circuit**.
This means:
- **VCC must connect to the XIAO's 5V pin** (not 3V3) — the module has an internal LDO
- **Signal lines (SPI, I2C) are safe at 3.3V** — the level converter handles the translation
- The 5V pin on the XIAO is only live when the board is powered via USB

---

## Wiring table

| Display pin | Signal name  | → | XIAO pin | GPIO   | Notes                          |
|:-----------:|--------------|---|----------|--------|--------------------------------|
| 1           | VCC          | → | **5V**   | —      | Module power — must be 5V      |
| 2           | GND          | → | GND      | —      |                                |
| 3           | LCD_CS       | → | D3       | GPIO5  | LCD chip select                |
| 4           | LCD_RST      | → | D1       | GPIO3  | LCD reset                      |
| 5           | LCD_RS (DC)  | → | D2       | GPIO4  | Data/command select            |
| 6           | SDI (MOSI)   | → | D10      | GPIO10 | SPI data to display            |
| 7           | SCK          | → | D8       | GPIO8  | SPI clock                      |
| 8           | LED (BL)     | → | D0       | GPIO2  | Backlight — HIGH = on          |
| 9           | SDO (MISO)   | → | D9       | GPIO9  | SPI data from display          |
| 10          | CTP_SCL      | → | D5       | GPIO7  | Touch I2C clock                |
| 11          | CTP_RST      | → | **3V3**  | —      | Tie high to keep touch active  |
| 12          | CTP_SDA      | → | D4       | GPIO6  | Touch I2C data                 |
| 13          | CTP_INT      | → | —        | —      | Leave **unconnected** (polled) |
| 14          | SD_CS        | → | —        | —      | Leave **unconnected** (no SD)  |

---

## ASCII wiring diagram

```
XIAO ESP32-C3                         LCD Wiki 3.5" ST7796
(USB-C connector at top)              (14-pin 2.54mm header)

        ┌──────────────┐
        │  USB-C       │
        │              │
  5V ───┤ 5V           ├──────────────────── Pin 1  VCC
 GND ───┤ GND          ├──────────────────── Pin 2  GND
 3V3 ───┤ 3V3          ├──────────────────── Pin 11 CTP_RST  (hold high)
        │              │
  D0 ───┤ GPIO2        ├──────────────────── Pin 8  LED      (backlight)
  D1 ───┤ GPIO3        ├──────────────────── Pin 4  LCD_RST
  D2 ───┤ GPIO4        ├──────────────────── Pin 5  LCD_RS   (DC)
  D3 ───┤ GPIO5        ├──────────────────── Pin 3  LCD_CS
  D4 ───┤ GPIO6  (SDA) ├──────────────────── Pin 12 CTP_SDA  (I2C)
  D5 ───┤ GPIO7  (SCL) ├──────────────────── Pin 10 CTP_SCL  (I2C)
  D6 ───┤ GPIO21 (TX)  │  serial monitor via CH340 — do not use
  D7 ───┤ GPIO20 (RX)  │  serial monitor via CH340 — do not use
  D8 ───┤ GPIO8  (SCK) ├──────────────────── Pin 7  SCK      (SPI)
  D9 ───┤ GPIO9  (MISO)├──────────────────── Pin 9  SDO/MISO (SPI)
 D10 ───┤ GPIO10 (MOSI)├──────────────────── Pin 6  SDI/MOSI (SPI)
        │              │
        │  RST  BOOT   │                     Pin 13 CTP_INT  — leave NC
        └──────────────┘                     Pin 14 SD_CS    — leave NC
```

---

## Checklist before powering on

- [ ] VCC connected to **5V** (not 3V3)
- [ ] GND connected
- [ ] 3V3 connected to Pin 11 (CTP_RST)
- [ ] Pins 13 and 14 left unconnected
- [ ] D6 and D7 (GPIO20/21) left free for serial monitor
- [ ] No shorts between adjacent header pins (use a continuity tester)

---

## After flashing

The expected boot sequence on serial monitor:

```
I (xxx) display_driver: Initializing ST7796 SPI display
I (xxx) touch_driver:   Initializing FT6336U touch (SDA=6 SCL=7 RST=-1 INT=-1)
I (xxx) temperature:    Mock temperature mode — no MAX31855
I (xxx) relay:          Mock relay mode — no relay GPIO
I (xxx) display:        Display initialized — LVGL running (480x320)
```

If the screen stays white/black with no output, check:
1. VCC is on 5V, not 3V3
2. SCK/MOSI/CS/DC are not swapped
3. SPI clock polarity — ST7796 uses SPI Mode 0 (CPOL=0, CPHA=0)
