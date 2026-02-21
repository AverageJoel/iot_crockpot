/**
 * @file sim_controls.h
 * @brief Simulator control functions — not part of the firmware API.
 *
 * Included by main.c to drive keyboard-controlled state changes.
 * Implemented in crockpot.c and wifi.c stubs.
 */

#ifndef SIM_CONTROLS_H
#define SIM_CONTROLS_H

#include <stdbool.h>

/* Crockpot simulator controls */
void  sim_crockpot_set_temperature(float temp_f);
float sim_crockpot_get_temperature(void);
void  sim_crockpot_toggle_sensor_error(void);

/* WiFi simulator control */
void sim_wifi_toggle_connected(void);

/* Display driver — hand the window handle over after SDL init */
struct SDL_Window;
void sim_display_driver_set_window(struct SDL_Window *win);

#endif /* SIM_CONTROLS_H */
