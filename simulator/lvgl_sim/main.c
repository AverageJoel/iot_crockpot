/**
 * @file main.c
 * @brief IoT Crockpot LVGL PC Simulator
 *
 * Runs gui.c (the real firmware GUI code) on the desktop using SDL2 as the
 * display backend and mouse as touch input. No ESP-IDF required.
 *
 * Build:
 *   mkdir build && cd build
 *   cmake .. -G Ninja -DCMAKE_C_COMPILER=/c/msys64/ucrt64/bin/gcc.exe
 *   PATH="/c/msys64/ucrt64/bin:$PATH" ninja crockpot_sim
 *   ./crockpot_sim
 *
 * Keys:
 *   O         crockpot OFF
 *   W         crockpot WARM
 *   L         crockpot LOW
 *   H         crockpot HIGH
 *   1         start Slow Cook schedule
 *   2         start Quick Warm schedule
 *   3         start All Day schedule
 *   Up/Down   temperature +/- 5°F
 *   E         toggle sensor error
 *   D         toggle WiFi connected
 *   T         show toast message
 *   R         show error toast
 *   X         export CSV data log
 *   S         print current status to stdout
 *   Q / Esc   quit
 */

/* Prevent SDL from renaming main→SDL_main; we provide a plain main(). */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* LVGL */
#include "lvgl.h"

/* GUI (real firmware code) */
#include "gui.h"
#include "crockpot.h"

/* Simulator controls */
#include "stubs/sim_controls.h"

/* New simulator modules */
#include "stubs/datalog.h"
#include "stubs/web_server.h"
#include "stubs/telegram.h"

/* ── Display constants ───────────────────────────────────────────────────── */

#define LCD_W    480
#define LCD_H    320
#define SCALE    2          /* Window = 960×640 for readability */
#define WIN_W    (LCD_W * SCALE)
#define WIN_H    (LCD_H * SCALE)

/* Draw buffer: 32 lines of RGB565 pixels (partial render mode, ~30 KB) */
#define DRAW_BUF_LINES  32
#define BYTES_PER_PIXEL  2   /* RGB565 */
static uint8_t s_draw_buf[LCD_W * DRAW_BUF_LINES * BYTES_PER_PIXEL];

/* ── SDL2 handles ────────────────────────────────────────────────────────── */

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;  /* LCD_W × LCD_H, RGB565 */

/* ── LVGL display flush callback ─────────────────────────────────────────── */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    SDL_Rect rect = {
        .x = area->x1,
        .y = area->y1,
        .w = w,
        .h = h,
    };

    /* Upload the dirty rectangle to the SDL texture */
    SDL_UpdateTexture(s_texture, &rect, px_map, w * BYTES_PER_PIXEL);

    /* Present only on the last flush of this frame */
    if (lv_display_flush_is_last(disp)) {
        SDL_RenderClear(s_renderer);
        /* Stretch LCD_W×LCD_H texture → WIN_W×WIN_H window (SCALE×) */
        SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
        SDL_RenderPresent(s_renderer);
    }

    lv_display_flush_ready(disp);
}

/* ── LVGL pointer (touch) input read callback ────────────────────────────── */

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int x, y;
    Uint32 buttons = SDL_GetMouseState(&x, &y);

    /* Map scaled window coordinates back to LVGL logical pixels */
    data->point.x = (int32_t)(x / SCALE);
    data->point.y = (int32_t)(y / SCALE);
    data->state   = (buttons & SDL_BUTTON_LMASK)
                    ? LV_INDEV_STATE_PRESSED
                    : LV_INDEV_STATE_RELEASED;
}

/* ── 1-second tick LVGL timer callback ──────────────────────────────────── */

static void sim_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Advance physics + schedule engine */
    crockpot_tick_s();

    /* Log a data sample */
    crockpot_status_t st = crockpot_get_status();
    datalog_tick(&st);
}

/* ── Telegram poll timer (every 2 s) ─────────────────────────────────────── */

static void telegram_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    telegram_poll();
}

/* ── Keyboard handler ────────────────────────────────────────────────────── */

static void handle_keydown(SDL_Keycode sym)
{
    switch (sym) {
        /* Heat state controls */
        case SDLK_o:
            crockpot_schedule_stop();
            crockpot_set_state(CROCKPOT_OFF);
            break;
        case SDLK_w:
            crockpot_set_state(CROCKPOT_WARM);
            break;
        case SDLK_l:
            crockpot_set_state(CROCKPOT_LOW);
            break;
        case SDLK_h:
            crockpot_set_state(CROCKPOT_HIGH);
            break;

        /* Schedule shortcuts */
        case SDLK_1:
            crockpot_schedule_start(&CROCKPOT_SCHED_SLOW_COOK);
            gui_show_message("Schedule: Slow Cook", 3000);
            break;
        case SDLK_2:
            crockpot_schedule_start(&CROCKPOT_SCHED_QUICK_WARM);
            gui_show_message("Schedule: Quick Warm", 3000);
            break;
        case SDLK_3:
            crockpot_schedule_start(&CROCKPOT_SCHED_ALL_DAY);
            gui_show_message("Schedule: All Day", 3000);
            break;

        /* Temperature adjustment */
        case SDLK_UP:
            sim_crockpot_set_temperature(sim_crockpot_get_temperature() + 5.0f);
            break;
        case SDLK_DOWN:
            sim_crockpot_set_temperature(sim_crockpot_get_temperature() - 5.0f);
            break;

        /* Fault injection */
        case SDLK_e:
            sim_crockpot_toggle_sensor_error();
            break;
        case SDLK_d:
            sim_wifi_toggle_connected();
            break;

        /* Toast / error overlay testing */
        case SDLK_t:
            gui_show_message("Test message from keyboard", 3000);
            break;
        case SDLK_r:
            gui_show_error("SAFETY SHUTOFF: Temperature 305.2°F exceeded limit.");
            break;

        /* Data logging export */
        case SDLK_x: {
            char fname[64];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(fname, sizeof(fname),
                     "crockpot_log_%Y%m%d_%H%M%S.csv", tm_info);
            if (datalog_export_csv(fname)) {
                char msg[80];
                snprintf(msg, sizeof(msg), "Log exported: %s", fname);
                gui_show_message(msg, 4000);
            } else {
                gui_show_message("No data to export yet", 3000);
            }
            break;
        }

        /* Debug: print status to stdout */
        case SDLK_s: {
            crockpot_status_t st = crockpot_get_status();
            printf("[status] state=%s  temp=%.1f°F  relay=M%d/A%d  "
                   "sched=%s/%s  uptime=%lus\n",
                   crockpot_state_to_string(st.state),
                   st.temperature_f,
                   st.relay_main, st.relay_aux,
                   st.schedule_active ? "active" : "none",
                   st.schedule_active ? st.schedule_name : "-",
                   (unsigned long)st.uptime_seconds);
            break;
        }

        default:
            break;
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* ── SDL2 ────────────────────────────────────────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    static const char *WINDOW_TITLE =
        "IoT Crockpot Simulator  "
        "[O=off  W=warm  L=low  H=high  1/2/3=schedule  "
        "\xe2\x86\x91\xe2\x86\x93=temp  E=sensor-err  D=wifi  "
        "X=export  S=status  T=toast  R=error  Q=quit]";

    s_window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(
        s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    /* Texture stores the logical LCD pixels; renderer scales them up */
    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        LCD_W, LCD_H
    );
    if (!s_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    /* Hand the window to the display_driver stub for title updates */
    sim_display_driver_set_window(s_window);

    /* ── Simulator modules ───────────────────────────────────────────────── */
    datalog_init();
    web_server_init();
    telegram_init();

    /* ── LVGL ────────────────────────────────────────────────────────────── */
    lv_init();

    /* Use SDL_GetTicks as the millisecond tick source */
    lv_tick_set_cb((lv_tick_get_cb_t)SDL_GetTicks);

    /* Create display */
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Create pointer input device (mouse → touch) */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, mouse_read_cb);

    /* ── GUI ─────────────────────────────────────────────────────────────── */
    if (!gui_init()) {
        fprintf(stderr, "gui_init() failed\n");
        return 1;
    }
    gui_start();

    /* 1-second timer: physics + schedule + datalog */
    lv_timer_create(sim_tick_cb, 1000, NULL);

    /* 2-second timer: Telegram poll */
    lv_timer_create(telegram_tick_cb, 2000, NULL);

    printf("IoT Crockpot Simulator running at %dx%d (LVGL %dx%d × %d)\n",
           WIN_W, WIN_H, LCD_W, LCD_H, SCALE);
    printf("Keys: O=off  W=warm  L=low  H=high  1/2/3=schedule  "
           "Up/Down=temp  E=sensor-err  D=wifi  X=export  S=status  "
           "T=toast  R=error  Q=quit\n");
    printf("Web API: http://localhost:8080/\n");

    /* ── Event loop ──────────────────────────────────────────────────────── */
    bool running = true;
    while (running) {
        /* Process SDL events: quit + keyboard (mouse state read in indev cb) */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_q ||
                        event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    } else {
                        handle_keydown(event.key.keysym.sym);
                    }
                    break;
            }
        }

        /* Non-blocking web server poll */
        web_server_poll();

        /* Drive LVGL timers (renders dirty regions, fires update_timer_cb) */
        uint32_t time_till_next = lv_timer_handler();

        /* Sleep at most 5ms to stay responsive; respect LVGL's requested delay */
        uint32_t sleep_ms = (time_till_next < 5) ? time_till_next : 5;
        if (sleep_ms > 0) SDL_Delay(sleep_ms);
    }

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    web_server_shutdown();
    telegram_shutdown();

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

    return 0;
}
