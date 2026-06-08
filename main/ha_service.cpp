#include "ha_service.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ha_service";

static esp_websocket_client_handle_t s_client;
static HaStateCallback               s_state_cb;
static HaStatusCallback              s_status_cb;
static HaRuntimeConfig               s_config;
static bool                          s_connected;
static bool                          s_authenticated;
static int                           s_msg_id;

// Timestamp (ms) kiedy ostatnio utracono połączenie; 0 = połączone
static int64_t s_disconnected_since_ms = 0;

// Śledzenie subskrypcji — czy HA potwierdził subscribe_entities?
static int     s_sub_id        = 0;    // msg_id wysłanego subscribe_entities
static bool    s_sub_confirmed = false; // HA odpowiedział success:true dla s_sub_id
static int64_t s_last_sub_ms   = 0;   // czas ostatniego send_subscribe (ms uptime)

// ---------------------------------------------------------------------------
// Bufor skladania fragmentowanych wiadomosci WebSocket
// ---------------------------------------------------------------------------
#define WS_RX_BUF_SIZE (64 * 1024)   // 64 KB — HA moze wysylac duze wiadomosci JSON
static char *s_rx_buf    = nullptr;
static int   s_rx_filled = 0;

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------
static void ws_send(const char *json)
{
    if (!s_client || !s_connected) {
        ESP_LOGW(TAG, "TX skip: not connected");
        return;
    }
    ESP_LOGI(TAG, "TX: %.120s", json);
    int ret = esp_websocket_client_send_text(s_client, json, strlen(json),
                                              pdMS_TO_TICKS(5000));
    if (ret < 0)
        ESP_LOGE(TAG, "TX failed: ret=%d", ret);
}

static void send_auth()
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "auth");
    cJSON_AddStringToObject(msg, "access_token", s_config.token);
    char *txt = cJSON_PrintUnformatted(msg);
    ws_send(txt);
    cJSON_free(txt);
    cJSON_Delete(msg);
}

// ---------------------------------------------------------------------------
// Subskrypcja: zbiera unikalne entity_id ze wszystkich ekranow/kafelkow
// ---------------------------------------------------------------------------
static void send_subscribe()
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "subscribe_entities");
    s_sub_id        = ++s_msg_id;
    s_sub_confirmed = false;
    s_last_sub_ms   = esp_timer_get_time() / 1000LL;
    cJSON_AddNumberToObject(msg, "id", s_sub_id);
    cJSON *ids = cJSON_AddArrayToObject(msg, "entity_ids");

    // Prosty zestaw unikalnych entity_id (maks. 128 encji)
    static char visited[128][64];
    size_t visited_count = 0;

    for (size_t s = 0; s < ha_screen_count(); ++s) {
        const HaScreen *screen = ha_screen_get(s);
        if (!screen) continue;
        for (size_t t = 0; t < screen->tile_count; ++t) {
            for (size_t r = 0; r < screen->tiles[t].row_count; ++r) {
                const char *eid = screen->tiles[t].rows[r].entity_id;
                if (eid[0] == '\0') continue;
                bool already = false;
                for (size_t v = 0; v < visited_count; ++v) {
                    if (strcmp(visited[v], eid) == 0) { already = true; break; }
                }
                if (!already && visited_count < 128) {
                    strlcpy(visited[visited_count++], eid, 64);
                    cJSON_AddItemToArray(ids, cJSON_CreateString(eid));
                }
            }
        }
    }

    char *txt = cJSON_PrintUnformatted(msg);
    ws_send(txt);
    cJSON_free(txt);
    cJSON_Delete(msg);
    ESP_LOGI(TAG, "Subscribed to %u unique entities", (unsigned)visited_count);
}

// ---------------------------------------------------------------------------
// Ekstrakcja wartosci z obiektu stanu HA dla danego wiersza
// state_obj – obiekt encji z pola "a" lub "+" w polu "c"
// ---------------------------------------------------------------------------
static void extract_row_value(const HaRow *row, cJSON *state_obj,
                               char *out, size_t out_len)
{
    if (!row || !state_obj || out_len == 0) return;

    cJSON *value_node = nullptr;
    if (row->attribute[0] == '\0') {
        value_node = cJSON_GetObjectItemCaseSensitive(state_obj, "s");
    } else {
        cJSON *attrs = cJSON_GetObjectItemCaseSensitive(state_obj, "a");
        if (attrs)
            value_node = cJSON_GetObjectItemCaseSensitive(attrs, row->attribute);
    }

    if (!value_node) { strlcpy(out, "--", out_len); return; }

    if (cJSON_IsString(value_node))
        strlcpy(out, value_node->valuestring, out_len);
    else if (cJSON_IsNumber(value_node))
        snprintf(out, out_len, "%.4g", value_node->valuedouble);
    else if (cJSON_IsBool(value_node))
        strlcpy(out, cJSON_IsTrue(value_node) ? "on" : "off", out_len);
    else
        strlcpy(out, "--", out_len);
}

// ---------------------------------------------------------------------------
// Wywoluje s_state_cb dla wszystkich wierszy pasujacych do entity_id
// ---------------------------------------------------------------------------
static void dispatch_entity_state(const char *entity_id, cJSON *state_obj)
{
    for (size_t s = 0; s < ha_screen_count(); ++s) {
        const HaScreen *screen = ha_screen_get(s);
        if (!screen) continue;
        for (size_t t = 0; t < screen->tile_count; ++t) {
            for (size_t r = 0; r < screen->tiles[t].row_count; ++r) {
                const HaRow *row = &screen->tiles[t].rows[r];
                if (strcmp(row->entity_id, entity_id) != 0) continue;
                char value[64] = {};
                extract_row_value(row, state_obj, value, sizeof(value));
                ESP_LOGD(TAG, "[state] %s[%u/%u/%u] = %s",
                         entity_id, (unsigned)s, (unsigned)t, (unsigned)r, value);
                if (s_state_cb) s_state_cb(s, t, r, value);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Obsluga zdarzenia subscribe_entities
// "a" = pelny stan (added/snapshot)
// "c" = zmiana (diff z kluczem "+")
// ---------------------------------------------------------------------------
static void handle_entity_event(cJSON *event_obj)
{
    if (!event_obj) return;

    // ---- "a" : snapshot / dodane encje --------------------------------
    {
        cJSON *group = cJSON_GetObjectItemCaseSensitive(event_obj, "a");
        if (cJSON_IsObject(group)) {
            cJSON *entry = nullptr;
            cJSON_ArrayForEach(entry, group) {
                if (entry->string)
                    dispatch_entity_state(entry->string, entry);
            }
        }
    }

    // ---- "c" : diff ---------------------------------------------------
    {
        cJSON *group = cJSON_GetObjectItemCaseSensitive(event_obj, "c");
        if (cJSON_IsObject(group)) {
            cJSON *entry = nullptr;
            cJSON_ArrayForEach(entry, group) {
                if (!entry->string) continue;
                cJSON *plus = cJSON_GetObjectItemCaseSensitive(entry, "+");
                if (cJSON_IsObject(plus))
                    dispatch_entity_state(entry->string, plus);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Parser wiadomosci WebSocket
// ---------------------------------------------------------------------------
static void handle_message(const char *data, int len)
{
    if (!data || len <= 0) return;
    ESP_LOGD(TAG, "RX: %.200s", data);

    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed (len=%d)", len);
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type)) { cJSON_Delete(root); return; }

    const char *t = type->valuestring;

    if (strcmp(t, "auth_required") == 0) {
        ESP_LOGI(TAG, "Auth required");
        send_auth();

    } else if (strcmp(t, "auth_ok") == 0) {
        ESP_LOGI(TAG, "Authenticated OK");
        s_authenticated = true;
        if (s_status_cb) s_status_cb(true);
        send_subscribe();

    } else if (strcmp(t, "auth_invalid") == 0) {
        ESP_LOGE(TAG, "Auth INVALID");
        s_authenticated = false;
        if (s_status_cb) s_status_cb(false);

    } else if (strcmp(t, "event") == 0) {
        cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
        handle_entity_event(event);

    } else if (strcmp(t, "result") == 0) {
        cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
        cJSON *id_j    = cJSON_GetObjectItemCaseSensitive(root, "id");
        int    msg_id  = cJSON_IsNumber(id_j) ? (int)id_j->valuedouble : -1;
        bool   is_sub  = (s_sub_id > 0 && msg_id == s_sub_id);

        if (cJSON_IsTrue(success)) {
            if (is_sub) {
                ESP_LOGI(TAG, "Subskrypcja potwierdzona (id=%d)", msg_id);
                s_sub_confirmed = true;
            }
        } else {
            cJSON *err     = cJSON_GetObjectItemCaseSensitive(root, "error");
            cJSON *err_msg = err ? cJSON_GetObjectItemCaseSensitive(err, "message") : nullptr;
            const char *reason = (cJSON_IsString(err_msg) && err_msg->valuestring)
                                 ? err_msg->valuestring : "?";
            if (is_sub) {
                // HA jeszcze nie gotowe lub problem — watchdog ponowi subscribe
                ESP_LOGW(TAG, "Subskrypcja nieudana (id=%d): %s — retry za chwile",
                         msg_id, reason);
                s_sub_confirmed = false;   // watchdog wyzwoli retry
            } else {
                ESP_LOGW(TAG, "Komenda nieudana (id=%d): %s", msg_id, reason);
            }
        }
    }

    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base;
    auto *data = static_cast<esp_websocket_event_data_t *>(event_data);

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        s_connected            = true;
        s_authenticated        = false;
        s_disconnected_since_ms = 0;   // watchdog: połączono
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        s_connected     = false;
        s_authenticated = false;
        s_sub_confirmed = false;
        s_sub_id        = 0;
        if (s_disconnected_since_ms == 0)
            s_disconnected_since_ms = esp_timer_get_time() / 1000LL;
        if (s_status_cb) s_status_cb(false);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data || !data->data_ptr || data->data_len <= 0 || !s_rx_buf) break;
        // Ignoruj ping (0x09), pong (0x0A), close (0x08) — tylko tekst/binary
        if (data->op_code != 0x00 && data->op_code != 0x01 && data->op_code != 0x02) break;
        if (data->payload_offset == 0) {
            s_rx_filled = 0;
        }
        {
            int avail = WS_RX_BUF_SIZE - s_rx_filled - 1;
            int n = (data->data_len < avail) ? data->data_len : avail;
            if (n > 0) {
                memcpy(s_rx_buf + s_rx_filled, data->data_ptr, n);
                s_rx_filled += n;
            }
        }
        // Wykrycie konca wiadomosci: odebrano wszystkie bajty payloadu
        {
            const bool all_received = (data->payload_len > 0)
                ? (data->payload_offset + data->data_len >= (int)data->payload_len)
                : data->fin;
            if (all_received) {
                s_rx_buf[s_rx_filled] = '\0';
                handle_message(s_rx_buf, s_rx_filled);
                s_rx_filled = 0;
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        s_connected     = false;
        s_authenticated = false;
        s_sub_confirmed = false;
        s_sub_id        = 0;
        if (s_disconnected_since_ms == 0)
            s_disconnected_since_ms = esp_timer_get_time() / 1000LL;
        if (s_status_cb) s_status_cb(false);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Watchdog — pilnuje połączenia, wymusza reconnect gdy offline > 15s
// ---------------------------------------------------------------------------
static void watchdog_task(void *)
{
    // Odczekaj chwilę po starcie żeby WS zdążył się połączyć
    vTaskDelay(pdMS_TO_TICKS(20000));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));   // sprawdzaj co 10s

        if (!s_client) continue;

        int64_t now_ms = esp_timer_get_time() / 1000LL;

        // ── Przypadek 1: połączony i uwierzytelniony ─────────────────────────
        if (s_connected && s_authenticated) {
            // Subskrypcja niepotwierdzona? → ponów po 5 s od ostatniej próby
            if (!s_sub_confirmed && s_last_sub_ms > 0 &&
                (now_ms - s_last_sub_ms) >= 5000) {
                ESP_LOGW(TAG, "Watchdog: subskrypcja niepotwierdzona — ponawiam");
                send_subscribe();
            }
            // Połączenie wygląda OK — pomiń logikę offline
            continue;
        }

        // ── Przypadek 2: offline ─────────────────────────────────────────────
        if (s_disconnected_since_ms == 0) {
            s_disconnected_since_ms = now_ms;
            continue;
        }

        int64_t offline_ms = now_ms - s_disconnected_since_ms;
        ESP_LOGW(TAG, "Watchdog: offline %.1f s", offline_ms / 1000.0f);

        if (offline_ms >= 15000) {
            // Wbudowany auto-reconnect nie działa — wymuś ręcznie
            ESP_LOGW(TAG, "Watchdog: wymuszam reconnect");
            esp_websocket_client_stop(s_client);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_websocket_client_start(s_client);
            // Zresetuj timer żeby nie hammerować co iterację
            s_disconnected_since_ms = now_ms;
        }
    }
}

// ---------------------------------------------------------------------------
// Publiczne API
// ---------------------------------------------------------------------------
void ha_service_start(const HaRuntimeConfig *config,
                      HaStateCallback state_cb,
                      HaStatusCallback status_cb)
{
    if (!config || !ha_config_ready(config)) {
        ESP_LOGW(TAG, "HA config missing");
        if (status_cb) status_cb(false);
        return;
    }

    s_config    = *config;
    s_state_cb  = state_cb;
    s_status_cb = status_cb;
    s_msg_id    = 1;
    s_rx_filled = 0;

    if (!s_rx_buf) {
        s_rx_buf = static_cast<char *>(
            heap_caps_malloc(WS_RX_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_rx_buf)
            s_rx_buf = static_cast<char *>(malloc(WS_RX_BUF_SIZE));
        if (!s_rx_buf)
            ESP_LOGE(TAG, "Brak pamieci na bufor WS!");
        else
            ESP_LOGI(TAG, "WS RX bufor: %d B @ %p", WS_RX_BUF_SIZE, s_rx_buf);
    }

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri                   = s_config.url;
    ws_cfg.reconnect_timeout_ms  = 3000;
    ws_cfg.network_timeout_ms    = 1000;
    ws_cfg.ping_interval_sec     = 2;   // Ping co 2s gdy brak ruchu — wymusza SPI transfer
    ws_cfg.pingpong_timeout_sec  = 8;   // Reconnect jesli brak ponga przez 8s
    ws_cfg.buffer_size           = 16384;

    s_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, nullptr);
    esp_websocket_client_start(s_client);
    ESP_LOGI(TAG, "WS client -> %s", s_config.url);

    // Uruchom watchdog — pilnuje połączenia niezależnie od wbudowanego auto-reconnect
    xTaskCreate(watchdog_task, "ha_ws_wdog", 3072, nullptr, 3, nullptr);
}

void ha_service_send_command(size_t screen_idx, size_t tile_idx,
                             size_t row_idx, bool turn_on)
{
    if (!s_client || !s_authenticated) return;

    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen || tile_idx >= screen->tile_count) return;
    const HaTile *tile = &screen->tiles[tile_idx];
    if (row_idx >= tile->row_count) return;
    const HaRow *row = &tile->rows[row_idx];

    const char *service = turn_on ? "turn_on" : "turn_off";

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id",      ++s_msg_id);
    cJSON_AddStringToObject(msg, "type",    "call_service");
    cJSON_AddStringToObject(msg, "domain",  row->domain);
    cJSON_AddStringToObject(msg, "service", service);
    cJSON *target = cJSON_AddObjectToObject(msg, "target");
    cJSON_AddStringToObject(target, "entity_id", row->entity_id);

    char *txt = cJSON_PrintUnformatted(msg);
    ESP_LOGI(TAG, "CMD %s -> %s.%s", row->entity_id, row->domain, service);
    ws_send(txt);
    cJSON_free(txt);
    cJSON_Delete(msg);
}

bool ha_service_connected()
{
    return s_connected && s_authenticated;
}
