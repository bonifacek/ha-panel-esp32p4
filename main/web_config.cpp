#include "web_config.h"
#include "ha_config.h"
#include "ha_entities.h"

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
    "<meta charset='utf-8'><title>HA Panel</title><style>"
    "body{font-family:Arial,sans-serif;margin:0;background:#eef3f6;color:#16232c}"
    ".wrap{max-width:1080px;margin:0 auto;padding:16px}"
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
    "button.del{background:#c0392b}"
    "button.sm{padding:5px 10px;font-size:12px}"
    ".toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:10px}"
    ".hint{font-size:12px;color:#5b6b75;margin-top:3px}"
    "pre{white-space:pre-wrap;background:#12202a;color:#e8f3f1;padding:12px;"
    "border-radius:6px;max-height:180px;overflow:auto;font-size:12px}"
    ".screen-list .item{display:flex;align-items:center;gap:8px;padding:8px 10px;"
    "border:1px solid #d8e1e7;border-radius:6px;margin-bottom:6px;background:#f8fbfc}"
    ".screen-list .item .name{flex:1;font-weight:600}"
    ".screen-list .item .def{font-size:11px;color:#0f6b63;margin-right:4px}"
    ".editor{background:#f0f6f8;border:1px solid #c5d5dc;border-radius:8px;"
    "padding:14px;margin-top:10px}"
    ".editor h4{margin:0 0 10px;font-size:15px}"
    ".tile-list .titem{border:1px solid #d0dde3;border-radius:6px;padding:10px;"
    "margin-bottom:8px;background:white}"
    ".tile-list .titem .th{display:flex;align-items:center;gap:6px}"
    ".tile-list .titem .th .tlbl{flex:1;font-weight:600;font-size:14px}"
    ".row-list .ritem{display:flex;align-items:center;gap:6px;margin:4px 0;"
    "font-size:13px;border:1px solid #e0e8ec;border-radius:5px;padding:6px 8px}"
    ".row-list .ritem .rid{flex:1;color:#234;font-family:monospace;font-size:12px}"
    "input.sm{padding:6px 8px;font-size:12px;width:auto}"
    ".search-res{max-height:200px;overflow-y:auto;border:1px solid #cdd8de;"
    "border-radius:6px;margin-top:6px}"
    ".search-res .ent{padding:7px 10px;cursor:pointer;font-size:13px;"
    "border-bottom:1px solid #eaf0f3}"
    ".search-res .ent:hover{background:#e2eff5}"
    ".search-res .ent .eid{font-family:monospace;font-size:11px;color:#557}"
    "</style></head><body><div class='wrap'>"
    "<h2>Home Assistant Panel</h2>";

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

// Sekcja dashboard — lista ekranow + edytor
static const char P3[] =
    "<section>"
    "<h3>Dashboard</h3>"
    "<div class='toolbar'>"
    "<button onclick='addScreen()'>+ Dodaj ekran</button>"
    "<button onclick='saveDashboard()'>Zapisz dashboard</button>"
    "<button onclick='exportDashboard()' class='sec'>&#11123; Eksportuj JSON</button>"
    "<button onclick='document.getElementById(\"importFile\").click()' class='sec'>&#11121; Importuj JSON</button>"
    "<input type='file' id='importFile' accept='.json' style='display:none' onchange='importDashboard(this)'>"
    "</div>"
    "<div class='screen-list' id='screenList'></div>"
    "<div id='screenEditor' style='display:none'></div>"
    "</section>";

// JavaScript — zarzadzanie dashboardem
static const char P4[] =
    "<script>"
    "let db={default_screen:0,screens:[]};"
    "let curS=-1,curT=-1;"
    "let haEnts=[];let filteredEnts=[];"

    "async function load(){"
    "try{let r=await fetch('/api/ha');let j=await r.json();"
    "document.getElementById('url').value=j.url||'';"
    "document.getElementById('token').value=j.token||'';}catch(e){}"
    "try{let r=await fetch('/api/dashboard');let j=await r.json();"
    "db=j;}catch(e){}"
    "renderScreens();}"

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
    "document.getElementById('haout').style.display='block';"
    "document.getElementById('haout').textContent='Restart...';}"

    "async function saveDashboard(){"
    "let r=await fetch('/api/dashboard',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(db)});"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent=await r.text();}"

    "function renderScreens(){"
    "let el=document.getElementById('screenList');el.innerHTML='';"
    "db.screens.forEach((s,i)=>{"
    "let d=document.createElement('div');d.className='item';"
    "let def=i===db.default_screen?'<span class=def>&#9733; domyslny</span>':'';"
    "d.innerHTML=def+'<span class=name>'+esc(s.name)+'</span>'"
    "+'<button class=sm onclick=editScreen('+i+')>Edytuj</button>'"
    "+'<button class=sm onclick=setDefault('+i+')>Domyslny</button>'"
    "+'<button class=\"sm del\" onclick=delScreen('+i+')>Usun</button>';"
    "el.appendChild(d);});}"

    "function addScreen(){"
    "db.screens.push({name:'Ekran '+(db.screens.length+1),tiles:[]});"
    "editScreen(db.screens.length-1);}"

    "function delScreen(i){"
    "if(!confirm('Usunac ekran?'))return;"
    "db.screens.splice(i,1);"
    "if(db.default_screen>=db.screens.length)"
    "db.default_screen=Math.max(0,db.screens.length-1);"
    "curS=-1;curT=-1;"
    "document.getElementById('screenEditor').style.display='none';"
    "renderScreens();}"

    "function setDefault(i){db.default_screen=i;renderScreens();}"

    "function editScreen(i){curS=i;curT=-1;renderScreens();renderScreenEditor();}"

    "function renderScreenEditor(){"
    "if(curS<0)return;"
    "let s=db.screens[curS];"
    "let el=document.getElementById('screenEditor');"
    "el.style.display='block';"
    "el.innerHTML='<div class=editor>'"
    "+'<h4>Ekran: <input id=sname type=text class=sm value=\"'+esc(s.name)+'\" onchange=\"db.screens[curS].name=this.value;renderScreens()\"> </h4>'"
    "+'<div class=toolbar><button onclick=addTile()>+ Dodaj kafelek</button></div>'"
    "+'<div class=\"tile-list\" id=tileList></div>'"
    "+'<div id=tileEditor></div></div>';"
    "renderTiles();}"

    "function renderTiles(){"
    "if(curS<0)return;"
    "let s=db.screens[curS];"
    "let el=document.getElementById('tileList');if(!el)return;el.innerHTML='';"
    "s.tiles.forEach((t,i)=>{"
    "let d=document.createElement('div');d.className='titem';"
    "let rows=t.rows.map(r=>'<span style=\"font-size:11px;color:#557;font-family:monospace\">'+esc(r.entity_id)+'</span>').join(' ');"
    "d.innerHTML='<div class=th><span class=tlbl>'+esc(t.label)+'</span>'"
    "+'<button class=sm onclick=editTile('+i+')>Edytuj</button>'"
    "+'<button class=\"sm del\" onclick=delTile('+i+')>Usun</button></div>'"
    "+'<div style=margin-top:4px>'+rows+'</div>';"
    "el.appendChild(d);});}"

    "function addTile(){"
    "if(curS<0)return;"
    "db.screens[curS].tiles.push({label:'Kafelek '+(db.screens[curS].tiles.length+1),rows:[]});"
    "editTile(db.screens[curS].tiles.length-1);}"

    "function delTile(i){"
    "if(!confirm('Usunac kafelek?'))return;"
    "db.screens[curS].tiles.splice(i,1);curT=-1;"
    "let te=document.getElementById('tileEditor');if(te)te.innerHTML='';"
    "renderTiles();}"

    "function editTile(i){curT=i;renderTiles();renderTileEditor();}"

    "function renderTileEditor(){"
    "if(curS<0||curT<0)return;"
    "let tile=db.screens[curS].tiles[curT];"
    "let el=document.getElementById('tileEditor');if(!el)return;"
    "let rowsHtml=tile.rows.map((r,i)=>"
    "'<div class=ritem>'"
    "+'<input class=sm type=text style=\"width:110px\" value=\"'+esc(r.label)+'\" onchange=\"db.screens[curS].tiles[curT].rows['+i+'].label=this.value\">'"
    "+'<span class=rid>'+esc(r.entity_id)+'</span>'"
    "+'<input class=sm type=text style=\"width:90px\" placeholder=atrybut value=\"'+esc(r.attribute)+'\" onchange=\"db.screens[curS].tiles[curT].rows['+i+'].attribute=this.value\">'"
    "+'<input class=sm type=text style=\"width:60px\" placeholder=jednostka value=\"'+esc(r.unit)+'\" onchange=\"db.screens[curS].tiles[curT].rows['+i+'].unit=this.value\">'"
    "+'<button class=\"sm del\" onclick=delRow('+i+')>x</button></div>'"
    ").join('');"
    "el.innerHTML='<div class=editor style=margin-top:8px>'"
    "+'<h4>Kafelek: <input id=tlbl type=text class=sm value=\"'+esc(tile.label)+'\" onchange=\"db.screens[curS].tiles[curT].label=this.value;renderTiles()\"></h4>'"
    "+'<b style=font-size:13px>Wiersze encji</b>'"
    "+'<div class=\"row-list\" id=rowList>'+rowsHtml+'</div>'"
    "+'<br><b style=font-size:13px>Dodaj wiersz</b>'"
    "+'<div class=toolbar style=margin-top:6px>'"
    "+'<input id=entSearch type=text class=sm placeholder=\"Szukaj encji\" style=\"width:260px\" oninput=searchEnts()>'"
    "+'<button class=sm onclick=fetchEnts()>Pobierz z HA</button></div>'"
    "+'<div class=\"search-res\" id=entResults></div></div>';"
    "renderEntResults();}"

    "function delRow(i){"
    "db.screens[curS].tiles[curT].rows.splice(i,1);renderTileEditor();}"

    "async function fetchEnts(){"
    "let sr=document.getElementById('entResults');if(sr)sr.innerHTML='Pobieranie...';"
    "try{let r=await fetch('/api/haentities');let j=await r.json();"
    "haEnts=j.entities||[];renderEntResults();}catch(e){"
    "if(sr)sr.innerHTML='Blad: '+e.message;}}"

    "function searchEnts(){renderEntResults();}"

    "function renderEntResults(){"
    "let sr=document.getElementById('entResults');if(!sr)return;"
    "let q=(document.getElementById('entSearch')||{value:''}).value.toLowerCase();"
    "let list=q?haEnts.filter(e=>e.entity_id.toLowerCase().includes(q)||"
    "(e.label||'').toLowerCase().includes(q)):haEnts;"
    "if(!list.length){sr.innerHTML='<p style=padding:8px;color:#888>Brak wynikow.</p>';return;}"
    "filteredEnts=list.slice(0,80);"
    "sr.innerHTML=filteredEnts.map((e,i)=>"
    "'<div class=ent onclick=\"addRowByIdx('+i+')\">'"
    "+'<b>'+esc(e.label||e.entity_id)+'</b> <span class=eid>'+esc(e.entity_id)+'</span></div>'"
    ").join('');}"

    "function addRowByIdx(i){if(i>=0&&i<filteredEnts.length)addRow(filteredEnts[i]);}"

    "function addRow(e){"
    "if(curS<0||curT<0)return;"
    "let tile=db.screens[curS].tiles[curT];"
    "if(tile.rows.length>=6)return alert('Max 6 wierszy w kafelku');"
    "tile.rows.push({entity_id:e.entity_id,label:e.label||e.entity_id,attribute:'',unit:''});"
    "renderTileEditor();}"

    "function esc(s){return String(s||'').replace(/[&<>\"]/g,"
    "m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[m]))}"

    // Eksport: pobiera JSON z urzadzenia i zapisuje jako plik
    "async function exportDashboard(){"
    "try{"
    "let r=await fetch('/api/dashboard');let txt=await r.text();"
    "let b=new Blob([txt],{type:'application/json'});"
    "let u=URL.createObjectURL(b);"
    "let a=document.createElement('a');a.href=u;a.download='ha-panel.json';"
    "document.body.appendChild(a);a.click();"
    "document.body.removeChild(a);URL.revokeObjectURL(u);"
    "}catch(e){alert('Blad eksportu: '+e.message);}}"

    // Import: wczytuje plik JSON i wysyla do urzadzenia (restart po zapisie)
    "async function importDashboard(inp){"
    "if(!inp.files||!inp.files[0])return;"
    "let txt=await inp.files[0].text();"
    "try{let j=JSON.parse(txt);"
    "if(!j.screens||!Array.isArray(j.screens))throw new Error('Brak pola screens');"
    "}catch(e){alert('Nieprawidlowy plik: '+e.message);inp.value='';return;}"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent='Importowanie...';"
    "try{"
    "let r=await fetch('/api/dashboard',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:txt});"
    "out.textContent=await r.text();"
    "}catch(e){out.textContent='Blad importu: '+e.message;}"
    "inp.value='';}"

    "load();"
    "</script></body></html>";

// ---------------------------------------------------------------------------
// Handlery HTTP
// ---------------------------------------------------------------------------
static esp_err_t send_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, P1);
    httpd_resp_sendstr_chunk(req, P2);
    httpd_resp_sendstr_chunk(req, P3);
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
// GET /api/haentities — pobiera encje z HA /api/states (scanner tekstowy)
// ---------------------------------------------------------------------------
static void scan_send_escaped(httpd_req_t *req, const char *text)
{
    for (const char *p = text; p && *p; ++p) {
        if (*p == '"') httpd_resp_sendstr_chunk(req, "\\\"");
        else if (*p == '\\') httpd_resp_sendstr_chunk(req, "\\\\");
        else if ((unsigned char)*p >= 0x20) {
            char tmp[2] = {*p, '\0'};
            httpd_resp_sendstr_chunk(req, tmp);
        }
    }
}

static esp_err_t fetch_ha_entities(httpd_req_t *req)
{
    if (!ha_config_ready(&s_config))
        return httpd_resp_send(req, "{\"entities\":[]}", HTTPD_RESP_USE_STRLEN);

    char http_url[160] = {};
    if (strncmp(s_config.url, "ws://", 5) == 0)
        snprintf(http_url, sizeof(http_url), "http://%s", s_config.url + 5);
    else if (strncmp(s_config.url, "wss://", 6) == 0)
        snprintf(http_url, sizeof(http_url), "https://%s", s_config.url + 6);
    char *trim = strstr(http_url, "/api/websocket");
    if (trim) *trim = '\0';
    strlcat(http_url, "/api/states", sizeof(http_url));

    char auth_header[280] = {};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_config.token);

    const size_t buf_size = 4 * 1024 * 1024;
    char *buf = static_cast<char *>(
        heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No PSRAM");

    esp_http_client_config_t cfg = {};
    cfg.url               = http_url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 15000;
    cfg.buffer_size       = 4096;
    cfg.buffer_size_tx    = 512;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        esp_http_client_cleanup(client);
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HA HTTP error");
    }

    int total = 0, rd;
    while ((rd = esp_http_client_read(client, buf + total,
                                      (int)(buf_size - total - 1))) > 0)
        total += rd;
    buf[total] = '\0';
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "HA /api/states: %d B", total);

    static const char EID_KEY[] = "\"entity_id\":\"";
    static const char FN_KEY[]  = "\"friendly_name\":\"";
    const size_t EID_LEN = sizeof(EID_KEY) - 1;
    const size_t FN_LEN  = sizeof(FN_KEY)  - 1;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"entities\":[");

    bool first = true;
    char *pos = buf;

    while (true) {
        char *eid_key = strstr(pos, EID_KEY);
        if (!eid_key) break;
        char *eid_val = eid_key + EID_LEN;
        char *eid_end = strchr(eid_val, '"');
        if (!eid_end) break;

        const size_t eid_len = eid_end - eid_val;
        if (eid_len == 0 || eid_len >= 64) { pos = eid_end + 1; continue; }

        char entity_id[64] = {};
        memcpy(entity_id, eid_val, eid_len);

        char domain[32] = {};
        const char *dot = strchr(entity_id, '.');
        if (dot) {
            const size_t dlen = dot - entity_id;
            if (dlen < sizeof(domain)) memcpy(domain, entity_id, dlen);
        }

        char *next_eid = strstr(eid_end + 1, EID_KEY);
        char *bound    = next_eid ? next_eid : (buf + total);

        char label[128] = {};
        strlcpy(label, entity_id, sizeof(label));

        char saved = *bound;
        *bound = '\0';
        char *fn_key = strstr(eid_end, FN_KEY);
        *bound = saved;

        if (fn_key && fn_key < bound) {
            char *fn_val = fn_key + FN_LEN;
            size_t i = 0;
            while (fn_val < bound && *fn_val != '"' && i < sizeof(label) - 1) {
                if (*fn_val == '\\' && fn_val + 1 < bound) {
                    fn_val++;
                    label[i++] = *fn_val++;
                } else {
                    label[i++] = *fn_val++;
                }
            }
            label[i] = '\0';
        }

        if (!first) httpd_resp_sendstr_chunk(req, ",");
        first = false;

        char tmp[96] = {};
        snprintf(tmp, sizeof(tmp), "{\"entity_id\":\"%s\",\"label\":\"", entity_id);
        httpd_resp_sendstr_chunk(req, tmp);
        scan_send_escaped(req, label);
        snprintf(tmp, sizeof(tmp), "\",\"domain\":\"%s\"}", domain);
        httpd_resp_sendstr_chunk(req, tmp);

        pos = eid_end + 1;
    }

    free(buf);
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
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
    reg("/api/haentities",  HTTP_GET,  fetch_ha_entities);
    reg("/api/dashboard",   HTTP_GET,  get_dashboard);
    reg("/api/dashboard",   HTTP_POST, save_dashboard);
    reg("/api/reboot",      HTTP_POST, reboot_device);

    ESP_LOGI(TAG, "Web config started");
    return ESP_OK;
}
