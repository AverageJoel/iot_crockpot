/**
 * @file web_server.h
 * @brief Minimal HTTP API server for the PC simulator.
 *
 * Uses mongoose v7 for a single-threaded, non-blocking HTTP server.
 * Default port 8080; override with SIM_HTTP_PORT environment variable.
 *
 * Endpoints:
 *   GET  /api/status                → JSON crockpot status
 *   POST /api/state/{off|warm|low|high}  → set heat state
 *   GET  /api/schedules             → JSON list of preset schedules
 *   POST /api/schedule/start/{name} → start named preset
 *   POST /api/schedule/stop         → stop active schedule
 *   GET  /api/history               → JSON last 60 temp readings
 *   GET  /                          → minimal HTML control page
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/** Bind to port and start listening. Call once at startup. */
void web_server_init(void);

/**
 * @brief Non-blocking poll — call every iteration of the event loop.
 *
 * Internally calls mg_mgr_poll(&mgr, 0) so it returns immediately.
 */
void web_server_poll(void);

/** Shut down the server and free resources. */
void web_server_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
