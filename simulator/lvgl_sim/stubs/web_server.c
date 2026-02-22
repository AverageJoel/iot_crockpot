/**
 * @file web_server.c
 * @brief Mongoose-based HTTP API server for the PC simulator.
 */

#include "web_server.h"
#include "datalog.h"
#include "crockpot.h"

#include "mongoose.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ── Config ────────────────────────────────────────────────────────────────────

#define DEFAULT_PORT "8080"

static struct mg_mgr s_mgr;
static bool          s_initialized = false;

// ── JSON helpers ──────────────────────────────────────────────────────────────

static void send_json(struct mg_connection *c, int status,
                      const char *body)
{
    mg_http_reply(c, status,
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n",
                  "%s", body);
}

static void status_to_json(const crockpot_status_t *st,
                            char *buf, size_t len)
{
    uint32_t h = st->uptime_seconds / 3600;
    uint32_t m = (st->uptime_seconds % 3600) / 60;
    uint32_t s = st->uptime_seconds % 60;

    snprintf(buf, len,
             "{"
             "\"state\":\"%s\","
             "\"temperature_f\":%.1f,"
             "\"relay_main\":%s,"
             "\"relay_aux\":%s,"
             "\"sensor_error\":%s,"
             "\"wifi_connected\":%s,"
             "\"uptime_seconds\":%lu,"
             "\"uptime\":\"%.2lu:%.2lu:%.2lu\","
             "\"schedule_active\":%s,"
             "\"schedule_name\":\"%s\","
             "\"schedule_step\":%d,"
             "\"schedule_total_steps\":%d,"
             "\"schedule_step_remaining_s\":%lu,"
             "\"schedule_step_progress\":%.2f"
             "}",
             crockpot_state_to_string(st->state),
             st->temperature_f,
             st->relay_main ? "true" : "false",
             st->relay_aux  ? "true" : "false",
             st->sensor_error    ? "true" : "false",
             st->wifi_connected  ? "true" : "false",
             (unsigned long)st->uptime_seconds,
             (unsigned long)h, (unsigned long)m, (unsigned long)s,
             st->schedule_active ? "true" : "false",
             st->schedule_name,
             st->schedule_step,
             st->schedule_total_steps,
             (unsigned long)st->schedule_step_remaining_s,
             st->schedule_step_progress);
}

// -- URI helpers -------------------------------------------------------------

/**
 * Extract the last path component of an mg_str URI into a null-terminated
 * caller-supplied buffer. Returns false if URI has no component after last slash.
 *
 * IMPORTANT: hm->uri is NOT null-terminated; it points into the raw HTTP
 * request buffer and must be bounded by uri.len before any str* calls.
 */
static bool uri_last_component(struct mg_str uri, char *out, size_t out_len)
{
    const char *last_slash = NULL;
    for (size_t i = 0; i < uri.len; i++) {
        if (uri.buf[i] == '/') last_slash = uri.buf + i;
    }
    if (!last_slash || last_slash[1] == '\0') return false;

    const char *start = last_slash + 1;
    size_t comp_len = (size_t)(uri.buf + uri.len - start);
    if (comp_len >= out_len) comp_len = out_len - 1;
    memcpy(out, start, comp_len);
    out[comp_len] = '\0';
    return true;
}

/**
 * Decode percent-encoded characters in-place.
 * e.g. "Slow%20Cook" -> "Slow Cook". '+' also treated as space.
 */
static void url_decode_inplace(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], '\0' };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

// ── Route handlers ────────────────────────────────────────────────────────────

static void handle_get_status(struct mg_connection *c)
{
    crockpot_status_t st = crockpot_get_status();
    char buf[512];
    status_to_json(&st, buf, sizeof(buf));
    send_json(c, 200, buf);
}

static void handle_post_state(struct mg_connection *c,
                               struct mg_http_message *hm)
{
    // URI: /api/state/{state} -- extract last path component
    // e.g. /api/state/high -> "high"
    char state_str[32];
    if (!uri_last_component(hm->uri, state_str, sizeof(state_str))) {
        send_json(c, 400, "{\"error\":\"missing state\"}");
        return;
    }

    crockpot_state_t state;
    if (!crockpot_state_from_string(state_str, &state)) {
        send_json(c, 400, "{\"error\":\"unknown state\"}");
        return;
    }

    crockpot_schedule_stop();  // cancel any active schedule
    crockpot_set_state(state);
    send_json(c, 200, "{\"ok\":true}");
}

static void handle_get_schedules(struct mg_connection *c)
{
    // Return array of preset schedule names and step counts
    static const struct {
        const crockpot_schedule_t *sched;
    } presets[] = {
        { &CROCKPOT_SCHED_SLOW_COOK  },
        { &CROCKPOT_SCHED_QUICK_WARM },
        { &CROCKPOT_SCHED_ALL_DAY    },
    };

    char buf[512];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "[");
    for (int i = 0; i < 3; i++) {
        if (i > 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ",");
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                      "{\"name\":\"%s\",\"num_steps\":%d,\"repeat\":%s}",
                      presets[i].sched->name,
                      presets[i].sched->num_steps,
                      presets[i].sched->repeat ? "true" : "false");
    }
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "]");
    send_json(c, 200, buf);
}

static void handle_post_schedule_start(struct mg_connection *c,
                                        struct mg_http_message *hm)
{
    // URI: /api/schedule/start/{name}  (name may be percent-encoded)
    char name[64];
    if (!uri_last_component(hm->uri, name, sizeof(name))) {
        send_json(c, 400, "{\"error\":\"missing name\"}");
        return;
    }
    url_decode_inplace(name);

    const crockpot_schedule_t *presets[] = {
        &CROCKPOT_SCHED_SLOW_COOK,
        &CROCKPOT_SCHED_QUICK_WARM,
        &CROCKPOT_SCHED_ALL_DAY,
    };
    for (int i = 0; i < 3; i++) {
        if (strcasecmp(presets[i]->name, name) == 0) {
            crockpot_schedule_start(presets[i]);
            char resp[64];
            snprintf(resp, sizeof(resp),
                     "{\"ok\":true,\"schedule\":\"%s\"}", presets[i]->name);
            send_json(c, 200, resp);
            return;
        }
    }
    send_json(c, 404, "{\"error\":\"schedule not found\"}");
}

static void handle_post_schedule_stop(struct mg_connection *c)
{
    crockpot_schedule_stop();
    send_json(c, 200, "{\"ok\":true}");
}

static void handle_get_history(struct mg_connection *c)
{
    // Return last 60 datalog entries as a JSON array
    datalog_entry_t entries[60];
    int n = datalog_get_last(entries, 60);

    char *buf = malloc(n * 80 + 8);
    if (!buf) {
        send_json(c, 500, "{\"error\":\"out of memory\"}");
        return;
    }

    int pos = 0;
    pos += sprintf(buf + pos, "[");
    for (int i = 0; i < n; i++) {
        if (i > 0) pos += sprintf(buf + pos, ",");
        pos += sprintf(buf + pos,
                       "{\"t\":%lu,\"f\":%.1f,\"state\":%d}",
                       (unsigned long)entries[i].timestamp_s,
                       entries[i].temperature_f,
                       entries[i].state);
    }
    pos += sprintf(buf + pos, "]");

    send_json(c, 200, buf);
    free(buf);
}

static void handle_root(struct mg_connection *c)
{
    static const char html[] =
        "<!DOCTYPE html><html><head>"
        "<title>IoT Crockpot Simulator</title>"
        "<style>body{font-family:sans-serif;max-width:600px;margin:2em auto}"
        "button{margin:4px;padding:8px 16px;cursor:pointer}"
        "#status{background:#222;color:#aef;padding:1em;border-radius:6px;"
        "font-family:monospace;white-space:pre}</style>"
        "</head><body>"
        "<h2>IoT Crockpot Simulator</h2>"
        "<div id='status'>Loading...</div>"
        "<h3>Heat State</h3>"
        "<button onclick=\"setState('off')\">OFF</button>"
        "<button onclick=\"setState('warm')\">WARM</button>"
        "<button onclick=\"setState('low')\">LOW</button>"
        "<button onclick=\"setState('high')\">HIGH</button>"
        "<h3>Schedules</h3>"
        "<button onclick=\"startSched('Slow Cook')\">Slow Cook</button>"
        "<button onclick=\"startSched('Quick Warm')\">Quick Warm</button>"
        "<button onclick=\"startSched('All Day')\">All Day</button>"
        "<button onclick=\"stopSched()\">Stop Schedule</button>"
        "<script>"
        "function refresh(){"
        "fetch('/api/status').then(r=>r.json()).then(d=>{"
        "document.getElementById('status').textContent=JSON.stringify(d,null,2)"
        "})}"
        "function setState(s){"
        "fetch('/api/state/'+s,{method:'POST'}).then(refresh)}"
        "function startSched(n){"
        "fetch('/api/schedule/start/'+encodeURIComponent(n),"
        "{method:'POST'}).then(refresh)}"
        "function stopSched(){"
        "fetch('/api/schedule/stop',{method:'POST'}).then(refresh)}"
        "refresh(); setInterval(refresh,2000);"
        "</script></body></html>";

    mg_http_reply(c, 200,
                  "Content-Type: text/html\r\n",
                  "%s", html);
}

// ── Mongoose event handler ────────────────────────────────────────────────────

static void fn(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (mg_match(hm->uri, mg_str("/api/status"), NULL) &&
        mg_match(hm->method, mg_str("GET"), NULL)) {
        handle_get_status(c);
    } else if (mg_match(hm->uri, mg_str("/api/state/*"), NULL) &&
               mg_match(hm->method, mg_str("POST"), NULL)) {
        handle_post_state(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/schedules"), NULL) &&
               mg_match(hm->method, mg_str("GET"), NULL)) {
        handle_get_schedules(c);
    } else if (mg_match(hm->uri, mg_str("/api/schedule/start/*"), NULL) &&
               mg_match(hm->method, mg_str("POST"), NULL)) {
        handle_post_schedule_start(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/schedule/stop"), NULL) &&
               mg_match(hm->method, mg_str("POST"), NULL)) {
        handle_post_schedule_stop(c);
    } else if (mg_match(hm->uri, mg_str("/api/history"), NULL) &&
               mg_match(hm->method, mg_str("GET"), NULL)) {
        handle_get_history(c);
    } else if (mg_match(hm->uri, mg_str("/"), NULL)) {
        handle_root(c);
    } else {
        send_json(c, 404, "{\"error\":\"not found\"}");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void web_server_init(void)
{
    const char *port_env = getenv("SIM_HTTP_PORT");
    const char *port     = port_env ? port_env : DEFAULT_PORT;

    char addr[32];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%s", port);

    mg_mgr_init(&s_mgr);

    struct mg_connection *c = mg_http_listen(&s_mgr, addr, fn, NULL);
    if (!c) {
        fprintf(stderr, "[web_server] WARN: failed to bind to %s\n", addr);
        return;
    }

    s_initialized = true;
    fprintf(stderr, "[web_server] listening on http://localhost:%s\n", port);
}

void web_server_poll(void)
{
    if (!s_initialized) return;
    mg_mgr_poll(&s_mgr, 0);
}

void web_server_shutdown(void)
{
    if (!s_initialized) return;
    mg_mgr_free(&s_mgr);
    s_initialized = false;
}
