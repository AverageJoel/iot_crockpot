/**
 * @file telegram.c
 * @brief Telegram bot client using mongoose for the PC simulator.
 *
 * Token is read from SIM_TELEGRAM_TOKEN.  If unset or if TLS is not
 * available (MG_TLS=MG_TLS_NONE), Telegram is silently disabled.
 *
 * Uses standard mongoose HTTP/1.0 client pattern:
 *   mg_http_connect → MG_EV_CONNECT → mg_printf request → MG_EV_HTTP_MSG
 */

#include "telegram.h"
#include "datalog.h"
#include "crockpot.h"

#include "mongoose.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

// ── Config ────────────────────────────────────────────────────────────────────

#define TG_API_HOST  "https://api.telegram.org"

// ── State ─────────────────────────────────────────────────────────────────────

static char s_token[256]  = {0};
static bool s_enabled     = false;
static bool s_initialized = false;

static struct mg_mgr s_mgr;

// Last processed update_id (for getUpdates offset)
static long long s_last_update_id = 0;

// Are we waiting for a response from Telegram?
static bool s_busy = false;

// ── Pending replies ───────────────────────────────────────────────────────────

#define MAX_PENDING_REPLIES 8
typedef struct {
    long long chat_id;
    char      text[1024];
} pending_reply_t;

static pending_reply_t s_replies[MAX_PENDING_REPLIES];
static int             s_reply_count = 0;
static int             s_reply_sent  = 0;

// ── Context passed to mongoose event handler ──────────────────────────────────

typedef enum {
    TG_REQ_GET_UPDATES,
    TG_REQ_SEND_MESSAGE,
} tg_req_type_t;

typedef struct {
    tg_req_type_t type;
    long long     chat_id;
    char          body[800];  // pre-built JSON body for sendMessage
    int           body_len;
} tg_req_ctx_t;

static tg_req_ctx_t s_req_ctx;

// ── JSON helpers ──────────────────────────────────────────────────────────────

/** Find "key": in json and return pointer to the value start. */
static const char *json_find(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    return strstr(json, search);
}

/** Extract integer value for "key":number from json block. */
static long long json_int(const char *json, const char *key)
{
    const char *p = json_find(json, key);
    if (!p) return -1;
    p += strlen(key) + 3;  // skip "key":
    while (*p == ' ') p++;
    return strtoll(p, NULL, 10);
}

/** Extract string value for "key":"value" from json block into buf. */
static bool json_string(const char *json, const char *key,
                         char *buf, size_t buf_len)
{
    const char *p = json_find(json, key);
    if (!p) return false;
    p += strlen(key) + 3;
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n < buf_len - 1) {
        buf[n++] = *p++;
    }
    buf[n] = '\0';
    return true;
}

// ── Escape string for JSON ────────────────────────────────────────────────────

static int json_escape(const char *src, char *dst, size_t dst_len)
{
    int di = 0;
    for (int si = 0; src[si] && di < (int)dst_len - 2; si++) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
        }
        dst[di++] = c;
    }
    dst[di] = '\0';
    return di;
}

// ── Message formatting ────────────────────────────────────────────────────────

static void format_status(char *buf, size_t len)
{
    crockpot_status_t st = crockpot_get_status();
    uint32_t h = st.uptime_seconds / 3600;
    uint32_t m = (st.uptime_seconds % 3600) / 60;

    int n = snprintf(buf, len,
                     "\xF0\x9F\x8D\xB2 IoT Crockpot Simulator\n"
                     "State: %s\n"
                     "Temp: %.1f\xC2\xB0""F\n"
                     "Relay: M=%s A=%s\n"
                     "Uptime: %luh %02lum\n",
                     crockpot_state_to_string(st.state),
                     st.temperature_f,
                     st.relay_main ? "ON" : "OFF",
                     st.relay_aux  ? "ON" : "OFF",
                     (unsigned long)h,
                     (unsigned long)m);

    if (st.schedule_active && n < (int)len - 80) {
        uint32_t rh = st.schedule_step_remaining_s / 3600;
        uint32_t rm = (st.schedule_step_remaining_s % 3600) / 60;
        if (st.schedule_step_remaining_s > 0) {
            snprintf(buf + n, len - (size_t)n,
                     "Schedule: %s (Step %d/%d, %luh %02lum left)\n",
                     st.schedule_name,
                     st.schedule_step + 1, st.schedule_total_steps,
                     (unsigned long)rh, (unsigned long)rm);
        } else {
            snprintf(buf + n, len - (size_t)n,
                     "Schedule: %s (Step %d/%d)\n",
                     st.schedule_name,
                     st.schedule_step + 1, st.schedule_total_steps);
        }
    }

    if (strlen(buf) + 14 < len) {
        strncat(buf, st.sensor_error ? "Sensor: ERROR" : "Sensor: OK",
                len - strlen(buf) - 1);
    }
}

// ── Command processing ────────────────────────────────────────────────────────

static void queue_reply(long long chat_id, const char *text)
{
    if (s_reply_count >= MAX_PENDING_REPLIES) return;
    s_replies[s_reply_count].chat_id = chat_id;
    strncpy(s_replies[s_reply_count].text, text,
            sizeof(s_replies[0].text) - 1);
    s_replies[s_reply_count].text[sizeof(s_replies[0].text) - 1] = '\0';
    s_reply_count++;
}

static void process_command(long long chat_id, const char *text)
{
    char reply[512];

    if (strncmp(text, "/status", 7) == 0 || strcmp(text, "/start") == 0) {
        format_status(reply, sizeof(reply));
    } else if (strcmp(text, "/off") == 0) {
        crockpot_schedule_stop();
        crockpot_set_state(CROCKPOT_OFF);
        snprintf(reply, sizeof(reply), "Crockpot set to OFF");
    } else if (strcmp(text, "/warm") == 0) {
        crockpot_set_state(CROCKPOT_WARM);
        snprintf(reply, sizeof(reply), "Crockpot set to WARM");
    } else if (strcmp(text, "/low") == 0) {
        crockpot_set_state(CROCKPOT_LOW);
        snprintf(reply, sizeof(reply), "Crockpot set to LOW");
    } else if (strcmp(text, "/high") == 0) {
        crockpot_set_state(CROCKPOT_HIGH);
        snprintf(reply, sizeof(reply), "Crockpot set to HIGH");
    } else if (strcmp(text, "/schedule") == 0) {
        snprintf(reply, sizeof(reply),
                 "Available schedules:\n"
                 "  /slow  — Slow Cook (HIGH 1h \xe2\x86\x92 LOW 6h \xe2\x86\x92 WARM)\n"
                 "  /quick — Quick Warm (HIGH 30m \xe2\x86\x92 WARM)\n"
                 "  /allday — All Day (LOW 8h \xe2\x86\x92 WARM)\n"
                 "  /stopschedule — stop current schedule");
    } else if (strcmp(text, "/slow") == 0) {
        crockpot_schedule_start(&CROCKPOT_SCHED_SLOW_COOK);
        snprintf(reply, sizeof(reply), "Slow Cook schedule started");
    } else if (strcmp(text, "/quick") == 0) {
        crockpot_schedule_start(&CROCKPOT_SCHED_QUICK_WARM);
        snprintf(reply, sizeof(reply), "Quick Warm schedule started");
    } else if (strcmp(text, "/allday") == 0) {
        crockpot_schedule_start(&CROCKPOT_SCHED_ALL_DAY);
        snprintf(reply, sizeof(reply), "All Day schedule started");
    } else if (strcmp(text, "/stopschedule") == 0) {
        crockpot_schedule_stop();
        snprintf(reply, sizeof(reply), "Schedule stopped");
    } else if (strcmp(text, "/log") == 0) {
        char fname[64];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(fname, sizeof(fname), "crockpot_log_%Y%m%d_%H%M%S.csv", t);
        if (datalog_export_csv(fname)) {
            snprintf(reply, sizeof(reply),
                     "Log exported: %s (%d samples)", fname, datalog_count());
        } else {
            snprintf(reply, sizeof(reply), "No data to export yet.");
        }
    } else if (strcmp(text, "/help") == 0) {
        snprintf(reply, sizeof(reply),
                 "/status /off /warm /low /high\n"
                 "/schedule /slow /quick /allday /stopschedule\n"
                 "/log /help");
    } else {
        snprintf(reply, sizeof(reply),
                 "Unknown command. Use /help for a list.");
    }

    queue_reply(chat_id, reply);
}

// ── Response parsing ──────────────────────────────────────────────────────────

static void parse_updates(const char *body)
{
    const char *p = body;
    while ((p = strstr(p, "\"update_id\":")) != NULL) {
        long long uid = strtoll(p + 12, NULL, 10);

        if (uid > s_last_update_id) {
            s_last_update_id = uid;

            // Extract this update's block
            const char *next = strstr(p + 1, "\"update_id\":");
            size_t blen = next ? (size_t)(next - p) : strlen(p);
            if (blen >= 1024) blen = 1023;

            char block[1024];
            memcpy(block, p, blen);
            block[blen] = '\0';

            char text[256] = {0};
            if (json_string(block, "text", text, sizeof(text))) {
                long long chat_id = json_int(block, "id");
                if (chat_id > 0) {
                    fprintf(stderr, "[telegram] %lld: %s\n", chat_id, text);
                    process_command(chat_id, text);
                }
            }
        }
        p++;
    }
}

// ── Mongoose event handler ────────────────────────────────────────────────────

static void tg_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    tg_req_ctx_t *req = (tg_req_ctx_t *)c->fn_data;

    if (ev == MG_EV_CONNECT) {
        // Connection established (or TLS handshake done for HTTPS).
        // Send the HTTP request now.
        if (req->type == TG_REQ_GET_UPDATES) {
            mg_printf(c,
                      "GET /bot%s/getUpdates?offset=%lld&timeout=0"
                      "&allowed_updates=%%5B%%22message%%22%%5D"
                      " HTTP/1.0\r\n"
                      "Host: api.telegram.org\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      s_token,
                      s_last_update_id + 1);
        } else {
            mg_printf(c,
                      "POST /bot%s/sendMessage HTTP/1.0\r\n"
                      "Host: api.telegram.org\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %d\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "%.*s",
                      s_token,
                      req->body_len,
                      req->body_len, req->body);
        }
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (req->type == TG_REQ_GET_UPDATES) {
            // Null-terminate body for string scanning
            char *body = malloc(hm->body.len + 1);
            if (body) {
                memcpy(body, hm->body.buf, hm->body.len);
                body[hm->body.len] = '\0';
                parse_updates(body);
                free(body);
            }
        }
        c->is_closing = 1;
        s_busy = false;
    } else if (ev == MG_EV_ERROR) {
        fprintf(stderr, "[telegram] connection error: %s\n", (char *)ev_data);
        c->is_closing = 1;
        s_busy = false;
    } else if (ev == MG_EV_CLOSE) {
        s_busy = false;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void telegram_init(void)
{
    const char *tok = getenv("SIM_TELEGRAM_TOKEN");
    if (!tok || tok[0] == '\0') {
        fprintf(stderr,
                "[telegram] SIM_TELEGRAM_TOKEN not set — Telegram disabled\n");
        s_enabled = false;
        return;
    }

#if MG_TLS == MG_TLS_NONE
    fprintf(stderr,
            "[telegram] TLS disabled (MG_TLS=MG_TLS_NONE) — "
            "Telegram disabled (requires HTTPS)\n");
    s_enabled = false;
    return;
#endif

    strncpy(s_token, tok, sizeof(s_token) - 1);
    s_token[sizeof(s_token) - 1] = '\0';

    mg_mgr_init(&s_mgr);
    s_enabled     = true;
    s_initialized = true;
    s_busy        = false;
    fprintf(stderr, "[telegram] enabled (token: %.*s...)\n", 8, s_token);
}

void telegram_poll(void)
{
    if (!s_enabled || !s_initialized) return;

    if (!s_busy) {
        if (s_reply_sent < s_reply_count) {
            // Send a queued reply
            pending_reply_t *r = &s_replies[s_reply_sent++];
            if (s_reply_sent >= s_reply_count) {
                s_reply_count = 0;
                s_reply_sent  = 0;
            }

            // Build JSON body (escape the text)
            char escaped[900];
            json_escape(r->text, escaped, sizeof(escaped));

            s_req_ctx.type     = TG_REQ_SEND_MESSAGE;
            s_req_ctx.chat_id  = r->chat_id;
            s_req_ctx.body_len = snprintf(s_req_ctx.body, sizeof(s_req_ctx.body),
                                          "{\"chat_id\":%lld,\"text\":\"%s\"}",
                                          r->chat_id, escaped);

            char url[256];
            snprintf(url, sizeof(url), "%s/bot%s/sendMessage",
                     TG_API_HOST, s_token);
            struct mg_connection *c =
                mg_http_connect(&s_mgr, url, tg_ev_handler, &s_req_ctx);
            if (c) {
                s_busy = true;
            }
        } else {
            // Poll for new updates
            s_req_ctx.type = TG_REQ_GET_UPDATES;
            char url[256];
            snprintf(url, sizeof(url), "%s/bot%s/getUpdates",
                     TG_API_HOST, s_token);
            struct mg_connection *c =
                mg_http_connect(&s_mgr, url, tg_ev_handler, &s_req_ctx);
            if (c) {
                s_busy = true;
            }
        }
    }

    mg_mgr_poll(&s_mgr, 0);
}

void telegram_shutdown(void)
{
    if (!s_initialized) return;
    mg_mgr_free(&s_mgr);
    s_initialized = false;
    s_enabled     = false;
}
