# LVGL PC Simulator

Runs the actual `gui.c` firmware code on the desktop in a 960×640 SDL2 window
(2× scaled from the physical 480×320 display). Mouse acts as touch input.

## What This Is

This is **not** a re-implementation of the UI. It compiles and links the real
`firmware/main/gui.c` directly, with thin stubs replacing ESP-IDF headers
(`esp_log.h`, `esp_timer.h`, `esp_lvgl_port.h`) and mock implementations of
`crockpot.c`, `wifi.c`, and `display_driver.c`.

The result is pixel-accurate LVGL rendering with full touch interaction —
no flash cycle required to iterate on the UI.

## Prerequisites

- CMake 3.20+
- A C compiler:
  - **Windows**: MSYS2/MinGW (`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake`)
  - **Linux**: `apt install gcc cmake`
  - **macOS**: Xcode CLT + `brew install cmake`
- SDL2 (optional — fetched automatically if not found):
  - Windows/MSYS2: `pacman -S mingw-w64-ucrt-x86_64-SDL2`
  - Linux: `apt install libsdl2-dev`
  - macOS: `brew install sdl2`
- Internet connection on first build (to fetch LVGL v9.2.2 from GitHub)

## Build

```bash
cd simulator/lvgl_sim
mkdir build && cd build
cmake .. -G "MinGW Makefiles"   # Windows/MSYS2
# or: cmake .. -G "Ninja"
# or: cmake ..                  # Linux/macOS (Makefile)
cmake --build . --parallel
```

First build downloads LVGL (~10 MB) and optionally SDL2. Subsequent builds
are incremental and fast.

## Run

```bash
./crockpot_sim          # Linux/macOS
crockpot_sim.exe        # Windows
```

A 960×640 window opens showing the main crockpot screen.

## Keyboard Controls

| Key | Action |
|-----|--------|
| `O` | Crockpot → OFF |
| `W` | Crockpot → WARM |
| `L` | Crockpot → LOW |
| `H` | Crockpot → HIGH |
| `↑` / `↓` | Temperature +/− 5°F |
| `E` | Toggle sensor error (shows SENSOR ERR in UI) |
| `D` | Toggle WiFi connected (changes WiFi icon color) |
| `T` | Show a test toast message (3 s auto-dismiss) |
| `R` | Show a test error overlay (persistent, tap to dismiss) |
| `Q` / `Esc` | Quit |

Mouse clicks drive touch input — all buttons and screen navigation work.

## Architecture

```
simulator/lvgl_sim/
├── CMakeLists.txt          Build: LVGL (FetchContent) + SDL2 + gui.c + stubs
├── lv_conf.h               LVGL config: RGB565, Montserrat fonts 14/16/20/28
├── main.c                  SDL2 init, LVGL init, event loop, keyboard handler
└── stubs/
    ├── crockpot.c          Mock state machine with drifting temperature
    ├── display_driver.c/.h Backlight stub (logs to stderr / window title)
    ├── esp_log.h           ESP_LOGI/LOGE/LOGW → fprintf(stderr)
    ├── esp_lvgl_port.h     lvgl_port_lock/unlock → no-ops (single-threaded)
    ├── esp_timer.c/.h      esp_timer_get_time() → SDL_GetTicks() * 1000
    ├── freertos/FreeRTOS.h Empty stub (gui.c includes it but doesn't use it)
    ├── sim_controls.h      Simulator control function declarations
    ├── wifi.c/.h           Mock WiFi with toggleable connected state
    └── crockpot.c          (also implements sim_crockpot_* helpers)
```

The real `firmware/main/gui.c` and `firmware/main/gui.h` are compiled directly
(referenced by path in CMakeLists.txt — no copying needed).

## Troubleshooting

**Colors look wrong (inverted or swapped)**
Try adding `#define LV_COLOR_16_SWAP 1` to `lv_conf.h` and rebuilding.

**Window is blank after launch**
LVGL may not have rendered the first frame yet. Try clicking in the window to
trigger an indev read, which forces a redraw.

**Build error: `lv_conf.h` not found**
Ensure you are building from `simulator/lvgl_sim/build/` (not from a parent
directory). The `LV_CONF_PATH` in CMakeLists.txt is an absolute path derived
from `CMAKE_CURRENT_SOURCE_DIR`.

**SDL2 download fails (corporate proxy / no internet)**
Install SDL2 via your system package manager, then re-run cmake — `find_package`
will pick it up and skip the FetchContent download.
