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

## One-time PowerShell setup (first time only)

Windows disables script execution by default. Run this once to allow scripts for your user account:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

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
(look for **"USB Serial Device"** — the XIAO uses native USB Serial/JTAG, not a CH340 chip).

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

See `docs/wiring_xiao_display.md` for the full diagram and checklist.
Quick reference:

| Display pin | Signal | XIAO pin | GPIO |
|:-----------:|--------|----------|------|
| 1 | VCC | **5V** | — |
| 2 | GND | GND | — |
| 3 | LCD_CS | D3 | 5 |
| 4 | LCD_RST | D1 | 3 |
| 5 | LCD_RS (DC) | D2 | 4 |
| 6 | SDI (MOSI) | D10 | 10 |
| 7 | SCK | D8 | 8 |
| 8 | LED (BL) | D0 | 2 |
| 9 | SDO (MISO) | D9 | 9 |
| 10 | CTP_SCL | D5 | 7 |
| 11 | CTP_RST | **3V3** | — |
| 12 | CTP_SDA | D4 | 6 |
| 13 | CTP_INT | D7 | GPIO20 |
| 14 | SD_CS | — | NC |

> **VCC must be 5V**, not 3.3V. The module has an onboard level converter
> so 3.3V signals from the XIAO work fine, but the module itself needs 5V input.

The XIAO ESP32-C3 uses **native USB Serial/JTAG** — serial monitor runs through the
USB-C cable directly (no CH340 chip). GPIO20 and GPIO21 are free I/O pins.
GPIO20 (D7) is used for the CTP_INT touch interrupt wire.

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

**Guru Meditation Error: Load access fault at boot (in `uart_tx_flush` or similar)**
→ Caused by a stale generated `sdkconfig` conflicting with a changed console config.
  For example, if `sdkconfig.defaults` was changed from `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`
  to `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, but the old `sdkconfig` was not regenerated,
  the binary tries to flush a UART peripheral that isn't configured.
  **Fix:** delete the generated `sdkconfig` and rebuild:
  ```powershell
  Remove-Item sdkconfig
  idf.py build
  idf.py -p COM<N> flash monitor
  ```
  This also applies any time `sdkconfig.defaults` is changed — always delete `sdkconfig`
  and rebuild from scratch to avoid stale option conflicts.
