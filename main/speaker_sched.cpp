#include "speaker_sched.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

#include <string.h>
#include <time.h>

static const char *TAG = "spk_sched";
static const char *kNs  = "spk_sched";
static const char *kKey = "cfg";

// Wewnętrzna kopia używana przez sched_get_volume()
// Wartości domyślne ustawia sched_load() — wywoływane przed startem speakera
static SpeakerSchedule s_sched = {};

// ---------------------------------------------------------------------------
// Pomocnicze: czy bieżąca chwila mieści się w slocie?
// ---------------------------------------------------------------------------
static bool slot_matches(const VolumeSlot &sl,
                          int h, int m, int tm_wday)
{
    // Konwersja tm_wday (0=Nd,1=Pn,...,6=Sb) → nasz bitmask bit0=Pn..bit6=Nd
    auto to_bit = [](int w) -> uint8_t {
        return (w == 0) ? 0x40u : (uint8_t)(1u << (w - 1));
    };

    const uint8_t today    = to_bit(tm_wday);
    const uint8_t prev_day = to_bit((tm_wday + 6) % 7);

    const int from_m = sl.hour_from * 60 + sl.min_from;
    const int to_m   = sl.hour_to   * 60 + sl.min_to;
    const int now_m  = h * 60 + m;

    if (from_m <= to_m) {
        // Normalny przedział (np. 08:00–22:00)
        return (sl.days_mask & today) && now_m >= from_m && now_m < to_m;
    } else {
        // Przedział nocny przekraczający północ (np. 22:00–07:00)
        if (now_m >= from_m)
            return !!(sl.days_mask & today);    // np. 23:00 w dniu początkowym
        else
            return !!(sl.days_mask & prev_day); // np. 02:00 — noc zaczęła się wczoraj
    }
}

// ---------------------------------------------------------------------------
// Parsowanie JSON → SpeakerSchedule
// Format: { "dv": 70, "slots": [{ "a":1,"d":127,"hf":22,"mf":0,"ht":7,"mt":0,"v":0 }, ...] }
// ---------------------------------------------------------------------------
static void parse_json_to_sched(const char *json, SpeakerSchedule *s)
{
    s->default_volume = 70;
    s->slot_count     = 0;
    memset(s->slots, 0, sizeof(s->slots));

    cJSON *root = cJSON_Parse(json);
    if (!root) { ESP_LOGW(TAG, "parse_json_to_sched: blad JSON"); return; }

    cJSON *dv = cJSON_GetObjectItemCaseSensitive(root, "dv");
    if (cJSON_IsNumber(dv)) s->default_volume = (uint8_t)dv->valuedouble;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "slots");
    cJSON *el  = nullptr;
    cJSON_ArrayForEach(el, arr) {
        if (s->slot_count >= SCHED_MAX_SLOTS) break;
        VolumeSlot &v = s->slots[s->slot_count];

        auto gi = [&](const char *k, int def) -> int {
            cJSON *it = cJSON_GetObjectItemCaseSensitive(el, k);
            return cJSON_IsNumber(it) ? (int)it->valuedouble : def;
        };
        v.active    = (uint8_t)gi("a",  0);
        v.days_mask = (uint8_t)gi("d",  SCHED_ALL_DAYS);
        v.hour_from = (uint8_t)gi("hf", 22);
        v.min_from  = (uint8_t)gi("mf",  0);
        v.hour_to   = (uint8_t)gi("ht",  7);
        v.min_to    = (uint8_t)gi("mt",  0);
        v.volume    = (uint8_t)gi("v",   0);
        ++s->slot_count;
    }
    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// Publiczne API
// ---------------------------------------------------------------------------
esp_err_t sched_load(SpeakerSchedule *out)
{
    // Wartości domyślne
    s_sched.default_volume = 70;
    s_sched.slot_count     = 0;
    memset(s_sched.slots, 0, sizeof(s_sched.slots));

    nvs_handle_t nvs = 0;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "Brak harmonogramu w NVS — domyslne wartosci");
        if (out) *out = s_sched;
        return ESP_OK;
    }

    char buf[768] = {};
    size_t len = sizeof(buf) - 1;
    esp_err_t err = nvs_get_str(nvs, kKey, buf, &len);
    nvs_close(nvs);

    if (err == ESP_OK && buf[0]) {
        parse_json_to_sched(buf, &s_sched);
        ESP_LOGI(TAG, "Harmonogram: %d slotow, domyslna glosnosc %d%%",
                 s_sched.slot_count, s_sched.default_volume);
    }

    if (out) *out = s_sched;
    return ESP_OK;
}

esp_err_t sched_save(const SpeakerSchedule *s)
{
    s_sched = *s;

    // Serializuj do JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "dv", s->default_volume);
    cJSON *arr = cJSON_AddArrayToObject(root, "slots");
    for (int i = 0; i < s->slot_count; ++i) {
        const VolumeSlot &v = s->slots[i];
        cJSON *sl = cJSON_CreateObject();
        cJSON_AddNumberToObject(sl, "a",  v.active);
        cJSON_AddNumberToObject(sl, "d",  v.days_mask);
        cJSON_AddNumberToObject(sl, "hf", v.hour_from);
        cJSON_AddNumberToObject(sl, "mf", v.min_from);
        cJSON_AddNumberToObject(sl, "ht", v.hour_to);
        cJSON_AddNumberToObject(sl, "mt", v.min_to);
        cJSON_AddNumberToObject(sl, "v",  v.volume);
        cJSON_AddItemToArray(arr, sl);
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, kKey, txt);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    cJSON_free(txt);

    if (err != ESP_OK)
        ESP_LOGE(TAG, "Zapis harmonogramu nieudany: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "Harmonogram zapisany (%d slotow)", s->slot_count);

    return err;
}

esp_err_t sched_save_json(const char *json)
{
    if (!json) return ESP_ERR_INVALID_ARG;
    SpeakerSchedule s = {};
    parse_json_to_sched(json, &s);
    return sched_save(&s);
}

char *sched_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "dv", s_sched.default_volume);
    cJSON *arr = cJSON_AddArrayToObject(root, "slots");
    for (int i = 0; i < s_sched.slot_count; ++i) {
        const VolumeSlot &v = s_sched.slots[i];
        cJSON *sl = cJSON_CreateObject();
        cJSON_AddNumberToObject(sl, "a",  v.active);
        cJSON_AddNumberToObject(sl, "d",  v.days_mask);
        cJSON_AddNumberToObject(sl, "hf", v.hour_from);
        cJSON_AddNumberToObject(sl, "mf", v.min_from);
        cJSON_AddNumberToObject(sl, "ht", v.hour_to);
        cJSON_AddNumberToObject(sl, "mt", v.min_to);
        cJSON_AddNumberToObject(sl, "v",  v.volume);
        cJSON_AddItemToArray(arr, sl);
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;  // wywołujący musi zwolnić przez cJSON_free()
}

uint8_t sched_get_volume(void)
{
    // Gdy NTP nie zsynchronizowany — zwróć domyślną głośność
    time_t now;
    time(&now);
    if (now < 1577836800L) return s_sched.default_volume;

    struct tm t;
    localtime_r(&now, &t);

    for (int i = 0; i < s_sched.slot_count; ++i) {
        const VolumeSlot &sl = s_sched.slots[i];
        if (!sl.active) continue;
        if (slot_matches(sl, t.tm_hour, t.tm_min, t.tm_wday))
            return sl.volume;
    }
    return s_sched.default_volume;
}
