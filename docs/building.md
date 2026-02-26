# Building the Firmware

## Prerequisites

- [ESP-IDF v5.2](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/get-started/index.html) installed via the Windows installer
  - Installs to `C:\Users\<you>\esp\esp-idf`
  - Tools install to `C:\Users\<you>\.espressif`

---

## Important: Use PowerShell, NOT Git Bash

**idf.py does not work from Git Bash or MSYS2.** It detects the `MSYSTEM=MINGW64`
environment variable and aborts. Always use regular PowerShell or the ESP-IDF
shortcut from the Start Menu.

| Shell | Works? |
|-------|--------|
| PowerShell (Start Menu) | Yes |
| ESP-IDF PowerShell (Start Menu shortcut) | Yes — pre-activated |
| ESP-IDF CMD (Start Menu shortcut) | Yes — pre-activated |
| Git Bash / MSYS2 | **No** |

---

## First-time setup per terminal session

Open **PowerShell** and run the export script to add ESP-IDF tools to your PATH:

```powershell
. C:\Users\Joel\esp\esp-idf\export.ps1
```

You'll see a list of tools added to PATH ending with "Done! You can now compile ESP-IDF projects."
You need to do this once per PowerShell window. The Start Menu shortcuts do it automatically.

---

## Building

```powershell
cd C:\Users\Joel\Documents\Hardware_Projects\iot_crockpot\firmware

# First time on a new branch, or after switching targets:
idf.py set-target esp32c3      # or esp32s3 for custom PCB

# Every build:
idf.py build
```

Check binary size after build — the last line shows how much partition space is free:
```
iot_crockpot.bin binary size 0x164a00 bytes. Smallest app partition is 0x200000 bytes. 0x9b600 bytes (30%) free.
```

---

## Flashing

Plug in the XIAO ESP32-C3 via USB. Find its COM port in **Device Manager → Ports**
(look for "USB-SERIAL CH340").

```powershell
idf.py -p COM3 flash           # replace COM3 with your port
idf.py -p COM3 flash monitor   # flash then open serial monitor
```

Exit the monitor with `Ctrl+]`.

---

## Branches and targets

| Branch | Target | Notes |
|--------|--------|-------|
| `main` | `esp32s3` | Custom PCB (not yet fabricated) |
| `feature/xiao-esp32c3-display-test` | `esp32c3` | XIAO dev board, mocked temp/relay |

After switching branches, always run `idf.py set-target <target>` to regenerate
sdkconfig for the new chip. This deletes the old build directory automatically.

---

## XIAO ESP32-C3 wiring (display test branch)

| Display pin | Signal | XIAO pin | GPIO |
|-------------|--------|----------|------|
| LCD_SCL | SPI SCK | D8 | 8 |
| LCD_SDA | SPI MOSI | D10 | 10 |
| LCD_MISO | SPI MISO | D9 | 9 |
| LCD_CS | LCD CS | D3 | 5 |
| LCD_DC | LCD DC | D2 | 4 |
| LCD_RST | LCD RST | D1 | 3 |
| LCD_BL | Backlight | D0 | 2 |
| TP_SDA | Touch SDA | D4 | 6 |
| TP_SCL | Touch SCL | D5 | 7 |
| TP_RST | — | 3.3V | — |
| TP_INT | — | not connected | — |
| VCC | Power | 3V3 | — |
| GND | Ground | GND | — |

Serial monitor output comes through the CH340 USB bridge on D6/D7 (GPIO20/21).
Do **not** use those pins for anything else.

---

## Troubleshooting

**`idf.py` not recognized**
→ You're in Git Bash or forgot to run `export.ps1`. Open PowerShell and run the export script.

**`MSys/Mingw is no longer supported`**
→ Same cause. Switch to PowerShell.

**Partition too small for binary**
→ Edit `partitions.csv` and increase the factory partition size.
→ Make sure `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` is in `sdkconfig.defaults`
  (the string form `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` does **not** override the default on C3).

**Flash size showing 2MB despite 4MB chip**
→ The C3 default is 2MB. `sdkconfig.defaults` must have `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`.
  After changing defaults, run `idf.py set-target esp32c3` to regenerate sdkconfig.

**Touch or relay GPIO warnings about negative shift**
→ Use `#if CONFIG_... >= 0` (preprocessor) not `if (CONFIG_... >= 0)` (runtime) when
  constructing `pin_bit_mask`. The compiler evaluates `(1ULL << -1)` at compile time
  even in a dead branch.
