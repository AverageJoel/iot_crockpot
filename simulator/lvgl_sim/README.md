# LVGL PC Simulator

Runs the actual `gui.c` firmware code on the desktop in a 960×640 SDL2 window
(2× scaled from the physical 480×320 display). Mouse acts as touch input.

## What This Is

This is **not** a re-implementation of the UI. It compiles and links the real
`firmware/main/gui.c` directly, with thin stubs replacing ESP-IDF headers and
mock implementations of hardware-dependent modules.

The result is pixel-accurate LVGL rendering with full touch interaction, a
live HTTP control API, simulated thermal physics, and Telegram bot support —
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
- Internet connection on first build (fetches LVGL v9.2.2 and mongoose v7.14)

## Build

**Windows (MSYS2 UCRT64 shell):**
```bash
cd simulator/lvgl_sim
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_C_COMPILER=/c/msys64/ucrt64/bin/gcc.exe
cmake --build .
```

**Linux / macOS:**
```bash
cd simulator/lvgl_sim
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

First build downloads LVGL v9.2.2 and mongoose v7.14 via git, and optionally
SDL2. Subsequent builds are incremental and fast.

> **Windows note**: Run from the MSYS2 UCRT64 shell (not Git Bash or CMD) so
> that cmake, ninja, and gcc are on `PATH`. Install prerequisites with:
> `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2`

## Run

```bash
./crockpot_sim          # Linux/macOS
crockpot_sim.exe        # Windows
```

A 960×640 window opens showing the main crockpot screen.
The web control page is available at `http://localhost:8080/`.

## Keyboard Controls

| Key | Action |
|-----|--------|
| `O` | Crockpot → OFF |
| `W` | Crockpot → WARM |
| `L` | Crockpot → LOW |
| `H` | Crockpot → HIGH |
| `1` | Start Slow Cook schedule (HIGH 1h → LOW 6h → WARM) |
| `2` | Start Quick Warm schedule (HIGH 30m → WARM) |
| `3` | Start All Day schedule (LOW 8h → WARM) |
| `↑` / `↓` | Temperature +/− 5°F |
| `E` | Toggle sensor error |
| `D` | Toggle WiFi connected |
| `X` | Export temperature log to CSV |
| `S` | Print current status to terminal |
| `T` | Show a test toast message |
| `R` | Show a test error overlay |
| `Q` / `Esc` | Quit |

Mouse clicks drive touch input — all buttons and screen navigation work.

## Web API

The simulator exposes an HTTP API on port 8080 (override with `SIM_HTTP_PORT`
environment variable). Open `http://localhost:8080/` for a browser control page.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET`  | `/api/status` | Full JSON state snapshot |
| `POST` | `/api/state/{off\|warm\|low\|high}` | Set heat level |
| `GET`  | `/api/schedules` | List preset schedules |
| `POST` | `/api/schedule/start/{name}` | Start a named schedule (URL-encode spaces: `Slow%20Cook`) |
| `POST` | `/api/schedule/stop` | Stop active schedule |
| `GET`  | `/api/history` | Last 60 logged data points |

**Note:** POST requests require a `Content-Length` header. When using curl, add
`--data ''` to include one automatically. The browser control page handles this
correctly.

Example:
```bash
curl http://localhost:8080/api/status
curl -X POST http://localhost:8080/api/state/high --data ''
curl -X POST "http://localhost:8080/api/schedule/start/Slow%20Cook" --data ''
curl -X POST http://localhost:8080/api/schedule/stop --data ''
```

Status JSON example:
```json
{
  "state": "HIGH",
  "temperature_f": 245.3,
  "relay_main": true,
  "relay_aux": true,
  "sensor_error": false,
  "wifi_connected": true,
  "uptime_seconds": 3600,
  "uptime": "01:00:00",
  "schedule_active": true,
  "schedule_name": "Slow Cook",
  "schedule_step": 0,
  "schedule_total_steps": 3,
  "schedule_step_remaining_s": 3540,
  "schedule_step_progress": 0.02
}
```

## Telegram Bot (optional)

Set the `SIM_TELEGRAM_TOKEN` environment variable to your bot token before
launching. TLS is required for Telegram HTTPS; it is disabled by default
(`MG_TLS=MG_TLS_NONE`), so Telegram is silently disabled unless you enable TLS
in `CMakeLists.txt` and link OpenSSL.

When enabled, supported commands:

| Command | Action |
|---------|--------|
| `/status` | Current state, temp, relay, uptime, schedule |
| `/off` `/warm` `/low` `/high` | Set heat level |
| `/schedule` | List available schedules |
| `/slow` `/quick` `/allday` | Start a preset schedule |
| `/stopschedule` | Stop active schedule |
| `/log` | Export CSV and report filename |
| `/help` | Command list |

## Data Logging

Temperature, state, and relay status are logged to an in-memory ring buffer
(1440 samples = 24 hours at 60-second intervals). Press `X` or send `/log` via
Telegram to export a timestamped CSV file to the current directory.

## Architecture

```
simulator/lvgl_sim/
├── CMakeLists.txt          Build: LVGL + SDL2 + mongoose (FetchContent) + gui.c + stubs
├── lv_conf.h               LVGL config: RGB565, chart widget, Montserrat fonts
├── main.c                  SDL2 init, LVGL init, event loop, keyboard handler,
│                           1s physics timer, 2s Telegram poll timer
└── stubs/
    ├── crockpot.c          Thermal physics simulation + schedule engine + relay logic
    ├── datalog.c/.h        1440-entry ring buffer, 60s sample interval, CSV export
    ├── web_server.c/.h     mongoose HTTP server on :8080, REST API handlers
    ├── telegram.c/.h       Telegram bot: getUpdates polling + sendMessage
    ├── display_driver.c/.h Backlight stub (logs to stderr / window title)
    ├── esp_log.h           ESP_LOGI/LOGE/LOGW → fprintf(stderr)
    ├── esp_lvgl_port.h     lvgl_port_lock/unlock → no-ops (single-threaded)
    ├── esp_timer.c/.h      esp_timer_get_time() → SDL_GetTicks() * 1000
    ├── freertos/FreeRTOS.h Empty stub (gui.c includes it but doesn't use it)
    ├── sim_controls.h      sim_crockpot_* and sim_wifi_* control declarations
    └── wifi.c/.h           Mock WiFi with toggleable connected state
```

The real `firmware/main/gui.c` and `firmware/main/gui.h` are compiled directly
(referenced by path in CMakeLists.txt — no copying needed).

### Thermal Physics (simulator only)

The `crockpot.c` stub simulates realistic temperature behaviour:

- **Heating**: linear ramp toward target, capped at +2°F/tick
- **Cooling**: exponential decay toward ambient (70°F when OFF, target temp otherwise)
- **Noise**: ±0.3°F random jitter per tick

Target temperatures: OFF→70°F, WARM→145°F, LOW→185°F, HIGH→265°F

**Relay mapping:**

| State | relay_main | relay_aux |
|-------|-----------|-----------|
| OFF   | OFF | OFF |
| WARM  | OFF | ON  |
| LOW   | ON  | OFF |
| HIGH  | ON  | ON  |

**Safety shutoff**: automatically engages at >300°F, cancels any active schedule.

### Schedule Engine

Schedules are defined as ordered steps, each with a heat state and duration:

```
Slow Cook:   HIGH (1h) → LOW (6h) → WARM (∞)
Quick Warm:  HIGH (30m) → WARM (∞)
All Day:     LOW (8h) → WARM (∞)
```

`crockpot_tick_s()` is called every second from an LVGL timer. It counts down
the current step, advances to the next step when elapsed, and calls
`crockpot_set_state()` automatically.

## Troubleshooting

**Colors look wrong (inverted or swapped)**
Try adding `#define LV_COLOR_16_SWAP 1` to `lv_conf.h` and rebuilding.

**Window is blank after launch**
LVGL may not have rendered the first frame yet. Try clicking in the window to
trigger an indev read, which forces a redraw.

**Build error: `lv_conf.h` not found**
Ensure you are building from inside `simulator/lvgl_sim/build/`. The simulator
root is added to the include path by CMakeLists.txt.

**SDL2 download fails (corporate proxy / no internet)**
Install SDL2 via your system package manager, then re-run cmake — `find_package`
will pick it up and skip the FetchContent download.

**Port 8080 already in use**
Either a previous simulator instance is still running, or another service owns
the port. Kill the old process or set `SIM_HTTP_PORT=8081` before launching.
