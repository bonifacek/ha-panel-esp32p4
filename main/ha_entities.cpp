#include "ha_entities.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Plik JSON dashboardu w SPIFFS (partycja "storage", baza /storage)
static const char *kDashboardFile = "/storage/ha_dashboard.json";

static const char *TAG = "ha_entities";

static HaScreen *s_screens;
static size_t    s_screen_count;
static size_t    s_default_screen;

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------
static void extract_domain(const char *entity_id, char *domain, size_t domain_len)
{
    const char *dot = strchr(entity_id, '.');
    if (!dot) { strlcpy(domain, "homeassistant", domain_len); return; }
    size_t len = (size_t)(dot - entity_id);
    if (len >= domain_len) len = domain_len - 1;
    memcpy(domain, entity_id, len);
    domain[len] = '\0';
}

static bool is_switch_domain(const char *domain)
{
    return strcmp(domain, "switch")       == 0 ||
           strcmp(domain, "light")        == 0 ||
           strcmp(domain, "fan")          == 0 ||
           strcmp(domain, "input_boolean")== 0 ||
           strcmp(domain, "automation")   == 0;
}

static esp_err_t ensure_storage()
{
    if (s_screens) return ESP_OK;
    s_screens = static_cast<HaScreen *>(
        heap_caps_calloc(kHaMaxScreens, sizeof(HaScreen),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_screens)
        s_screens = static_cast<HaScreen *>(calloc(kHaMaxScreens, sizeof(HaScreen)));
    return s_screens ? ESP_OK : ESP_ERR_NO_MEM;
}

// ---------------------------------------------------------------------------
// Parsowanie JSON
// Format zapisywany przez web GUI:
// {
//   "default_screen": 0,
//   "screens": [
//     {
//       "name": "Salon",
//       "tiles": [
//         {
//           "label": "Klimat",
//           "rows": [
//             { "entity_id":"sensor.temp", "label":"Temperatura",
//               "attribute":"", "unit":"°C" }
//           ]
//         }
//       ]
//     }
//   ]
// }
// ---------------------------------------------------------------------------
static void parse_row(cJSON *obj, HaRow *row)
{
    auto get_str = [&](const char *key, char *dst, size_t dst_len) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
        strlcpy(dst,
                (cJSON_IsString(item) && item->valuestring) ? item->valuestring : "",
                dst_len);
    };
    get_str("entity_id", row->entity_id, sizeof(row->entity_id));
    get_str("label",     row->label,     sizeof(row->label));
    get_str("attribute", row->attribute, sizeof(row->attribute));
    get_str("unit",      row->unit,      sizeof(row->unit));

    // Typ sensora — opcjonalne pole "sensor_type" w JSON
    cJSON *st_item = cJSON_GetObjectItemCaseSensitive(obj, "sensor_type");
    if (cJSON_IsString(st_item) && st_item->valuestring) {
        const char *s = st_item->valuestring;
        if      (strcmp(s, "temperature") == 0)  row->sensor_type = SensorType::Temperature;
        else if (strcmp(s, "humidity")    == 0)  row->sensor_type = SensorType::Humidity;
        else if (strcmp(s, "cpu")         == 0)  row->sensor_type = SensorType::Cpu;
        else if (strcmp(s, "memory")      == 0)  row->sensor_type = SensorType::Memory;
        else if (strcmp(s, "power")       == 0)  row->sensor_type = SensorType::Power;
        else if (strcmp(s, "illuminance") == 0)  row->sensor_type = SensorType::Illuminance;
        else                                      row->sensor_type = SensorType::Generic;
    } else {
        row->sensor_type = SensorType::Generic;
    }

    extract_domain(row->entity_id, row->domain, sizeof(row->domain));
    row->kind = is_switch_domain(row->domain) ? EntityKind::Switch : EntityKind::Sensor;

    if (row->label[0] == '\0')
        strlcpy(row->label, row->entity_id, sizeof(row->label));
}

static void parse_tile(cJSON *obj, HaTile *tile)
{
    cJSON *lbl = cJSON_GetObjectItemCaseSensitive(obj, "label");
    strlcpy(tile->label,
            (cJSON_IsString(lbl) && lbl->valuestring) ? lbl->valuestring : "Kafelek",
            sizeof(tile->label));

    // Layout (pionowy / poziomy)
    cJSON *layout_j = cJSON_GetObjectItemCaseSensitive(obj, "layout");
    tile->layout = (cJSON_IsString(layout_j) && layout_j->valuestring &&
                    strcmp(layout_j->valuestring, "horizontal") == 0)
                   ? TileLayout::Horizontal : TileLayout::Vertical;

    // Pozycja i rozmiar — opcjonalne; -1/0 = auto
    auto get_i16 = [](cJSON *parent_obj, const char *key, int16_t def) -> int16_t {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(parent_obj, key);
        return cJSON_IsNumber(item) ? (int16_t)item->valuedouble : def;
    };
    tile->x = get_i16(obj, "x", -1);
    tile->y = get_i16(obj, "y", -1);
    tile->w = get_i16(obj, "w",  0);
    tile->h = get_i16(obj, "h",  0);

    tile->row_count = 0;
    cJSON *rows_arr = cJSON_GetObjectItemCaseSensitive(obj, "rows");
    cJSON *r = nullptr;
    cJSON_ArrayForEach(r, rows_arr) {
        if (tile->row_count >= kHaMaxRows) break;
        if (cJSON_IsObject(r))
            parse_row(r, &tile->rows[tile->row_count++]);
    }
}

static void parse_screen(cJSON *obj, HaScreen *screen)
{
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(obj, "name");
    strlcpy(screen->name,
            (cJSON_IsString(nm) && nm->valuestring) ? nm->valuestring : "Ekran",
            sizeof(screen->name));

    screen->tile_count = 0;
    cJSON *tiles_arr = cJSON_GetObjectItemCaseSensitive(obj, "tiles");
    cJSON *t = nullptr;
    cJSON_ArrayForEach(t, tiles_arr) {
        if (screen->tile_count >= kHaMaxTiles) break;
        if (cJSON_IsObject(t))
            parse_tile(t, &screen->tiles[screen->tile_count++]);
    }
}

// ---------------------------------------------------------------------------
// Publiczne API
// ---------------------------------------------------------------------------
esp_err_t ha_dashboard_load()
{
    esp_err_t err = ensure_storage();
    if (err != ESP_OK) return err;

    s_screen_count   = 0;
    s_default_screen = 0;
    memset(s_screens, 0, sizeof(HaScreen) * kHaMaxScreens);

    // Odczyt z SPIFFS (partycja "storage", max 7 MB — bez limitu NVS ~4 KB)
    FILE *f = fopen(kDashboardFile, "r");
    if (!f) {
        ESP_LOGW(TAG, "Brak pliku dashboardu (%s) — pusta konfiguracja", kDashboardFile);
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0 || file_size > 512 * 1024L) {
        fclose(f);
        ESP_LOGW(TAG, "Nieprawidlowy rozmiar pliku dashboardu: %ld B", file_size);
        return ESP_OK;
    }

    char *body = static_cast<char *>(calloc(1, (size_t)file_size + 1));
    if (!body) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t rd = fread(body, 1, (size_t)file_size, f);
    fclose(f);
    body[rd] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGW(TAG, "JSON dashboardu niepoprawny");
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *def = cJSON_GetObjectItemCaseSensitive(root, "default_screen");
    if (cJSON_IsNumber(def))
        s_default_screen = (size_t)def->valuedouble;

    cJSON *screens_arr = cJSON_GetObjectItemCaseSensitive(root, "screens");
    cJSON *s = nullptr;
    cJSON_ArrayForEach(s, screens_arr) {
        if (s_screen_count >= kHaMaxScreens) break;
        if (cJSON_IsObject(s))
            parse_screen(s, &s_screens[s_screen_count++]);
    }
    cJSON_Delete(root);

    if (s_default_screen >= s_screen_count && s_screen_count > 0)
        s_default_screen = 0;

    ESP_LOGI(TAG, "Zaladowano %u ekranow (%ld B)", (unsigned)s_screen_count, file_size);
    return ESP_OK;
}

esp_err_t ha_dashboard_save_json(const char *json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(kDashboardFile, "w");
    if (!f) {
        ESP_LOGE(TAG, "Nie mozna otworzyc %s do zapisu", kDashboardFile);
        return ESP_FAIL;
    }

    const size_t json_len = strlen(json);
    const size_t written  = fwrite(json, 1, json_len, f);
    int close_err = fclose(f);

    if (written != json_len || close_err != 0) {
        ESP_LOGE(TAG, "Blad zapisu: %zu/%zu B, fclose=%d", written, json_len, close_err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Dashboard zapisany: %zu B -> %s", json_len, kDashboardFile);
    return ESP_OK;
}

size_t          ha_screen_count()           { return s_screen_count; }
size_t          ha_default_screen()         { return s_default_screen; }
const HaScreen *ha_screen_get(size_t i)     { return (i < s_screen_count) ? &s_screens[i] : nullptr; }
