#include "web_config.h"
#include "ha_config.h"
#include "ha_entities.h"
#include "features_config.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web_config";
static httpd_handle_t s_server;
static HaRuntimeConfig s_config;
static HwFeatures s_features;

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------
static void json_escape_send(httpd_req_t *req, const char *text)
{
    for (const char *p = text; p && *p; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') {
            char esc[3] = {'\\', c, '\0'};
            httpd_resp_sendstr_chunk(req, esc);
        } else if ((unsigned char)c >= 0x20) {
            char out[2] = {c, '\0'};
            httpd_resp_sendstr_chunk(req, out);
        }
    }
}

static bool json_copy_string(cJSON *root, const char *key, char *out, size_t out_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    strlcpy(out, item->valuestring, out_len);
    return true;
}

// ---------------------------------------------------------------------------
// Strona glowna — HTML + JS
// ---------------------------------------------------------------------------
static const char P1[] =
    "<!doctype html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='utf-8'><title>HA Panel — Ustawienia</title><style>"
    "body{font-family:Arial,sans-serif;margin:0;background:#eef3f6;color:#16232c}"
    ".wrap{max-width:600px;margin:0 auto;padding:16px}"
    "h2{margin:0 0 16px}"
    "section{background:white;border:1px solid #d8e1e7;border-radius:8px;"
    "padding:18px;margin-bottom:14px}"
    "h3{margin:0 0 12px;font-size:16px}"
    "label{display:block;margin:10px 0 4px;font-size:13px}"
    "input[type=text],input[type=password]{width:100%;box-sizing:border-box;"
    "padding:10px;border:1px solid #b8c5cc;border-radius:6px;font-size:14px}"
    "button{margin:4px 4px 4px 0;padding:9px 14px;border:0;border-radius:6px;"
    "background:#0f6b63;color:white;font-weight:700;cursor:pointer;font-size:13px}"
    "button.sec{background:#5b6b75}"
    ".toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:10px}"
    ".hint{font-size:12px;color:#5b6b75;margin-top:3px}"
    "pre{white-space:pre-wrap;background:#12202a;color:#e8f3f1;padding:12px;"
    "border-radius:6px;max-height:180px;overflow:auto;font-size:12px}"
    "</style></head><body><div class='wrap'>"
    "<h2>HA Panel &mdash; Ustawienia</h2>";

static const char P2[] =
    "<section><h3>Polaczenie z Home Assistant</h3>"
    "<label>WebSocket URL</label>"
    "<input id='url' type='text' placeholder='ws://192.168.1.100:8123/api/websocket'>"
    "<p class='hint'>HTTP: ws://IP:8123/api/websocket &nbsp;|&nbsp; HTTPS: wss://domena/api/websocket</p>"
    "<label>Long-Lived Access Token</label>"
    "<input id='token' type='password' placeholder='eyJ0eXAiOiJKV1Qi...'>"
    "<p class='hint'>HA -> Profil -> Tokeny -> Utwórz token</p>"
    "<div class='toolbar'>"
    "<button onclick='saveHa()'>Zapisz polaczenie</button>"
    "<button onclick='testConn()' class='sec'>Test</button>"
    "<button onclick='reboot()' class='sec'>Restart</button>"
    "</div>"
    "<pre id='haout' style='display:none'></pre>"
    "</section>";

// Sekcja konfiguracji sprzetu
static const char P_FEATURES[] =
    "<section><h3>Sprzet</h3>"
    "<p class='hint'>Wylacz modul jesli plytka nie posiada danego podzespolu. Zmiana zapisuje sie w pamieci i wymaga restartu.</p>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_camera' style='width:18px;height:18px'>"
    "Kamera (detekcja ruchu, budzenie ekranu)</label>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_battery' style='width:18px;height:18px'>"
    "Bateria (monitor napiecia ADC)</label>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_speaker' style='width:18px;height:18px'>"
    "Glosnik (zarezerwowane, niezaimplementowane)</label>"
    "<div class='toolbar' style='margin-top:10px'>"
    "<button onclick='saveFeatures()'>Zapisz i restartuj</button>"
    "</div>"
    "<pre id='featout' style='display:none'></pre>"
    "</section>";

// Sekcja eksport / import konfiguracji urzadzenia
static const char P_CFGIO[] =
    "<section><h3>Kopia konfiguracji</h3>"
    "<p class='hint'>Eksportuje polaczenie HA i ustawienia sprzetu do pliku JSON. "
    "Przydatne przy wymianie urzadzenia lub aktualizacji firmware.</p>"
    "<div class='toolbar'>"
    "<button onclick='exportCfg()'>&#11123; Eksportuj</button>"
    "<button onclick='document.getElementById(\"cfgFile\").click()' class='sec'>&#11121; Importuj</button>"
    "<input type='file' id='cfgFile' accept='.json' style='display:none' onchange='importCfg(this)'>"
    "</div>"
    "<pre id='cfgout' style='display:none'></pre>"
    "</section>";

// JavaScript — tylko ustawienia urzadzenia (HA polaczenie + sprzet)
static const char P4[] =
    "<script>"
    "async function load(){"
    "try{let r=await fetch('/api/ha');let j=await r.json();"
    "document.getElementById('url').value=j.url||'';"
    "document.getElementById('token').value=j.token||'';}catch(e){}}"

    "async function saveHa(){"
    "let b={url:document.getElementById('url').value,"
    "token:document.getElementById('token').value};"
    "let r=await fetch('/api/ha',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent=await r.text();}"

    "async function testConn(){"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent='Testowanie...';"
    "let r=await fetch('/api/test');out.textContent=await r.text();}"

    "async function reboot(){"
    "await fetch('/api/reboot',{method:'POST'});"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent='Restart...';}"

    "async function loadFeatures(){"
    "try{let r=await fetch('/api/features');let j=await r.json();"
    "document.getElementById('feat_camera').checked=!!j.camera;"
    "document.getElementById('feat_battery').checked=!!j.battery;"
    "document.getElementById('feat_speaker').checked=!!j.speaker;"
    "}catch(e){}}"

    "async function saveFeatures(){"
    "let b={camera:document.getElementById('feat_camera').checked,"
    "battery:document.getElementById('feat_battery').checked,"
    "speaker:document.getElementById('feat_speaker').checked};"
    "let r=await fetch('/api/features',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "let out=document.getElementById('featout');"
    "out.style.display='block';out.textContent=await r.text();}"

    "async function exportCfg(){"
    "try{"
    "let r=await fetch('/api/config');let txt=await r.text();"
    "let b=new Blob([txt],{type:'application/json'});"
    "let u=URL.createObjectURL(b);"
    "let a=document.createElement('a');a.href=u;a.download='ha-panel-config.json';"
    "document.body.appendChild(a);a.click();"
    "document.body.removeChild(a);URL.revokeObjectURL(u);"
    "}catch(e){alert('Blad eksportu: '+e.message);}}"

    "async function importCfg(inp){"
    "if(!inp.files||!inp.files[0])return;"
    "let txt=await inp.files[0].text();"
    "try{JSON.parse(txt);}catch(e){alert('Nieprawidlowy plik: '+e.message);inp.value='';return;}"
    "let out=document.getElementById('cfgout');"
    "out.style.display='block';out.textContent='Importowanie...';"
    "try{"
    "let r=await fetch('/api/config',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:txt});"
    "out.textContent=await r.text();"
    "}catch(e){out.textContent='Blad: '+e.message;}"
    "inp.value='';}"

    "load();loadFeatures();"
    "</script></body></html>";

// ---------------------------------------------------------------------------
// Handlery HTTP
// ---------------------------------------------------------------------------
static esp_err_t send_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, P1);
    httpd_resp_sendstr_chunk(req, P2);
    httpd_resp_sendstr_chunk(req, P_FEATURES);
    httpd_resp_sendstr_chunk(req, P_CFGIO);
    httpd_resp_sendstr_chunk(req, P4);
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

static esp_err_t send_ha_config(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"url\":\"");
    json_escape_send(req, s_config.url);
    httpd_resp_sendstr_chunk(req, "\",\"token\":\"");
    json_escape_send(req, s_config.token);
    httpd_resp_sendstr_chunk(req, "\"}");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

static esp_err_t save_ha_config(httpd_req_t *req)
{
    if (req->content_len > 600)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
    char body[601] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read failed");
    body[n] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    HaRuntimeConfig next = {};
    json_copy_string(root, "url",   next.url,   sizeof(next.url));
    json_copy_string(root, "token", next.token, sizeof(next.token));
    cJSON_Delete(root);
    esp_err_t err = ha_config_save(&next);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    s_config = next;
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }, "restart_ha", 2048, nullptr, 5, nullptr);
    return httpd_resp_send(req, "Zapisano. Restart za chwile...", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t test_ha_connection(httpd_req_t *req)
{
    if (!ha_config_ready(&s_config))
        return httpd_resp_send(req, "Brak konfiguracji HA.", HTTPD_RESP_USE_STRLEN);

    char http_url[160] = {};
    if (strncmp(s_config.url, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http://%s/api/", s_config.url + 5);
        char *p = strstr(http_url, "/api/websocket");
        if (p) strcpy(p, "/api/");
    } else if (strncmp(s_config.url, "wss://", 6) == 0) {
        snprintf(http_url, sizeof(http_url), "https://%s/api/", s_config.url + 6);
        char *p = strstr(http_url, "/api/websocket");
        if (p) strcpy(p, "/api/");
    } else {
        snprintf(http_url, sizeof(http_url), "%s", s_config.url);
    }

    char auth_header[280] = {};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_config.token);

    esp_http_client_config_t cfg = {};
    cfg.url = http_url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    char result[128] = {};
    if (err != ESP_OK)
        snprintf(result, sizeof(result), "Blad: %s", esp_err_to_name(err));
    else if (status == 200 || status == 201)
        snprintf(result, sizeof(result), "OK! HA odpowiedzial kodem %d", status);
    else if (status == 401)
        snprintf(result, sizeof(result), "Blad 401 – zly token");
    else
        snprintf(result, sizeof(result), "HA odpowiedzial kodem %d", status);

    return httpd_resp_send(req, result, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/dashboard — zwraca JSON dashboardu z SPIFFS
// ---------------------------------------------------------------------------
static esp_err_t get_dashboard(httpd_req_t *req)
{
    static const char *kEmpty  = "{\"default_screen\":0,\"screens\":[]}";
    static const char *kDashFile = "/storage/ha_dashboard.json";

    FILE *f = fopen(kDashFile, "r");
    if (!f) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, kEmpty, HTTPD_RESP_USE_STRLEN);
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0 || file_size > 512 * 1024L) {
        fclose(f);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, kEmpty, HTTPD_RESP_USE_STRLEN);
    }

    char *body = static_cast<char *>(calloc(1, (size_t)file_size + 1));
    if (!body) {
        fclose(f);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    }

    fread(body, 1, (size_t)file_size, f);
    fclose(f);
    body[file_size] = '\0';

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return send_err;
}

// ---------------------------------------------------------------------------
// POST /api/dashboard — zapisuje JSON i restartuje
// ---------------------------------------------------------------------------
static esp_err_t save_dashboard(httpd_req_t *req)
{
    if (req->content_len > 256 * 1024)   // 256 KB — SPIFFS nie ma ograniczenia NVS
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
    char *body = static_cast<char *>(calloc(1, req->content_len + 1));
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");

    // httpd_req_recv moze zwrocic mniej niz content_len gdy payload jest podzielony
    // na wiele pakietow TCP (typowe dla JSON > ~1460 B). Petla odbiera calosc.
    int received = 0;
    while (received < (int)req->content_len) {
        int n = httpd_req_recv(req, body + received,
                               (int)req->content_len - received);
        if (n <= 0) {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read error");
        }
        received += n;
    }
    body[received] = '\0';

    // Walidacja JSON przed zapisem — chroni przed zapisaniem uszkodzonego payloadu
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    cJSON_Delete(root);

    esp_err_t err = ha_dashboard_save_json(body);
    free(body);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");

    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }, "restart_db", 2048, nullptr, 5, nullptr);
    return httpd_resp_send(req, "Zapisano. Restart za chwile...", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/config — eksport: polaczenie HA + sprzet w jednym JSON
// ---------------------------------------------------------------------------
static esp_err_t get_config(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"ha_url\":\"");
    json_escape_send(req, s_config.url);
    httpd_resp_sendstr_chunk(req, "\",\"ha_token\":\"");
    json_escape_send(req, s_config.token);
    char feat[80];
    snprintf(feat, sizeof(feat),
             "\",\"features\":{\"camera\":%s,\"battery\":%s,\"speaker\":%s}}",
             s_features.camera  ? "true" : "false",
             s_features.battery ? "true" : "false",
             s_features.speaker ? "true" : "false");
    httpd_resp_sendstr_chunk(req, feat);
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/config — import: zapisuje polaczenie HA + sprzet, restartuje
// ---------------------------------------------------------------------------
static esp_err_t post_config(httpd_req_t *req)
{
    if (req->content_len > 800)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
    char body[801] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read failed");
    body[n] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    HaRuntimeConfig next_ha = s_config;
    HwFeatures next_feat = s_features;

    json_copy_string(root, "ha_url",   next_ha.url,   sizeof(next_ha.url));
    json_copy_string(root, "ha_token", next_ha.token, sizeof(next_ha.token));

    cJSON *feat = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (cJSON_IsObject(feat)) {
        cJSON *cam = cJSON_GetObjectItemCaseSensitive(feat, "camera");
        cJSON *bat = cJSON_GetObjectItemCaseSensitive(feat, "battery");
        cJSON *spk = cJSON_GetObjectItemCaseSensitive(feat, "speaker");
        if (cJSON_IsBool(cam)) next_feat.camera  = cJSON_IsTrue(cam);
        if (cJSON_IsBool(bat)) next_feat.battery = cJSON_IsTrue(bat);
        if (cJSON_IsBool(spk)) next_feat.speaker = cJSON_IsTrue(spk);
    }
    cJSON_Delete(root);

    esp_err_t err = ha_config_save(&next_ha);
    if (err == ESP_OK) err = hw_features_save(&next_feat);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");

    s_config   = next_ha;
    s_features = next_feat;
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }, "restart_cfg", 2048, nullptr, 5, nullptr);
    return httpd_resp_send(req, "Zaimportowano. Restart za chwile...", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/features — zwraca JSON z flagami sprzetu
// ---------------------------------------------------------------------------
static esp_err_t send_features_config(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"camera\":%s,\"battery\":%s,\"speaker\":%s}",
             s_features.camera  ? "true" : "false",
             s_features.battery ? "true" : "false",
             s_features.speaker ? "true" : "false");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /api/features — zapisuje flagi i restartuje
// ---------------------------------------------------------------------------
static esp_err_t save_features_config(httpd_req_t *req)
{
    if (req->content_len > 128)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
    char body[129] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read failed");
    body[n] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    HwFeatures next = s_features;
    cJSON *cam = cJSON_GetObjectItemCaseSensitive(root, "camera");
    cJSON *bat = cJSON_GetObjectItemCaseSensitive(root, "battery");
    cJSON *spk = cJSON_GetObjectItemCaseSensitive(root, "speaker");
    if (cJSON_IsBool(cam)) next.camera  = cJSON_IsTrue(cam);
    if (cJSON_IsBool(bat)) next.battery = cJSON_IsTrue(bat);
    if (cJSON_IsBool(spk)) next.speaker = cJSON_IsTrue(spk);
    cJSON_Delete(root);

    esp_err_t err = hw_features_save(&next);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    s_features = next;
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }, "restart_fw", 2048, nullptr, 5, nullptr);
    return httpd_resp_send(req, "Zapisano. Restart za chwile...", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /api/reboot
// ---------------------------------------------------------------------------
static esp_err_t reboot_device(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }, "web_reboot", 2048, nullptr, 5, nullptr);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Start serwera HTTP
// ---------------------------------------------------------------------------
esp_err_t web_config_start(const HaRuntimeConfig *initial_config)
{
    if (s_server) return ESP_OK;
    if (initial_config) s_config = *initial_config;
    hw_features_load(&s_features);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP start failed: %s", esp_err_to_name(err));
        return err;
    }

    auto reg = [&](const char *uri, httpd_method_t m,
                   esp_err_t (*handler)(httpd_req_t *)) {
        httpd_uri_t h = {.uri=uri,.method=m,.handler=handler,.user_ctx=nullptr};
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &h));
    };

    reg("/",                HTTP_GET,  send_index);
    reg("/api/ha",          HTTP_GET,  send_ha_config);
    reg("/api/ha",          HTTP_POST, save_ha_config);
    reg("/api/test",        HTTP_GET,  test_ha_connection);
    reg("/api/dashboard",   HTTP_GET,  get_dashboard);
    reg("/api/dashboard",   HTTP_POST, save_dashboard);
    reg("/api/features",    HTTP_GET,  send_features_config);
    reg("/api/features",    HTTP_POST, save_features_config);
    reg("/api/config",      HTTP_GET,  get_config);
    reg("/api/config",      HTTP_POST, post_config);
    reg("/api/reboot",      HTTP_POST, reboot_device);

    ESP_LOGI(TAG, "Web config started");
    return ESP_OK;
}
