# Display Options for IoT Crockpot

This document lists potential display options for the crockpot controller, ranging from full touchscreen displays to budget alternatives.

## Requirements

- Size: 2-4 inches
- Touch: Resistive or capacitive (see comparison below)
- Interface: SPI for display, SPI or I2C for touch
- Voltage: 3.3V compatible (ESP32-C3)
- Budget: As cheap as practical

## Resistive vs Capacitive Touch

| Feature | Resistive | Capacitive |
|---------|-----------|------------|
| **Price** | $3-8 | $10-18 |
| **Responsiveness** | Good | Excellent |
| **Pressure required** | Yes | No (hover works) |
| **Multi-touch** | No | Yes (some) |
| **Stylus/gloves** | Works | Usually no |
| **Durability** | Can wear out | Very durable |
| **Interface** | SPI (XPT2046) | I2C (FT6236) |
| **Extra pins needed** | 2 (shares SPI) | 2-3 (separate I2C) |

**For a crockpot:** Resistive is likely fine - simple UI, occasional use, and much cheaper.

---

## Option 1: All-in-One ESP32 + Display Boards

These replace the XIAO ESP32-C3 with an integrated solution.

| Product | Size | Resolution | Touch | Price | Notes |
|---------|------|------------|-------|-------|-------|
| ESP32-2432S028 "CYD" (Capacitive) | 2.8" | 320x240 | Capacitive | $12-18 | Huge community support |
| ESP32-2432S028R "CYD" (Resistive) | 2.8" | 320x240 | Resistive | $10-15 | Cheaper but less responsive |
| ESP32-2432S024 | 2.4" | 320x240 | Resistive | $10-14 | Smaller form factor |

**Pros:**
- Simplest integration (no wiring)
- Well documented
- Includes WiFi/BT, SD card slot, RGB LED

**Cons:**
- Replaces existing XIAO ESP32-C3
- Locked to ESP32-WROOM-32 (not C3)

**Links:**
- [CYD GitHub Community](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [Random Nerd Tutorials Guide](https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/)

---

## Option 2: Resistive Touch TFT Modules (CHEAPEST TOUCHSCREEN)

Resistive touch is cheaper than capacitive. Uses XPT2046 controller over SPI.

| Product | Size | Resolution | Driver | Touch IC | Price | Link |
|---------|------|------------|--------|----------|-------|------|
| ILI9341 + XPT2046 | 2.4" | 240x320 | ILI9341 | XPT2046 | **$3-5** | [AliExpress](https://www.aliexpress.com/w/wholesale-ili9341-xpt2046.html) |
| ILI9341 + XPT2046 | 2.8" | 240x320 | ILI9341 | XPT2046 | **$4-7** | [AliExpress](https://www.aliexpress.com/item/32617643223.html) |
| ILI9341 + XPT2046 | 3.2" | 240x320 | ILI9341 | XPT2046 | **$5-8** | [AliExpress](https://www.aliexpress.com/i/4000127496421.html) |
| ST7796 + XPT2046 | 3.5" | 320x480 | ST7796 | XPT2046 | **$6-10** | AliExpress |

**Interface:**
- Display: SPI (MOSI, CLK, CS, DC, RST)
- Touch: SPI (MOSI, CLK, T_CS, T_IRQ) - shares MOSI/CLK with display

**Pros:**
- **Cheapest full touchscreen option**
- Single SPI bus for both display and touch
- Well supported in ESP-IDF and Arduino
- Works with stylus or finger

**Cons:**
- Less responsive than capacitive
- Requires pressure (not hover-sensitive)
- Can wear out over time
- Not multi-touch

**XPT2046 Notes:**
- 12-bit ADC for touch position
- SPI interface (up to 2MHz)
- IRQ pin goes low when touched
- Shares SPI bus with display (different CS pins)

---

## Option 3: Capacitive Touch TFT Modules

More responsive than resistive, but pricier. Uses I2C touch controllers.

### 2.8" Displays

| Product | Resolution | Driver | Touch IC | Price | Link |
|---------|------------|--------|----------|-------|------|
| Elecrow 2.8" IPS Capacitive | 240x320 | ILI9341 | FT6236 | ~$11 | [Elecrow](https://www.elecrow.com/2-8-ips-spi-lcd-capacitive-touch-module-ili9341-driver-240-320-resolution.html) |
| Generic ILI9341 + Cap Touch | 240x320 | ILI9341 | FT6236 | $8-12 | AliExpress |

### 3.5" Displays

| Product | Resolution | Driver | Touch IC | Price | Link |
|---------|------------|--------|----------|-------|------|
| Elecrow 3.5" IPS Capacitive | 320x480 | ST7796 | FT6336 | ~$17 | [Elecrow](https://www.elecrow.com/3-5-ips-spi-lcd-capacitive-touch-module-st7796-driver-320-480-resolution.html) |
| Waveshare 3.5" Capacitive | 320x480 | ST7796S | FT6336U | ~$15 | [Waveshare](https://www.waveshare.com/3.5inch-capacitive-touch-lcd.htm) |

**Interface (typical):**
- Display: SPI (4-5 pins: MOSI, CLK, CS, DC, RST)
- Touch: I2C (2 pins: SDA, SCL + INT optional)

**Pros:**
- Keep existing XIAO ESP32-C3
- IPS displays have good viewing angles

**Cons:**
- More wiring required
- Uses more GPIO pins

---

## Option 4: Standalone Capacitive Touch Panels (Overlay)

Separate touch panel to pair with any LCD. Cheapest capacitive option.

| Product | Size | Controller | Interface | Price | Link |
|---------|------|------------|-----------|-------|------|
| AliExpress FT6236 Panel | 3.5" | FT6236 | I2C | ~$5-6 | [AliExpress](https://www.aliexpress.com/item/32814164683.html) |
| BuyDisplay 2.3" Panel | 2.3" | FT6236 | I2C | $5-10 | [BuyDisplay](https://www.buydisplay.com/2-3-inch-capacitive-touch-panel-screen-with-controller-ft6236) |
| BuyDisplay 3.2" Panel | 3.2" | FT6236 | I2C | $5-10 | [BuyDisplay](https://www.buydisplay.com/3-2-inch-capacitive-touch-panel-wiith-controller-ft6236-for-240x320-dots) |
| BuyDisplay 3.5" Panel | 3.5" | FT6236 | I2C | $5-10 | [BuyDisplay](https://www.buydisplay.com/3-5-inch-capacitive-touch-panel-wiith-controller-ft6236-for-320x480-dots) |

**Pros:**
- Cheapest capacitive touch option
- Pair with any matching LCD
- Simple I2C interface

**Cons:**
- Must match panel size to LCD exactly
- Alignment/mounting more complex
- Need separate LCD purchase

---

## Option 5: Capacitive Touch Buttons (No Screen Touch)

Use discrete touch buttons instead of touchscreen. Display is view-only.

| Product | Channels | Interface | Price | Link |
|---------|----------|-----------|-------|------|
| TTP223 Touch Sensor | 1 | GPIO | $0.20-0.30 ea | [Amazon](https://www.amazon.com/ALAMSCN-TTP223-Capacitive-Locking-Arduino/dp/B0BNHM7TQH) |
| TTP229 Touch Keypad | 16 | I2C/Serial | $2-4 | AliExpress |
| MPR121 Breakout | 12 | I2C | $3-6 | AliExpress |
| CAP1188 Breakout | 8 | I2C | $5-8 | Adafruit |

**Example Setup:**
- 3-4x TTP223 buttons: UP, DOWN, SELECT, POWER
- Cheap non-touch OLED or LCD for display
- Total: ~$5-8 for display + $1-2 for buttons

**Pros:**
- Cheapest overall solution (~$6-10 total)
- Dead simple to program
- Very reliable

**Cons:**
- No dynamic touch UI
- Fixed button positions
- Less "modern" feel

---

## Option 6: Non-Touch Displays (Reference)

If touch isn't required, these are the cheapest display options.

| Product | Size | Resolution | Interface | Price |
|---------|------|------------|-----------|-------|
| SSD1306 OLED | 0.96" | 128x64 | I2C | $2-4 |
| SSD1306 OLED | 1.3" | 128x64 | I2C/SPI | $3-5 |
| SH1106 OLED | 1.3" | 128x64 | I2C | $3-5 |
| SSD1309 OLED | 2.42" | 128x64 | I2C/SPI | $8-12 |
| ST7789 TFT | 1.3" | 240x240 | SPI | $3-5 |
| ST7789 TFT | 2.0" | 240x320 | SPI | $4-6 |
| ILI9341 TFT | 2.4" | 240x320 | SPI | $5-8 |

**Pros:**
- Extremely cheap
- Simple wiring
- Low power (especially OLED)

**Cons:**
- No touch capability
- Requires physical buttons for input

---

## Cost Comparison Summary

| Approach | Total Cost | Touch Type | Complexity |
|----------|-----------|------------|------------|
| **2.4" Resistive TFT (ILI9341+XPT2046)** | **$3-5** | Resistive | Low |
| **2.8" Resistive TFT (ILI9341+XPT2046)** | **$4-7** | Resistive | Low |
| TTP223 buttons + OLED | $4-7 | Buttons | Low |
| TTP223 buttons + 2.4" TFT | $6-10 | Buttons | Low |
| CYD Resistive (ESP32-2432S028R) | $10-15 | Resistive | Low |
| 2.8" Capacitive TFT | $11-17 | Capacitive | Medium |
| CYD Capacitive | $12-18 | Capacitive | Low |

---

## Recommended Options

### Cheapest Touchscreen: 2.8" Resistive TFT (~$5)
- ILI9341 display + XPT2046 touch controller
- Full touchscreen for the price of a basic display
- Good enough for simple UI (buttons, sliders)
- [AliExpress ILI9341+XPT2046](https://www.aliexpress.com/w/wholesale-ili9341-xpt2046.html)

### Budget Non-Touch: TTP223 + 1.3" OLED (~$6)
- 3x TTP223 touch buttons (UP/DOWN/SELECT)
- 1.3" SH1106 OLED display
- Simple, reliable, very cheap

### Best Touch Experience: 2.8" Capacitive TFT (~$11)
- Elecrow or generic ILI9341 + FT6236 module
- Smooth, responsive touch
- Good size for kitchen visibility

### Easiest Integration: CYD Resistive ($10-15)
- ESP32-2432S028R with resistive touch
- No wiring, huge community
- Replaces XIAO (trade-off)

---

## Technical Notes

### Common Touch Controllers

**Resistive (cheaper):**
- **XPT2046**: Most common resistive controller, SPI interface, 12-bit ADC

**Capacitive (more responsive):**
- **FT6236/FT6336**: Most common capacitive, I2C, single/dual touch
- **GT911**: Multi-touch (5-10 points), I2C
- **CST816S**: Low power, I2C, common on smartwatches

### ESP32-C3 GPIO Constraints
The XIAO ESP32-C3 has limited GPIOs (11 usable). Plan pin usage carefully.

| Function | Pins Required | Notes |
|----------|---------------|-------|
| SPI Display | 5 (MOSI, CLK, CS, DC, RST) | |
| XPT2046 Resistive Touch | 2 (T_CS, T_IRQ) | Shares MOSI/CLK with display |
| I2C Capacitive Touch | 2-3 (SDA, SCL, INT) | Separate bus from SPI |
| TTP223 Buttons | 1 per button | Simple GPIO input |

**Resistive touch advantage:** Shares SPI bus with display, only needs 2 extra pins (T_CS, T_IRQ).

**XIAO ESP32-C3 Available Pins:**
- D0-D10 (11 GPIOs total)
- D4/D5 are default I2C (SDA/SCL)
- D8/D9/D10 are default SPI (SCK/MISO/MOSI)

### Display Driver Libraries (ESP-IDF)
- **LVGL**: Full-featured GUI library, supports touch
- **TFT_eSPI**: Arduino-style, easy setup
- **esp_lcd**: ESP-IDF native, lower level

---

## Next Steps

1. Decide on touch approach (full screen vs buttons)
2. Order samples of top 2 choices
3. Prototype with simulator first
4. Update PCB design for chosen display

---

*Last updated: 2025-01-28*
