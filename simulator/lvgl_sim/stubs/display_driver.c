/**
 * @file display_driver.c
 * @brief display_driver stub implementation for PC simulator.
 *
 * Backlight state is shown in the SDL2 window title so it's visible
 * without cluttering stderr.
 */

#include "display_driver.h"
#include <SDL2/SDL.h>
#include <stdio.h>

/* Set from main.c after window creation */
static SDL_Window *s_window = NULL;

void sim_display_driver_set_window(SDL_Window *win)
{
    s_window = win;
}

void display_driver_set_backlight(bool on)
{
    fprintf(stderr, "[sim] backlight: %s\n", on ? "ON (full)" : "OFF (dimmed)");
    if (s_window) {
        /* Append backlight state to window title as a visual cue */
        const char *suffix = on ? "" : "  [DIMMED]";
        char title[256];
        snprintf(title, sizeof(title),
                 "IoT Crockpot Simulator  "
                 "[O=off W=warm L=low H=high  \xe2\x86\x91\xe2\x86\x93=temp  E=sensor-err  D=wifi  Q=quit]"
                 "%s", suffix);
        SDL_SetWindowTitle(s_window, title);
    }
}
