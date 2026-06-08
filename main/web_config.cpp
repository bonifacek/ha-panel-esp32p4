#include "web_config.h"
#include "ha_config.h"
#include "ha_entities.h"
#include "features_config.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "speaker_sched.h"
#include "tts_player.h"
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
    "<meta charset='utf-8'><title>HA Panel &mdash; Settings</title><style>"
    "body{font-family:Arial,sans-serif;margin:0;background:#eef3f6;color:#16232c}"
    ".wrap{max-width:600px;margin:0 auto;padding:16px}"
    "h2{margin:0}"
    ".lang-bar{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}"
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
    ".dbtn{padding:2px 5px;border:1px solid #b8c5cc;border-radius:3px;cursor:pointer;"
    "background:#f0f5f7;font-size:11px;margin:1px;line-height:1.6}"
    ".dbtn:hover{background:#d0dde4}"
    ".dsel{background:#0f6b63;color:white;border-color:#0f6b63}"
    ".stbl{width:100%;border-collapse:collapse;font-size:12px}"
    ".stbl th{padding:6px 4px;background:#f0f5f7;font-weight:700;white-space:nowrap}"
    ".stbl td{padding:5px 4px;border-top:1px solid #eef3f6;vertical-align:middle}"
    "input[type=time]{padding:4px;border:1px solid #b8c5cc;border-radius:4px;font-size:12px;width:88px}"
    "input[type=range]{vertical-align:middle}"
    "#lang-btn{background:#5b6b75;padding:5px 11px;font-size:12px;margin:0}"
    "</style></head><body><div class='wrap'>"
    "<div class='lang-bar'>"
    "<h2 data-i18n='heading'>HA Panel &mdash; Settings</h2>"
    "<button id='lang-btn' onclick='toggleLang()'>PL</button>"
    "</div>";

static const char P2[] =
    "<section><h3 data-i18n='sec_ha'>Home Assistant Connection</h3>"
    "<label>WebSocket URL</label>"
    "<input id='url' type='text' placeholder='ws://192.168.1.100:8123/api/websocket'>"
    "<p class='hint' data-i18n-html='ha_url_hint'>HTTP: ws://IP:8123/api/websocket &nbsp;|&nbsp; HTTPS: wss://domain/api/websocket</p>"
    "<label>Long-Lived Access Token</label>"
    "<input id='token' type='password' placeholder='eyJ0eXAiOiJKV1Qi...'>"
    "<p class='hint' data-i18n='ha_token_hint'>HA &rarr; Profile &rarr; Tokens &rarr; Create token</p>"
    "<div class='toolbar'>"
    "<button onclick='saveHa()' data-i18n='btn_save_ha'>Save connection</button>"
    "<button onclick='testConn()' class='sec' data-i18n='btn_test'>Test</button>"
    "<button onclick='reboot()' class='sec' data-i18n='btn_restart'>Restart</button>"
    "</div>"
    "<pre id='haout' style='display:none'></pre>"
    "</section>";

// Sekcja konfiguracji sprzetu
static const char P_FEATURES[] =
    "<section><h3 data-i18n='sec_hw'>Hardware</h3>"
    "<p class='hint' data-i18n='hw_hint'>Disable module if board does not have this component. Change is saved to memory and requires restart.</p>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_camera' style='width:18px;height:18px'>"
    "<span data-i18n='hw_camera'>Camera (motion detection, screen wake)</span></label>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_battery' style='width:18px;height:18px'>"
    "<span data-i18n='hw_battery'>Battery (ADC voltage monitor)</span></label>"
    "<label style='margin:8px 0;display:flex;align-items:center;gap:10px;cursor:pointer'>"
    "<input type='checkbox' id='feat_speaker' style='width:18px;height:18px'>"
    "<span data-i18n='hw_speaker'>Speaker ES8311 (touch beep, HA call bell)</span></label>"
    "<div class='toolbar' style='margin-top:10px'>"
    "<button onclick='saveFeatures()' data-i18n='btn_save_restart'>Save and restart</button>"
    "</div>"
    "<pre id='featout' style='display:none'></pre>"
    "</section>";

// Sekcja eksport / import konfiguracji urzadzenia
static const char P_CFGIO[] =
    "<section><h3 data-i18n='sec_backup'>Configuration backup</h3>"
    "<p class='hint' data-i18n='backup_hint'>Exports HA connection and hardware settings to JSON file. Useful when replacing device or updating firmware.</p>"
    "<div class='toolbar'>"
    "<button onclick='exportCfg()' data-i18n-html='btn_export'>&#11123; Export</button>"
    "<button onclick='document.getElementById(\"cfgFile\").click()' class='sec' data-i18n-html='btn_import'>&#11121; Import</button>"
    "<input type='file' id='cfgFile' accept='.json' style='display:none' onchange='importCfg(this)'>"
    "</div>"
    "<pre id='cfgout' style='display:none'></pre>"
    "</section>";

// Sekcja TTS
static const char P_TTS[] =
    "<section>"
    "<h3 data-i18n='sec_tts'>TTS &mdash; Text to speech</h3>"
    "<p class='hint' data-i18n-html='tts_hint'>"
    "TTS engine required in Home Assistant (recommended: <b>Piper</b> &mdash; local, free). "
    "HA Automation &rarr; <code>rest_command.panel_speak</code> &rarr; panel plays voice."
    "</p>"
    "<label data-i18n='tts_engine_label'>TTS engine (engine_id)</label>"
    "<input id='tts-eng' type='text' placeholder='tts.piper'>"
    "<p class='hint' data-i18n-html='tts_engine_hint'>Check in HA: Developer Tools &rarr; Template &rarr; type <code>{{ states.tts }}</code> &nbsp;|&nbsp; usually: <b>tts.piper</b></p>"
    "<label data-i18n-html='tts_lang_label'>Language <small style='color:#5b6b75'>(Piper: underscore, e.g. en_US)</small></label>"
    "<input id='tts-lang' type='text' placeholder='en_US' style='max-width:160px'>"
    "<label data-i18n='tts_voice_label'>Voice (required for Piper)</label>"
    "<input id='tts-voice' type='text' placeholder='en_US-ryan-medium'>"
    "<p class='hint' data-i18n-html='tts_voice_hint'>"
    "Piper: HA &rarr; Settings &rarr; Voice &rarr; click Piper engine &rarr; voice list.<br>"
    "EN examples: <b>en_US-ryan-medium</b> &nbsp;|&nbsp; <b>en_GB-alan-medium</b><br>"
    "<b>Note:</b> Piper uses underscore in language (en_US), not dash (en-US)!"
    "</p>"
    "<div class='toolbar'>"
    "<button onclick='saveTtsCfg()' data-i18n='btn_save_tts'>Save configuration</button>"
    "</div>"
    "<label style='margin-top:12px' data-i18n='tts_test_label'>TTS test</label>"
    "<input id='tts-txt' type='text' data-i18n-ph='tts_test_ph' placeholder='Test voice message'>"
    "<div class='toolbar'>"
    "<button onclick='testTts()' class='sec' data-i18n='btn_play_test'>&#9654; Play test</button>"
    "</div>"
    "<pre id='ttsout' style='display:none'></pre>"
    "</section>";

// Sekcja harmonogramu głośności
static const char P_SCHED[] =
    "<section>"
    "<h3 data-i18n='sec_sched'>Speaker volume schedule</h3>"
    "<p class='hint' data-i18n='sched_hint'>Time windows in which the speaker operates at selected volume. "
    "Volume 0% mutes sounds in that slot. "
    "Slots checked top-down — first match wins. "
    "Requires speaker enabled and NTP sync.</p>"
    "<label data-i18n='sched_default_vol'>Default volume (outside schedule):</label>"
    "<div style='display:flex;align-items:center;gap:10px;margin:8px 0 14px'>"
    "<input type='range' id='sched-dv' min='0' max='100' value='70' style='flex:1;max-width:220px'"
    " oninput='document.getElementById(\"sdv-lbl\").textContent=this.value+\"%\"'>"
    "<span id='sdv-lbl' style='min-width:36px;font-weight:700'>70%</span>"
    "</div>"
    "<div style='overflow-x:auto'>"
    "<table class='stbl' id='sched-tbl'>"
    "<thead><tr>"
    "<th data-i18n='sched_col_active'>Active</th>"
    "<th data-i18n='sched_col_days'>Days</th>"
    "<th data-i18n='sched_col_from'>From</th>"
    "<th data-i18n='sched_col_to'>To</th>"
    "<th data-i18n='sched_col_vol'>Volume</th>"
    "<th></th></tr></thead>"
    "<tbody id='sched-body'></tbody>"
    "</table>"
    "</div>"
    "<div class='toolbar' style='margin-top:10px'>"
    "<button onclick='addSlot()' data-i18n='btn_add_slot'>+ Add slot</button>"
    "<button id='btn-ss' onclick='saveSched()' data-i18n='btn_save_sched'>Save schedule</button>"
    "</div>"
    "<pre id='sched-out' style='display:none'></pre>"
    "</section>";

// JavaScript — ustawienia urządzenia + system i18n (EN/PL)
static const char P4[] =
    "<script>"
    // ── i18n ──────────────────────────────────────────────────────────────
    "var _L=localStorage.getItem('lang')||'en';"
    "document.getElementById('lang-btn').textContent=_L==='en'?'PL':'EN';"
    "var T={"
      "en:{"
        "title:'HA Panel — Settings',"
        "heading:'HA Panel — Settings',"
        "sec_ha:'Home Assistant Connection',"
        "ha_url_hint:'HTTP: ws://IP:8123/api/websocket &nbsp;|&nbsp; HTTPS: wss://domain/api/websocket',"
        "ha_token_hint:'HA → Profile → Tokens → Create token',"
        "btn_save_ha:'Save connection',"
        "btn_test:'Test',"
        "btn_restart:'Restart',"
        "sec_hw:'Hardware',"
        "hw_hint:'Disable module if board does not have this component. Change is saved to memory and requires restart.',"
        "hw_camera:'Camera (motion detection, screen wake)',"
        "hw_battery:'Battery (ADC voltage monitor)',"
        "hw_speaker:'Speaker ES8311 (touch beep, HA call bell)',"
        "btn_save_restart:'Save and restart',"
        "sec_backup:'Configuration backup',"
        "backup_hint:'Exports HA connection and hardware settings to JSON file. Useful when replacing device or updating firmware.',"
        "btn_export:'&#11123; Export',"
        "btn_import:'&#11121; Import',"
        "sec_tts:'TTS — Text to speech',"
        "tts_hint:'TTS engine required in Home Assistant (recommended: <b>Piper</b> — local, free). HA Automation → <code>rest_command.panel_speak</code> → panel plays voice.',"
        "tts_engine_label:'TTS engine (engine_id)',"
        "tts_engine_hint:'Check in HA: Developer Tools → Template → type <code>{{ states.tts }}</code> &nbsp;|&nbsp; usually: <b>tts.piper</b>',"
        "tts_lang_label:'Language <small style=\"color:#5b6b75\">(Piper: underscore, e.g. en_US)</small>',"
        "tts_voice_label:'Voice (required for Piper)',"
        "tts_voice_hint:'Piper: HA → Settings → Voice → click Piper engine → voice list.<br>EN examples: <b>en_US-ryan-medium</b> &nbsp;|&nbsp; <b>en_GB-alan-medium</b><br><b>Note:</b> Piper uses underscore in language (en_US), not dash (en-US)!',"
        "btn_save_tts:'Save configuration',"
        "tts_test_label:'TTS test',"
        "tts_test_ph:'Test voice message',"
        "btn_play_test:'▶ Play test',"
        "sec_sched:'Speaker volume schedule',"
        "sched_hint:'Time windows in which the speaker operates at selected volume. Volume 0% mutes sounds in that slot. Slots checked top-down — first match wins. Requires speaker enabled and NTP sync.',"
        "sched_default_vol:'Default volume (outside schedule):',"
        "sched_col_active:'Active',"
        "sched_col_days:'Days',"
        "sched_col_from:'From',"
        "sched_col_to:'To',"
        "sched_col_vol:'Volume',"
        "btn_add_slot:'+ Add slot',"
        "btn_save_sched:'Save schedule',"
        "js_testing:'Testing...',"
        "js_restart:'Restarting...',"
        "js_export_err:'Export error: ',"
        "js_invalid_file:'Invalid file: ',"
        "js_importing:'Importing...',"
        "js_error:'Error: ',"
        "js_sending:'Sending...',"
        "js_enter_test:'Enter test text',"
        "js_max_slots:'Maximum 8 slots',"
        "js_saving:'Saving...',"
        "days:['Mo','Tu','We','Th','Fr','Sa','Su']"
      "},"
      "pl:{"
        "title:'HA Panel — Ustawienia',"
        "heading:'HA Panel — Ustawienia',"
        "sec_ha:'Połączenie z Home Assistant',"
        "ha_url_hint:'HTTP: ws://IP:8123/api/websocket &nbsp;|&nbsp; HTTPS: wss://domena/api/websocket',"
        "ha_token_hint:'HA → Profil → Tokeny → Utwórz token',"
        "btn_save_ha:'Zapisz połączenie',"
        "btn_test:'Test',"
        "btn_restart:'Restart',"
        "sec_hw:'Sprzęt',"
        "hw_hint:'Wyłącz moduł jeśli płytka nie posiada danego podzespołu. Zmiana zapisuje się w pamięci i wymaga restartu.',"
        "hw_camera:'Kamera (detekcja ruchu, budzenie ekranu)',"
        "hw_battery:'Bateria (monitor napięcia ADC)',"
        "hw_speaker:'Głośnik ES8311 (pisk dotyku, dzwon połączenia HA)',"
        "btn_save_restart:'Zapisz i restartuj',"
        "sec_backup:'Kopia konfiguracji',"
        "backup_hint:'Eksportuje połączenie HA i ustawienia sprzętu do pliku JSON. Przydatne przy wymianie urządzenia lub aktualizacji firmware.',"
        "btn_export:'&#11123; Eksportuj',"
        "btn_import:'&#11121; Importuj',"
        "sec_tts:'TTS — Zamiana tekstu na mowę',"
        "tts_hint:'Wymagany silnik TTS w Home Assistant (zalecany: <b>Piper</b> — lokalny, PL, bezpłatny). HA Automation → <code>rest_command.panel_speak</code> → panel odtwarza głos.',"
        "tts_engine_label:'Silnik TTS (engine_id)',"
        "tts_engine_hint:'Sprawdź w HA: Developer Tools → Template → wpisz <code>{{ states.tts }}</code> &nbsp;|&nbsp; zwykle: <b>tts.piper</b>',"
        "tts_lang_label:'Język <small style=\"color:#5b6b75\">(Piper: podkreślnik, np. pl_PL)</small>',"
        "tts_voice_label:'Głos (wymagany dla Piper)',"
        "tts_voice_hint:'Piper: HA → Ustawienia → Głos → kliknij silnik Piper → lista głosów.<br>Przykłady PL: <b>pl_PL-gosia-medium</b> &nbsp;|&nbsp; <b>pl_PL-darkman-medium</b><br><b>Uwaga:</b> Piper używa podkreślnika w języku (pl_PL), nie myślnika (pl-PL)!',"
        "btn_save_tts:'Zapisz konfigurację',"
        "tts_test_label:'Test TTS',"
        "tts_test_ph:'Testowy komunikat głosowy',"
        "btn_play_test:'▶ Odtwórz test',"
        "sec_sched:'Harmonogram głośności głośnika',"
        "sched_hint:'Okna czasowe w których głośnik ma działać z wybraną głośnością. Głośność 0% wycisza dźwięki w danym przedziale. Sloty sprawdzane od góry — wygrywa pierwszy pasujący. Wymaga włączonego głośnika i synchronizacji NTP.',"
        "sched_default_vol:'Głośność domyślna (poza harmonogramem):',"
        "sched_col_active:'Aktywny',"
        "sched_col_days:'Dni tygodnia',"
        "sched_col_from:'Od',"
        "sched_col_to:'Do',"
        "sched_col_vol:'Głośność',"
        "btn_add_slot:'+ Dodaj slot',"
        "btn_save_sched:'Zapisz harmonogram',"
        "js_testing:'Testowanie...',"
        "js_restart:'Restart...',"
        "js_export_err:'Błąd eksportu: ',"
        "js_invalid_file:'Nieprawidłowy plik: ',"
        "js_importing:'Importowanie...',"
        "js_error:'Błąd: ',"
        "js_sending:'Wysyłanie...',"
        "js_enter_test:'Wpisz tekst testu',"
        "js_max_slots:'Maksymalnie 8 slotów',"
        "js_saving:'Zapisywanie...',"
        "days:['Pn','Wt','Śr','Cz','Pt','Sb','Nd']"
      "}"
    "};"

    "function applyLang(){"
    "document.title=T[_L].title;"
    "document.querySelectorAll('[data-i18n]').forEach(function(el){"
    "var k=el.getAttribute('data-i18n');if(T[_L][k]!==undefined)el.textContent=T[_L][k];});"
    "document.querySelectorAll('[data-i18n-html]').forEach(function(el){"
    "var k=el.getAttribute('data-i18n-html');if(T[_L][k]!==undefined)el.innerHTML=T[_L][k];});"
    "document.querySelectorAll('[data-i18n-ph]').forEach(function(el){"
    "var k=el.getAttribute('data-i18n-ph');if(T[_L][k]!==undefined)el.placeholder=T[_L][k];});"
    "_DN=T[_L].days;}"

    "function toggleLang(){"
    "_L=_L==='en'?'pl':'en';"
    "localStorage.setItem('lang',_L);"
    "document.getElementById('lang-btn').textContent=_L==='en'?'PL':'EN';"
    "applyLang();rSched();}"
    // ── koniec i18n ───────────────────────────────────────────────────────

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
    "out.style.display='block';out.textContent=T[_L].js_testing;"
    "let r=await fetch('/api/test');out.textContent=await r.text();}"

    "async function reboot(){"
    "await fetch('/api/reboot',{method:'POST'});"
    "let out=document.getElementById('haout');"
    "out.style.display='block';out.textContent=T[_L].js_restart;}"

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
    "}catch(e){alert(T[_L].js_export_err+e.message);}}"

    "async function importCfg(inp){"
    "if(!inp.files||!inp.files[0])return;"
    "let txt=await inp.files[0].text();"
    "try{JSON.parse(txt);}catch(e){alert(T[_L].js_invalid_file+e.message);inp.value='';return;}"
    "let out=document.getElementById('cfgout');"
    "out.style.display='block';out.textContent=T[_L].js_importing;"
    "try{"
    "let r=await fetch('/api/config',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:txt});"
    "out.textContent=await r.text();"
    "}catch(e){out.textContent=T[_L].js_error+e.message;}"
    "inp.value='';}"

    // ── TTS ───────────────────────────────────────────────────────────────
    "async function loadTtsCfg(){"
    "try{var r=await fetch('/api/tts_cfg');var j=await r.json();"
    "document.getElementById('tts-eng').value=j.engine||'';"
    "document.getElementById('tts-lang').value=j.lang||'';"
    "document.getElementById('tts-voice').value=j.voice||'';}catch(e){}}"

    "async function saveTtsCfg(){"
    "var b={engine:document.getElementById('tts-eng').value,"
    "lang:document.getElementById('tts-lang').value,"
    "voice:document.getElementById('tts-voice').value};"
    "var out=document.getElementById('ttsout');"
    "out.style.display='block';"
    "try{var r=await fetch('/api/tts_cfg',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "out.textContent=await r.text();}catch(e){out.textContent=T[_L].js_error+e.message;}}"

    "async function testTts(){"
    "var txt=document.getElementById('tts-txt').value;"
    "if(!txt){alert(T[_L].js_enter_test);return;}"
    "var out=document.getElementById('ttsout');"
    "out.style.display='block';out.textContent=T[_L].js_sending;"
    "try{var r=await fetch('/api/speak',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify({text:txt})});"
    "out.textContent=await r.text();}catch(e){out.textContent=T[_L].js_error+e.message;}}"
    // ── koniec TTS ────────────────────────────────────────────────────────

    // ── Harmonogram głośności ──────────────────────────────────────────────
    "var _sc={dv:70,slots:[]};"

    "async function loadSched(){"
    "try{var r=await fetch('/api/spk_sched');_sc=await r.json();"
    "if(!Array.isArray(_sc.slots))_sc.slots=[];}catch(e){}"
    "rSched();}"

    "var _DN=T[_L].days;"

    "function rSched(){"
    "var dv=_sc.dv||70;"
    "document.getElementById('sched-dv').value=dv;"
    "document.getElementById('sdv-lbl').textContent=dv+'%';"
    "var html='';"
    "(_sc.slots||[]).forEach(function(s,i){"
    "var days='';"
    "for(var j=0;j<7;j++){"
    "var on=(s.d||0)&(1<<j);"
    "days+='<button class=\"dbtn'+(on?' dsel':'')+'\" onclick=\"tday('+i+','+j+')\">'+_DN[j]+'</button>';}"
    "var p=function(n){return('0'+n).slice(-2);};"
    "var v=s.v||0;"
    "html+='<tr>'+"
    "'<td style=\"text-align:center\"><input type=checkbox '+(s.a?'checked':'')+' onchange=\"_sc.slots['+i+'].a=this.checked?1:0\"></td>'+"
    "'<td style=\"white-space:nowrap\">'+days+'</td>'+"
    "'<td><input type=time value=\"'+p(s.hf||0)+':'+p(s.mf||0)+'\" onchange=\"stm('+i+',0,this.value)\"></td>'+"
    "'<td><input type=time value=\"'+p(s.ht||0)+':'+p(s.mt||0)+'\" onchange=\"stm('+i+',1,this.value)\"></td>'+"
    "'<td style=\"white-space:nowrap\">'+"
    "'<input type=range min=0 max=100 value='+v+' data-i='+i+' oninput=\"svol(this)\" style=\"width:70px\">'+"
    "'<span id=\"sv'+i+'\" style=\"min-width:34px;display:inline-block\"> '+v+'%</span></td>'+"
    "'<td><button class=sec style=\"padding:3px 8px\" onclick=\"dslot('+i+')\">&#10005;</button></td>'+"
    "'</tr>';"
    "});"
    "document.getElementById('sched-body').innerHTML=html;}"

    "function svol(el){"
    "var i=+el.dataset.i;"
    "_sc.slots[i].v=+el.value;"
    "var sp=document.getElementById('sv'+i);"
    "if(sp)sp.textContent=' '+el.value+'%';}"

    "function tday(i,b){"
    "_sc.slots[i].d=(_sc.slots[i].d||0)^(1<<b);"
    "rSched();}"

    "function stm(i,p,v){"
    "var a=v.split(':').map(Number);"
    "if(p===0){_sc.slots[i].hf=a[0];_sc.slots[i].mf=a[1];}"
    "else{_sc.slots[i].ht=a[0];_sc.slots[i].mt=a[1];}}"

    "function addSlot(){"
    "if(!_sc.slots)_sc.slots=[];"
    "if(_sc.slots.length>=8){alert(T[_L].js_max_slots);return;}"
    "_sc.slots.push({a:1,d:127,hf:22,mf:0,ht:7,mt:0,v:0});"
    "rSched();}"

    "function dslot(i){"
    "_sc.slots.splice(i,1);"
    "rSched();}"

    "async function saveSched(){"
    "_sc.dv=+document.getElementById('sched-dv').value;"
    "var out=document.getElementById('sched-out');"
    "out.style.display='block';out.textContent=T[_L].js_saving;"
    "var btn=document.getElementById('btn-ss');"
    "btn.disabled=true;"
    "try{"
    "var r=await fetch('/api/spk_sched',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(_sc)});"
    "out.textContent=await r.text();"
    "}catch(e){out.textContent=T[_L].js_error+e.message;}"
    "btn.disabled=false;}"
    // ── koniec schedulera ──────────────────────────────────────────────────

    "load();loadFeatures();loadSched();loadTtsCfg();applyLang();"
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
    httpd_resp_sendstr_chunk(req, P_TTS);
    httpd_resp_sendstr_chunk(req, P_SCHED);
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
    return httpd_resp_send(req, "Saved. Restarting soon...", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t test_ha_connection(httpd_req_t *req)
{
    if (!ha_config_ready(&s_config))
        return httpd_resp_send(req, "No HA configuration.", HTTPD_RESP_USE_STRLEN);

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
        snprintf(result, sizeof(result), "Error: %s", esp_err_to_name(err));
    else if (status == 200 || status == 201)
        snprintf(result, sizeof(result), "OK! HA responded with code %d", status);
    else if (status == 401)
        snprintf(result, sizeof(result), "Error 401 – bad token");
    else
        snprintf(result, sizeof(result), "HA responded with code %d", status);

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

    char *body = static_cast<char *>(
        heap_caps_calloc(1, (size_t)file_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) body = static_cast<char *>(calloc(1, (size_t)file_size + 1));
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
    if (req->content_len > 512 * 1024)   // 512 KB — spójne z limitem odczytu w ha_dashboard_load
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
    char *body = static_cast<char *>(
        heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) body = static_cast<char *>(calloc(1, req->content_len + 1));
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
    return httpd_resp_send(req, "Saved. Restarting soon...", HTTPD_RESP_USE_STRLEN);
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
    return httpd_resp_send(req, "Imported. Restarting soon...", HTTPD_RESP_USE_STRLEN);
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
    return httpd_resp_send(req, "Saved. Restarting soon...", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/tts_cfg — zwraca silnik i jezyk TTS
// ---------------------------------------------------------------------------
static esp_err_t get_tts_cfg(httpd_req_t *req)
{
    char eng[64]   = {};
    char lang[16]  = {};
    char voice[64] = {};
    tts_get_engine(eng, sizeof(eng), lang, sizeof(lang), voice, sizeof(voice));

    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"engine\":\"%s\",\"lang\":\"%s\",\"voice\":\"%s\"}",
             eng, lang, voice);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /api/tts_cfg — zapisuje silnik i jezyk TTS
// ---------------------------------------------------------------------------
static esp_err_t save_tts_cfg(httpd_req_t *req)
{
    if (req->content_len > 256)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too large");

    char body[257] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read error");
    body[n] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");

    char eng[64]   = {};
    char lang[16]  = {};
    char voice[64] = {};
    cJSON *je = cJSON_GetObjectItemCaseSensitive(root, "engine");
    cJSON *jl = cJSON_GetObjectItemCaseSensitive(root, "lang");
    cJSON *jv = cJSON_GetObjectItemCaseSensitive(root, "voice");
    if (cJSON_IsString(je)) strlcpy(eng,   je->valuestring, sizeof(eng));
    if (cJSON_IsString(jl)) strlcpy(lang,  jl->valuestring, sizeof(lang));
    if (cJSON_IsString(jv)) strlcpy(voice, jv->valuestring, sizeof(voice));
    cJSON_Delete(root);

    esp_err_t err = tts_set_engine(eng, lang, voice);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");

    return httpd_resp_send(req, "TTS saved", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /api/speak — przyjmuje tekst lub URL i kolejkuje do TTS
// Body: {"text":"..."} lub {"url":"http://..."}
// Zwraca natychmiast — odtwarzanie dzieje sie asynchronicznie
// ---------------------------------------------------------------------------
static esp_err_t handle_speak(httpd_req_t *req)
{
    if (req->content_len > 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too large");

    char body[513] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read error");
    body[n] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");

    esp_err_t err = ESP_FAIL;
    cJSON *jurl  = cJSON_GetObjectItemCaseSensitive(root, "url");
    cJSON *jtext = cJSON_GetObjectItemCaseSensitive(root, "text");

    if (cJSON_IsString(jurl) && jurl->valuestring[0]) {
        err = tts_play_url(jurl->valuestring);
    } else if (cJSON_IsString(jtext) && jtext->valuestring[0]) {
        err = tts_speak(jtext->valuestring);
    }
    cJSON_Delete(root);

    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "No text/URL or queue full");

    return httpd_resp_send(req, "Playing...", HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// GET /api/spk_sched — zwraca JSON harmonogramu głośności
// ---------------------------------------------------------------------------
static esp_err_t get_spk_sched(httpd_req_t *req)
{
    char *json = sched_to_json();
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/spk_sched — zapisuje harmonogram z JSON (bez restartu)
// ---------------------------------------------------------------------------
static esp_err_t save_spk_sched(httpd_req_t *req)
{
    if (req->content_len > 1024)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");

    char body[1025] = {};
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read failed");
    body[n] = '\0';

    // Walidacja JSON
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    cJSON_Delete(root);

    esp_err_t err = sched_save_json(body);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");

    return httpd_resp_send(req, "Schedule saved", HTTPD_RESP_USE_STRLEN);
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
    config.max_uri_handlers = 18;
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
    reg("/api/spk_sched",  HTTP_GET,  get_spk_sched);
    reg("/api/spk_sched",  HTTP_POST, save_spk_sched);
    reg("/api/tts_cfg",    HTTP_GET,  get_tts_cfg);
    reg("/api/tts_cfg",    HTTP_POST, save_tts_cfg);
    reg("/api/speak",      HTTP_POST, handle_speak);

    ESP_LOGI(TAG, "Web config started");
    return ESP_OK;
}
