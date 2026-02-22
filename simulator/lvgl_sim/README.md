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

**Windows — launch from the MSYS2 UCRT64 shell:**
```bash
cd /c/Users/Joel/Documents/Hardware_Projects/iot_crockpot/simulator/lvgl_sim/build
./crockpot_sim.exe
```

**Linux / macOS:**
```bash
cd simulator/lvgl_sim/build
./crockpot_sim
```

A 960×640 window opens showing the main crockpot screen. Expected terminal output:
```
[web_server] listening on http://localhost:8080
[telegram] SIM_TELEGRAM_TOKEN not set — Telegram disabled
IoT Crockpot Simulator running at 960x640 (LVGL 480x320 × 2)
```

> **Why the MSYS2 shell on Windows?** The executable is built with MSYS2's GCC
> and links against MSYS2 runtime DLLs (`libgcc_s_seh-1.dll`,
> `libwinpthread-1.dll`, etc.) that live in `C:\msys64\ucrt64\bin\`. Running
> from the MSYS2 UCRT64 shell puts that directory on `PATH` automatically so
> the DLLs are found. Double-clicking the `.exe` from Explorer will fail with a
> missing DLL error unless those DLLs are copied next to the executable or
> added to the system `PATH`.

Once running:
- The GUI window responds to mouse clicks (touch input) and keyboard shortcuts
- The web control page is at **http://localhost:8080/**
- The JSON API is at **http://localhost:8080/api/status**

## How It Works on Windows

The simulator is a **native Windows `.exe`** — not emulation, not WSL. Each
component is a cross-platform C library compiled by GCC for x86-64 Windows:

| Component | Role | Windows implementation |
|-----------|------|------------------------|
| **SDL2** | Window creation + mouse/keyboard input | Calls `CreateWindow` / Win32 APIs |
| **LVGL** | UI rendering | Pure C — writes pixels into a buffer, no OS calls |
| **mongoose** | HTTP server | Uses Winsock2 (`ws2_32.dll`) instead of BSD sockets |
| **GCC (MSYS2)** | Compiler | Produces native PE executables |
| **`gui.c`** | UI logic | Plain C, calls only LVGL and `crockpot_*` — compiles anywhere |

At runtime the data flow is:

```
LVGL renders widgets → pixel buffer (RGB565)
    ↓
flush_cb() uploads dirty rectangles → SDL2 texture
    ↓
SDL2 scales 480×320 → 960×640 and presents to the window
    ↓  (every ~5 ms)
Mouse position → LVGL touch input
Keyboard events → crockpot state changes
mongoose polls for HTTP requests on :8080
1-second timer → crockpot_tick_s() (physics + schedule advance)
```

`gui.c` is completely unaware it is running on a PC. It calls
`crockpot_get_status()` and LVGL widget APIs exactly as it would on the
ESP32 — the stub implementations in `stubs/` provide the hardware behaviour.

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

Telegram is **disabled by default** because it requires HTTPS and TLS is turned
off in the build (`MG_TLS=MG_TLS_NONE`). To enable it:

**1. Install OpenSSL (MSYS2):**
```bash
pacman -S mingw-w64-ucrt-x86_64-openssl
```

**2. Edit `CMakeLists.txt` — change the TLS definition and add OpenSSL:**
```cmake
# Change:
MG_TLS=MG_TLS_NONE
# To:
MG_TLS=MG_TLS_OPENSSL

# Add ssl and crypto to target_link_libraries:
target_link_libraries(crockpot_sim PRIVATE lvgl ${SDL2_TARGET} m ws2_32 ssl crypto)
```

**3. Rebuild**, then create a bot via `@BotFather` on Telegram if you don't
have one (`/newbot` → copy the token).

**4. Launch with the token:**
```bash
SIM_TELEGRAM_TOKEN="123456789:ABCdef..." ./crockpot_sim.exe
```

You should see `[telegram] enabled (token: 12345678...)` in the terminal.
Open your bot in Telegram and send `/start` or `/status` to test it.

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
