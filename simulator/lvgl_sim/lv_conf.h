/**
 * @file lv_conf.h
 * @brief LVGL v9 configuration for the IoT Crockpot PC simulator.
 *
 * Mirrors the firmware's sdkconfig.defaults font selection and
 * uses the same RGB565 color depth as the ST7796 display.
 */

#if 1  /* Set to 1 to enable — required by LVGL's include guard pattern */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Color depth ─────────────────────────────────────────────────────────── */
/* 16-bit RGB565 matches the physical ST7796 display */
#define LV_COLOR_DEPTH 16

/* ── Memory ──────────────────────────────────────────────────────────────── */
/* 1 = LV_STDLIB_CLIB: use system malloc/free/realloc */
#define LV_USE_STDLIB_MALLOC    1
#define LV_USE_STDLIB_STRING    1
#define LV_USE_STDLIB_SPRINTF   1

/* LVGL heap size — used only when LV_USE_STDLIB_MALLOC = 0 (builtin) */
#define LV_MEM_SIZE (256U * 1024U)

/* ── Tick ────────────────────────────────────────────────────────────────── */
/* We call lv_tick_set_cb(SDL_GetTicks) at runtime — no compile-time hook */
#define LV_TICK_CUSTOM 0

/* ── Logging ─────────────────────────────────────────────────────────────── */
#define LV_USE_LOG 0

/* ── Assert ──────────────────────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* ── Fonts ───────────────────────────────────────────────────────────────── */
/* Must match firmware sdkconfig.defaults font enables */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0

/* Unscii (built-in fallback) */
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

/* Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Enable font compression and sub-pixel rendering */
#define LV_USE_FONT_COMPRESSED      0
#define LV_USE_FONT_SUBPX           0

/* ── Widgets ─────────────────────────────────────────────────────────────── */
/* All widgets default to enabled (1) in LVGL v9 — only list overrides */
#define LV_USE_ARC        1
#define LV_USE_BUTTON     1
#define LV_USE_LABEL      1
#define LV_USE_IMAGE      1
#define LV_USE_LINE       1
#define LV_USE_TABLE      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_CHART      1
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_SPAN       0
#define LV_USE_CALENDAR   0
#define LV_USE_KEYBOARD   0
#define LV_USE_LIST       1
#define LV_USE_MENU       0

/* ── Animation ───────────────────────────────────────────────────────────── */
#define LV_USE_ANIM 1

/* ── Layouts ─────────────────────────────────────────────────────────────── */
#define LV_USE_FLEX  1
#define LV_USE_GRID  1

/* ── Theme ───────────────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/* ── Performance ─────────────────────────────────────────────────────────── */
#define LV_DRAW_BUF_ALIGN 4

/* ── OS / threading ──────────────────────────────────────────────────────── */
/* No RTOS — single-threaded event loop */
#define LV_USE_OS LV_OS_NONE

/* ── SDL2 driver (not used — we provide manual flush/indev callbacks) ──── */
#define LV_USE_SDL 0

/* ── Miscellaneous ───────────────────────────────────────────────────────── */
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1

#endif /* LV_CONF_H */
#endif /* if 1 */
