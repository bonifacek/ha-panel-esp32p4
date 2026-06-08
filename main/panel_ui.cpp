#include "panel_ui.h"

#include "board_display.h"
#include "lvgl.h"
#include "polish_fonts.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "panel_ui";

// ---------------------------------------------------------------------------
// Font ikon FontAwesome (fa-solid-900.ttf, size 18)
// ---------------------------------------------------------------------------
LV_FONT_DECLARE(lv_font_icons_18)

// UTF-8 encoding codepoints FontAwesome Solid
#define ICON_THERMO  "\xEF\x8B\x89"   // f2c9  termometr
#define ICON_DROP    "\xEF\x81\x83"   // f043  kropla (wilgotnosc)
#define ICON_BOLT    "\xEF\x83\xA7"   // f0e7  blyskawica (moc/energia)
#define ICON_BULB    "\xEF\x83\xAB"   // f0eb  zarowka (swiatlo)
#define ICON_PLUG    "\xEF\x87\xA6"   // f1e6  wtyczka
#define ICON_FIRE    "\xEF\x81\xAD"   // f06d  ogien (ogrzewanie)
#define ICON_SNOW    "\xEF\x8B\x9C"   // f2dc  platki sniegu (klimatyzacja)

// ---------------------------------------------------------------------------
// Kolory
// ---------------------------------------------------------------------------
static constexpr uint32_t kColorBackground   = 0x080D12;
static constexpr uint32_t kColorHeader       = 0x101923;
static constexpr uint32_t kColorCard         = 0x121D27;
static constexpr uint32_t kColorCardOffline  = 0x2A1B16;
static constexpr uint32_t kColorText         = 0xEEF6F8;
static constexpr uint32_t kColorMuted        = 0x9BAAB3;
static constexpr uint32_t kColorAccent       = 0x35D0BA;
static constexpr uint32_t kColorAccent2      = 0xF6B84A;
static constexpr uint32_t kColorDanger       = 0xF07863;
// Trend
static constexpr time_t kTrendStableSec = 30 * 60;  // 30 minut bez zmiany → stabilny

enum class TrendDir : uint8_t { None, Up, Down, Stable };

// Switch state colors
static constexpr uint32_t kColorSwitchOnBg      = 0x0D3A30;
static constexpr uint32_t kColorSwitchOffBg     = 0x2A1010;
static constexpr uint32_t kColorSwitchOnBtn     = 0x0C4838;  // ciemna zieleń: włączony
static constexpr uint32_t kColorSwitchOffBtn    = 0x1E2A35;  // ciemny niebieski: wyłączony
static constexpr uint32_t kColorSwitchOnPress   = 0x186550;  // jaśniejsza zieleń: przyciśnięty
static constexpr uint32_t kColorSwitchOffPress  = 0x2A3A48;  // jaśniejszy niebieski: przyciśnięty

// ---------------------------------------------------------------------------
// Globalne wskazniki LVGL
// ---------------------------------------------------------------------------
static lv_obj_t  *s_wifi_label;
static lv_obj_t  *s_mqtt_label;
static lv_obj_t  *s_battery_label;
static lv_obj_t  *s_datetime_label = nullptr;
static lv_timer_t *s_clock_timer   = nullptr;
static lv_obj_t  *s_tabview;

// Etykiety wartosci: [screen][tile][row]
static lv_obj_t *s_row_labels[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Karty kafelkow: [screen][tile]
static lv_obj_t *s_tile_cards[kHaMaxScreens][kHaMaxTiles];
// Przyciski switch: [screen][tile][row]
static lv_obj_t *s_switch_btns[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Kółko stanu (lewy wskaźnik w przycisku): [screen][tile][row]
static lv_obj_t *s_switch_circles[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Stany przelacznikow: [screen][tile][row]
static bool      s_switch_states[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Ikony kolorowe (osobny label): [screen][tile][row]
static lv_obj_t *s_icon_labels[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Strzalki trendu (sensor): [screen][tile][row]
static lv_obj_t  *s_trend_labels[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Wskazniki kolowe (arc, sensor w trybie Arc): [screen][tile][row]
static lv_obj_t  *s_arc_widgets[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
// Krople statusu urządzenia (Device): [screen][tile][row]
static lv_obj_t  *s_device_dots[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
static float      s_prev_num_value[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
static bool       s_has_num_value[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
static TrendDir   s_trend_dir[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
static time_t     s_trend_ts[kHaMaxScreens][kHaMaxTiles][kHaMaxRows];
static lv_timer_t *s_trend_timer = nullptr;

static UiCommandCallback s_command_callback;

// ---------------------------------------------------------------------------
// Animacje
// ---------------------------------------------------------------------------
static void anim_flash_cb(void *obj, int32_t val)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
}

// Po zakonczeniu flesza sensora: przywroc ciemne tlo
static void sensor_flash_done(lv_anim_t *a)
{
    lv_obj_t *lbl = (lv_obj_t *)a->var;
    lv_obj_set_style_bg_color(lbl, lv_color_hex(0x0C141B), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_60, 0);
}

// Krotki blask aktualizacji na etykiecie sensora (teal → zanika → ciemne tlo)
static void sensor_label_flash(lv_obj_t *lbl)
{
    lv_obj_set_style_bg_color(lbl, lv_color_hex(kColorAccent), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl);
    lv_anim_set_exec_cb(&a, anim_flash_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, 0);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, sensor_flash_done);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
// Strzalki trendu — stale widoczne, 3 stany
// ---------------------------------------------------------------------------

// Aktualizuje etykiete trendu: ▲ zielona / ▼ czerwona / — biala / ukryta
static void update_trend_label(lv_obj_t *lbl, TrendDir dir)
{
    if (!lbl) return;

    // Reset po ewentualnym stanie Stable (który ustawiał stały rozmiar i tło)
    lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(lbl, 0, 0);

    switch (dir) {
        case TrendDir::None:
            lv_label_set_text(lbl, "");
            lv_obj_set_style_opa(lbl, LV_OPA_TRANSP, 0);
            break;
        case TrendDir::Up:
            lv_label_set_text(lbl, LV_SYMBOL_UP);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x4CD97B), 0);
            lv_obj_set_style_opa(lbl, LV_OPA_COVER, 0);
            break;
        case TrendDir::Down:
            lv_label_set_text(lbl, LV_SYMBOL_DOWN);
            lv_obj_set_style_text_color(lbl, lv_color_hex(kColorDanger), 0);
            lv_obj_set_style_opa(lbl, LV_OPA_COVER, 0);
            break;
        case TrendDir::Stable:
            // Żółty prostokąt 18×3 px — nie zależy od czcionki
            lv_label_set_text(lbl, " ");
            lv_obj_set_size(lbl, 18, 3);
            lv_obj_set_style_bg_color(lbl, lv_color_hex(kColorAccent2), 0);
            lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(lbl, 2, 0);
            lv_obj_set_style_opa(lbl, LV_OPA_COVER, 0);
            break;
    }
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0);
}

// Timer co 60s: jesli Up/Down nie zmienilo sie od 30min → przelacz na Stable
static void trend_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    time_t now;
    time(&now);
    if (now < 1577836800) return;  // NTP jeszcze nie zsynchronizowany

    for (size_t s = 0; s < kHaMaxScreens; ++s) {
        for (size_t t = 0; t < kHaMaxTiles; ++t) {
            for (size_t r = 0; r < kHaMaxRows; ++r) {
                TrendDir &dir = s_trend_dir[s][t][r];
                if ((dir == TrendDir::Up || dir == TrendDir::Down) &&
                    (now - s_trend_ts[s][t][r]) >= kTrendStableSec) {
                    dir = TrendDir::Stable;
                    update_trend_label(s_trend_labels[s][t][r], TrendDir::Stable);
                }
            }
        }
    }
}

// Zmiana koloru etykiety switch + krotki blask
// ON: zielony tekst + ciemno-zielone tlo; OFF: teal tekst + ciemne tlo
static void switch_label_flash(lv_obj_t *lbl, bool is_on)
{
    lv_obj_set_style_bg_color(lbl, lv_color_hex(is_on ? 0x0C2E22 : 0x0C141B), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(is_on ? 0x22C55E : kColorAccent), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl);
    lv_anim_set_exec_cb(&a, anim_flash_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_90);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------
// Dobiera ikonę do wiersza encji — sensor_type ma priorytet przed jednostką
static const char *icon_for_row(const HaRow *row)
{
    if (row->kind == EntityKind::Presence || row->kind == EntityKind::Device) {
        return "";  // brak dedykowanej ikony — Device uzywa tylko kola koloru
    }

    if (row->kind == EntityKind::Switch) {
        const char *d = row->domain;
        if (strcmp(d, "light") == 0)                            return ICON_BULB;
        if (strcmp(d, "fan") == 0)                              return ICON_SNOW;
        if (strcmp(d, "climate") == 0)                          return ICON_FIRE;
        if (strcmp(d, "switch") == 0 ||
            strcmp(d, "input_boolean") == 0)                    return ICON_PLUG;
        return ICON_PLUG;  // cover, media_player, automation itp.
    }

    // SensorType-based (precyzyjny — ustawiany przez uzytkownika w edytorze)
    switch (row->sensor_type) {
        case SensorType::Temperature: return ICON_THERMO;
        case SensorType::Humidity:    return ICON_DROP;
        case SensorType::Power:       return ICON_BOLT;
        case SensorType::Illuminance: return ICON_BULB;
        case SensorType::Cpu:         return "";   // brak dedykowanej ikony CPU
        case SensorType::Memory:      return "";   // brak dedykowanej ikony RAM
        default: break;
    }

    // Fallback oparty na jednostce (Generic lub brak sensor_type)
    const char *u = row->unit;
    if (!u || !u[0]) return "";
    if (strcmp(u, "\xc2\xb0""C") == 0 || strcmp(u, "\xc2\xb0""F") == 0 ||
        strcmp(u, "°C") == 0 || strcmp(u, "°F") == 0) return ICON_THERMO;
    // "%" bez sensor_type → NIE pokazuj ikony kropli (byloby mylace dla CPU/RAM)
    if (strcmp(u, "W")  == 0 || strcmp(u, "kW") == 0 ||
        strcmp(u, "kWh") == 0 || strcmp(u, "Wh") == 0)   return ICON_BOLT;
    if (strcmp(u, "lx") == 0 || strcmp(u, "lm") == 0)    return ICON_BULB;
    return "";
}

// Poczatkowy (statyczny) kolor ikony — zanim przyjdzie pierwsza wartosc
static lv_color_t icon_color_for_row(const HaRow *row)
{
    if (row->kind == EntityKind::Presence || row->kind == EntityKind::Device) {
        return lv_color_hex(kColorMuted);
    }

    if (row->kind == EntityKind::Switch) {
        const char *d = row->domain;
        if (strcmp(d, "light") == 0)       return lv_color_hex(0xFFE082); // ciepla zolc — zarowka
        if (strcmp(d, "fan") == 0)         return lv_color_hex(0x4FC3F7); // jasno-niebieski — wiatr
        if (strcmp(d, "climate") == 0)     return lv_color_hex(0xFF7043); // pomaranczowy — ogrzewanie
        return lv_color_hex(0x81C784);  // zielony — wtyczka (domyslny)
    }

    switch (row->sensor_type) {
        case SensorType::Temperature: return lv_color_hex(0xFF7043); // pomaranczowy — termometr
        case SensorType::Humidity:    return lv_color_hex(0x4FC3F7); // jasno-niebieski — kropla
        case SensorType::Power:       return lv_color_hex(0xFFD600); // zolty — blyskawica
        case SensorType::Illuminance: return lv_color_hex(0xFFE082); // ciepla zolc — zarowka
        default: break;
    }

    // Fallback dla Generic oparty na jednostce
    const char *u = row->unit;
    if (!u || !u[0]) return lv_color_hex(kColorMuted);
    if (strcmp(u, "\xc2\xb0""C") == 0 || strcmp(u, "\xc2\xb0""F") == 0 ||
        strcmp(u, "°C") == 0 || strcmp(u, "°F") == 0)  return lv_color_hex(0xFF7043);
    if (strcmp(u, "W")  == 0 || strcmp(u, "kW") == 0 ||
        strcmp(u, "kWh") == 0 || strcmp(u, "Wh") == 0)  return lv_color_hex(0xFFD600);
    if (strcmp(u, "lx") == 0 || strcmp(u, "lm") == 0)   return lv_color_hex(0xFFE082);
    return lv_color_hex(kColorMuted);
}

// Dynamiczny kolor ikony zalezny od wartosci — temperatura i wilgotnosc
static lv_color_t icon_color_for_value(SensorType type, float val)
{
    switch (type) {
        case SensorType::Temperature:
            // < 0°C: niebieski | 0-15: jasno-niebieski | 15-23: zielony
            // 23-26: pomaranczowy | > 26: czerwony
            if (val < 0.0f)   return lv_color_hex(0x2196F3); // niebieski
            if (val < 15.0f)  return lv_color_hex(0x4FC3F7); // jasno-niebieski
            if (val < 23.0f)  return lv_color_hex(0x4CAF50); // zielony
            if (val < 26.0f)  return lv_color_hex(0xFF7043); // pomaranczowy
            return lv_color_hex(kColorDanger);                // czerwony (> 26°C)

        case SensorType::Humidity:
            // < 40%: zolty | 40-60%: zielony | > 60%: czerwony
            if (val < 40.0f)  return lv_color_hex(0xFFD600); // zolty
            if (val <= 60.0f) return lv_color_hex(0x4CAF50); // zielony
            return lv_color_hex(kColorDanger);                // czerwony (> 60%)

        default:
            return lv_color_hex(kColorMuted); // bez specjalnego koloru
    }
}

// ---------------------------------------------------------------------------
// Animacje ikon
// ---------------------------------------------------------------------------

// Zatrzymaj animacje, przywróć pełną opacity i obrót 0
static void icon_anim_stop(lv_obj_t *icon)
{
    if (!icon) return;
    lv_anim_delete(icon, nullptr);
    lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_rotation(icon, 0, 0);
}

// Żarówka ON: łagodne pulsowanie opacity 160 ↔ 255
static void icon_anim_bulb_on(lv_obj_t *icon)
{
    if (!icon) return;
    lv_anim_delete(icon, nullptr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
    });
    lv_anim_set_values(&a, 160, 255);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_playback_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// Żarówka OFF: przyciemniona ikona (50%)
static void icon_anim_bulb_off(lv_obj_t *icon)
{
    if (!icon) return;
    lv_anim_delete(icon, nullptr);
    lv_obj_set_style_opa(icon, LV_OPA_50, 0);
    lv_obj_set_style_transform_rotation(icon, 0, 0);
}

// Wentylator ON: ciągły obrót 360° co 2s (pivot musi byc ustawiony przy tworzeniu)
static void icon_anim_fan_on(lv_obj_t *icon)
{
    if (!icon) return;
    lv_anim_delete(icon, nullptr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_style_transform_rotation(static_cast<lv_obj_t *>(obj), v, 0);
    });
    lv_anim_set_values(&a, 0, 3600);       // 0.1° jednostki → 3600 = 360°
    lv_anim_set_duration(&a, 2000);        // 1 obrót / 2s
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

// Zakres osi wskaznika kolowego dla danego typu sensora
static void arc_range_for_sensor(SensorType type, int16_t *out_min, int16_t *out_max)
{
    switch (type) {
        case SensorType::Temperature: *out_min = -20;  *out_max = 50;   break;
        case SensorType::Humidity:    *out_min = 0;    *out_max = 100;  break;
        case SensorType::Cpu:         *out_min = 0;    *out_max = 100;  break;
        case SensorType::Memory:      *out_min = 0;    *out_max = 100;  break;
        case SensorType::Power:       *out_min = 0;    *out_max = 3500; break;
        case SensorType::Illuminance: *out_min = 0;    *out_max = 1000; break;
        default:                      *out_min = 0;    *out_max = 100;  break;
    }
}

// Parsuje pierwszą liczbę z łańcucha (np. "23.5" z "23.5 °C" lub "65" z "65%")
static bool parse_first_float(const char *str, float *out)
{
    if (!str || !out) return false;
    char *end;
    *out = strtof(str, &end);
    return (end != str);
}

static bool payload_is_on(const char *value)
{
    return value &&
           (strcasecmp(value, "on")   == 0 ||
            strcasecmp(value, "1")    == 0 ||
            strcasecmp(value, "true") == 0 ||
            strcasecmp(value, "open") == 0);
}

static void format_row_value(const HaRow *row, const char *value,
                              char *out, size_t out_len,
                              bool compact = false)
{
    if (out_len == 0) return;
    const char *safe = value ? value : "--";
    char formatted[64] = {};

    // Device: bez tekstu statusu — wartosc nie jest wyswietlana jako tekst
    if (row->kind == EntityKind::Device) {
        strlcpy(out, "", out_len);
        return;
    }

    if (row->kind == EntityKind::Presence) {
        // home/not_home → czytelne polskie etykiety; inne stany (strefy) → as-is
        if      (strcmp(safe, "home")     == 0) strlcpy(formatted, "W domu",    sizeof(formatted));
        else if (strcmp(safe, "not_home") == 0) strlcpy(formatted, "Nieobecny", sizeof(formatted));
        else                                    strlcpy(formatted, safe,         sizeof(formatted));
        // Presence nie ma jednostki — format zawsze: "Etykieta: Stan"
        snprintf(out, out_len, "%s: %s", row->label, formatted);
        return;
    }

    if (row->kind == EntityKind::Switch) {
        if (payload_is_on(safe)) {
            strlcpy(formatted, "Wlaczony", sizeof(formatted));
        } else if (strcasecmp(safe, "off")   == 0 ||
                   strcasecmp(safe, "0")     == 0 ||
                   strcasecmp(safe, "false") == 0) {
            strlcpy(formatted, "Wylaczony", sizeof(formatted));
        } else {
            strlcpy(formatted, safe, sizeof(formatted));
        }
    } else {
        // Wartość numeryczna z częścią dziesiętną → pokazuj tylko 1 miejsce po przecinku
        char *end;
        const float val = strtof(safe, &end);
        if (end != safe && *end == '\0' && strchr(safe, '.') != nullptr) {
            snprintf(formatted, sizeof(formatted), "%.1f", val);
        } else {
            strlcpy(formatted, safe, sizeof(formatted));
        }
    }

    // compact = tryb poziomy: sama wartosc + jednostka (bez etykiety)
    if (compact) {
        if (row->unit[0] != '\0')
            snprintf(out, out_len, "%s%s", formatted, row->unit);
        else
            strlcpy(out, formatted, out_len);
    } else {
        if (row->unit[0] != '\0')
            snprintf(out, out_len, "%s: %s %s", row->label, formatted, row->unit);
        else
            snprintf(out, out_len, "%s: %s", row->label, formatted);
    }
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------
static void style_status_chip(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(label, &lv_font_polish_18, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x192633), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label, 8, 0);
    lv_obj_set_style_pad_hor(label, 10, 0);
    lv_obj_set_style_pad_ver(label, 5, 0);
}

// text_color: kolor tekstu; bg_color: kolor tla (0 = ciemne domyslne)
static void style_value_label(lv_obj_t *label, uint32_t text_color)
{
    lv_obj_set_style_text_font(label, &lv_font_polish_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x0C141B), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_60, 0);
    lv_obj_set_style_radius(label, 7, 0);
    lv_obj_set_style_pad_hor(label, 10, 0);
    lv_obj_set_style_pad_ver(label, 7, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
}

// ---------------------------------------------------------------------------
// Callback przycisku switch
// user_data: packed = (screen << 8) | (tile << 4) | row
// ---------------------------------------------------------------------------
static void switch_event_cb(lv_event_t *event)
{
    const uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    const size_t s = (packed >> 8) & 0x0F;
    const size_t t = (packed >> 4) & 0x0F;
    const size_t r = packed        & 0x0F;

    if (!s_command_callback) return;
    const bool next_state = !s_switch_states[s][t][r];
    s_command_callback(s, t, r, next_state);
}

// ---------------------------------------------------------------------------
// Callback zegara NTP — aktualizacja co 1 sekunde
// ---------------------------------------------------------------------------
static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_datetime_label) return;

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    if (t.tm_year < (2020 - 1900)) {
        lv_label_set_text(s_datetime_label, "--:--");
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d  %02d.%02d.%04d",
             t.tm_hour, t.tm_min,
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    lv_label_set_text(s_datetime_label, buf);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
static void make_status_bar(lv_obj_t *root)
{
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_size(bar, LV_PCT(100), 64);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kColorHeader), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ── LEWA SEKCJA: data i czas (NTP) ───────────────────────────────────
    lv_obj_t *left = lv_obj_create(bar);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, 64);
    lv_obj_set_flex_grow(left, 1);

    s_datetime_label = lv_label_create(left);
    lv_label_set_text(s_datetime_label, "--:--");
    lv_obj_set_style_text_color(s_datetime_label, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_datetime_label, &lv_font_polish_20, 0);
    lv_obj_align(s_datetime_label, LV_ALIGN_LEFT_MID, 0, 0);

    // ── SRODKOWA SEKCJA: tytul aplikacji ─────────────────────────────────
    lv_obj_t *mid = lv_obj_create(bar);
    lv_obj_remove_style_all(mid);
    lv_obj_set_height(mid, 64);
    lv_obj_set_flex_grow(mid, 1);

    lv_obj_t *title = lv_label_create(mid);
    lv_label_set_text(title, "Home Assistant panel");
    lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(title, &lv_font_polish_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // ── PRAWA SEKCJA: chipsty statusow ───────────────────────────────────
    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, 64);
    lv_obj_set_flex_grow(right, 2);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(right, 8, 0);

    s_wifi_label = lv_label_create(right);
    lv_label_set_text(s_wifi_label, "Wi-Fi: start");
    style_status_chip(s_wifi_label);

    s_mqtt_label = lv_label_create(right);
    lv_label_set_text(s_mqtt_label, "HA: start");
    style_status_chip(s_mqtt_label);

    s_battery_label = lv_label_create(right);
    lv_label_set_text(s_battery_label, "BAT: --");
    style_status_chip(s_battery_label);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(kColorAccent2), 0);
}

// ---------------------------------------------------------------------------
// Kafelek: jedno skupisko wielu wierszy encji
// ---------------------------------------------------------------------------
static int32_t calc_tile_height(const HaTile *tile)
{
    if (tile->layout == TileLayout::Horizontal)
        return 56;  // pojedynczy wiersz poziomy: naglowek + encje obok siebie

    // Pionowy: header: 30px + padding_top: 14px + padding_bot: 14px = 58
    int32_t h = 58;
    for (size_t r = 0; r < tile->row_count; ++r) {
        const HaRow *row_r = &tile->rows[r];
        if (row_r->kind == EntityKind::Switch) {
            h += 50;  // dotykowy wiersz switch (bez osobnego przycisku)
        } else if (row_r->kind == EntityKind::Sensor &&
                   row_r->display_mode == RowDisplayMode::Arc) {
            h += 88;  // kontener wskaznika kolowego
        } else {
            h += 38;  // standardowy wiersz (Sensor tekst / Presence / Device)
        }
        if (r < tile->row_count - 1)
            h += 7;
    }
    return h < 120 ? 120 : h;
}

static void make_tile_card(lv_obj_t *parent,
                            size_t screen_idx, size_t tile_idx)
{
    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen || tile_idx >= screen->tile_count) return;
    const HaTile *tile = &screen->tiles[tile_idx];

    lv_obj_t *card = lv_obj_create(parent);
    s_tile_cards[screen_idx][tile_idx] = card;

    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(kColorCard), 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(card, 4, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kColorAccent), 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);

    // Pozycjonowanie i rozmiar
    const int32_t auto_h = calc_tile_height(tile);
    if (tile->x >= 0) {
        // Jawna pozycja z JSON (edytor ustawi koordynaty)
        lv_obj_set_pos(card, tile->x, tile->y);
        int32_t w = (tile->w > 0) ? tile->w : 490;
        int32_t h = (tile->h > 0) ? tile->h : auto_h;
        lv_obj_set_size(card, w, h);
    } else {
        // Auto: zachowanie wsteczne — flex wrap na rodzicu
        lv_obj_set_size(card, LV_PCT(48), auto_h);
    }

    // ── UKŁAD POZIOMY ────────────────────────────────────────────────────────
    if (tile->layout == TileLayout::Horizontal) {
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_pad_gap(card, 10, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Nazwa kafelka — lewa kolumna (stala szerokosc)
        lv_obj_t *header = lv_label_create(card);
        lv_label_set_text(header, tile->label);
        lv_obj_set_style_text_font(header, &lv_font_polish_18, 0);
        lv_obj_set_style_text_color(header, lv_color_hex(kColorAccent2), 0);
        lv_label_set_long_mode(header, LV_LABEL_LONG_DOT);
        lv_obj_set_width(header, 110);

        // Pionowy separator
        lv_obj_t *sep = lv_obj_create(card);
        lv_obj_remove_style_all(sep);
        lv_obj_set_size(sep, 1, 28);
        lv_obj_set_style_bg_color(sep, lv_color_hex(kColorMuted), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);

        // Encje: jedna obok drugiej
        for (size_t r = 0; r < tile->row_count; ++r) {
            const HaRow *row = &tile->rows[r];
            const char *icon_glyph = icon_for_row(row);

            lv_obj_t *cell = lv_obj_create(card);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(cell, 4, 0);

            // Device w ukladzie poziomym: tylko kolo koloru
            if (row->kind == EntityKind::Device) {
                lv_obj_t *dot = lv_obj_create(cell);
                s_device_dots[screen_idx][tile_idx][r] = dot;
                lv_obj_remove_style_all(dot);
                lv_obj_set_size(dot, 18, 18);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, lv_color_hex(0x4A5568), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                s_row_labels[screen_idx][tile_idx][r]    = nullptr;
                s_icon_labels[screen_idx][tile_idx][r]   = nullptr;
                s_switch_btns[screen_idx][tile_idx][r]   = nullptr;
                s_switch_circles[screen_idx][tile_idx][r]= nullptr;
                s_trend_labels[screen_idx][tile_idx][r]  = nullptr;
                s_arc_widgets[screen_idx][tile_idx][r]   = nullptr;
                continue;
            }

            if (icon_glyph[0] != '\0') {
                lv_obj_t *icon_lbl = lv_label_create(cell);
                lv_label_set_text(icon_lbl, icon_glyph);
                lv_obj_set_style_text_font(icon_lbl, &lv_font_icons_18, 0);
                lv_obj_set_style_text_color(icon_lbl, icon_color_for_row(row), 0);
                // Pivot na środku ikony — wymagany do animacji obrotu wentylator
                if (row->kind == EntityKind::Switch && strcmp(row->domain, "fan") == 0) {
                    lv_obj_set_style_transform_pivot_x(icon_lbl, 9, 0);
                    lv_obj_set_style_transform_pivot_y(icon_lbl, 9, 0);
                }
                s_icon_labels[screen_idx][tile_idx][r] = icon_lbl;
            } else {
                s_icon_labels[screen_idx][tile_idx][r] = nullptr;
            }

            lv_obj_t *lbl = lv_label_create(cell);
            s_row_labels[screen_idx][tile_idx][r] = lbl;

            char initial[48] = {};
            if (row->unit[0] != '\0')
                snprintf(initial, sizeof(initial), "--%s", row->unit);
            else
                strlcpy(initial, "--", sizeof(initial));

            lv_label_set_text(lbl, initial);
            lv_obj_set_style_text_font(lbl, &lv_font_polish_18, 0);
            lv_obj_set_style_text_color(lbl,
                lv_color_hex(row->kind == EntityKind::Switch ? kColorAccent2 : kColorAccent), 0);

            // Brak przyciskow, trendu i arc w trybie poziomym (brak miejsca)
            s_switch_btns[screen_idx][tile_idx][r]    = nullptr;
            s_switch_circles[screen_idx][tile_idx][r] = nullptr;
            s_trend_labels[screen_idx][tile_idx][r]   = nullptr;
            s_arc_widgets[screen_idx][tile_idx][r]    = nullptr;
            s_device_dots[screen_idx][tile_idx][r]    = nullptr;
        }
        return;  // koniec budowy karty poziomej
    }

    // ── UKŁAD PIONOWY (domyslny) ─────────────────────────────────────────────
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_gap(card, 7, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Naglowek kafelka — amber/zloty kolor odróznia od zwyklego tekstu
    lv_obj_t *header = lv_label_create(card);
    lv_label_set_text(header, tile->label);
    lv_obj_set_style_text_font(header, &lv_font_polish_18, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(kColorAccent2), 0);
    lv_label_set_long_mode(header, LV_LABEL_LONG_DOT);
    lv_obj_set_width(header, LV_PCT(100));

    // Wiersze encji
    for (size_t r = 0; r < tile->row_count; ++r) {
        const HaRow *row = &tile->rows[r];
        const char *icon_glyph = icon_for_row(row);

        // ════════════════════════════════════════════════════════════════════
        // DEVICE — tylko kolo koloru (bez tekstu statusu)
        // ════════════════════════════════════════════════════════════════════
        if (row->kind == EntityKind::Device) {
            lv_obj_t *row_cont = lv_obj_create(card);
            lv_obj_remove_style_all(row_cont);
            lv_obj_set_size(row_cont, LV_PCT(100), 38);
            lv_obj_set_flex_flow(row_cont, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row_cont, 6, 0);

            // Etykieta nazwy encji (lewa strona)
            lv_obj_t *dev_name = lv_label_create(row_cont);
            lv_label_set_text(dev_name, row->label);
            lv_obj_set_style_text_font(dev_name, &lv_font_polish_18, 0);
            lv_obj_set_style_text_color(dev_name, lv_color_hex(kColorAccent2), 0);
            lv_label_set_long_mode(dev_name, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(dev_name, 1);

            // Kolo statusu (prawa strona) — szare dopóki nie przyjdzie wartosc
            lv_obj_t *dot = lv_obj_create(row_cont);
            s_device_dots[screen_idx][tile_idx][r] = dot;
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 22, 22);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x4A5568), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

            s_row_labels[screen_idx][tile_idx][r]    = nullptr;
            s_icon_labels[screen_idx][tile_idx][r]   = nullptr;
            s_switch_btns[screen_idx][tile_idx][r]   = nullptr;
            s_switch_circles[screen_idx][tile_idx][r]= nullptr;
            s_trend_labels[screen_idx][tile_idx][r]  = nullptr;
            s_arc_widgets[screen_idx][tile_idx][r]   = nullptr;
            continue;
        }

        // ════════════════════════════════════════════════════════════════════
        // SENSOR w trybie ARC — wskaznik kolowy
        // ════════════════════════════════════════════════════════════════════
        if (row->kind == EntityKind::Sensor && row->display_mode == RowDisplayMode::Arc) {
            lv_obj_t *arc_cont = lv_obj_create(card);
            lv_obj_remove_style_all(arc_cont);
            lv_obj_set_size(arc_cont, LV_PCT(100), 88);
            lv_obj_set_flex_flow(arc_cont, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(arc_cont, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(arc_cont, 8, 0);

            // Ikona po lewej (jesli dostepna)
            if (icon_glyph[0] != '\0') {
                lv_obj_t *icon_lbl = lv_label_create(arc_cont);
                lv_label_set_text(icon_lbl, icon_glyph);
                lv_obj_set_style_text_font(icon_lbl, &lv_font_icons_18, 0);
                lv_obj_set_style_text_color(icon_lbl, icon_color_for_row(row), 0);
                lv_obj_set_style_bg_opa(icon_lbl, LV_OPA_TRANSP, 0);
                s_icon_labels[screen_idx][tile_idx][r] = icon_lbl;
            } else {
                s_icon_labels[screen_idx][tile_idx][r] = nullptr;
            }

            // Kontener wskaznika (72x72) — dzieci moga sie nakladac
            lv_obj_t *arc_wrap = lv_obj_create(arc_cont);
            lv_obj_remove_style_all(arc_wrap);
            lv_obj_set_size(arc_wrap, 72, 72);
            lv_obj_clear_flag(arc_wrap, LV_OBJ_FLAG_SCROLLABLE);

            // Wskaznik kolowy
            lv_obj_t *arc = lv_arc_create(arc_wrap);
            s_arc_widgets[screen_idx][tile_idx][r] = arc;
            lv_obj_set_size(arc, 72, 72);
            lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_pad_all(arc, 0, 0);
            lv_arc_set_bg_angles(arc, 135, 45);  // 270° jak speedometr

            int16_t arc_min, arc_max;
            arc_range_for_sensor(row->sensor_type, &arc_min, &arc_max);
            lv_arc_set_range(arc, arc_min, arc_max);
            lv_arc_set_value(arc, arc_min);

            // Szare tlo piers cionka
            lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A3A4A), LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 7, LV_PART_MAIN);
            // Kolorowy wskaznik
            lv_obj_set_style_arc_color(arc, icon_color_for_row(row), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
            // Ukryj galbke — sam wyswietlacz
            lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

            // Wartosc centrowana nad wskaznikiem
            lv_obj_t *val_lbl = lv_label_create(arc_wrap);
            s_row_labels[screen_idx][tile_idx][r] = val_lbl;
            lv_label_set_text(val_lbl, "--");
            lv_obj_set_style_text_font(val_lbl, &lv_font_polish_18, 0);
            lv_obj_set_style_text_color(val_lbl, lv_color_hex(kColorText), 0);
            lv_obj_set_style_bg_opa(val_lbl, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, 0);

            // Etykieta nazwy po prawej
            lv_obj_t *name_lbl = lv_label_create(arc_cont);
            lv_label_set_text(name_lbl, row->label);
            lv_obj_set_style_text_font(name_lbl, &lv_font_polish_18, 0);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(kColorAccent2), 0);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_flex_grow(name_lbl, 1);

            s_switch_btns[screen_idx][tile_idx][r]    = nullptr;
            s_switch_circles[screen_idx][tile_idx][r] = nullptr;
            s_trend_labels[screen_idx][tile_idx][r]   = nullptr;
            s_device_dots[screen_idx][tile_idx][r]    = nullptr;
            continue;
        }

        // ════════════════════════════════════════════════════════════════════
        // STANDARDOWY WIERSZ — tekst (Sensor / Switch / Presence)
        // ════════════════════════════════════════════════════════════════════
        lv_obj_t *row_cont = lv_obj_create(card);
        lv_obj_remove_style_all(row_cont);
        lv_obj_set_size(row_cont, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_cont, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row_cont, 6, 0);

        // Switch: wiekszy padding + zaokraglenie + dotykowy efekt nacisku
        if (row->kind == EntityKind::Switch) {
            lv_obj_set_style_pad_all(row_cont, 8, 0);
            lv_obj_set_style_radius(row_cont, 10, 0);
            lv_obj_set_style_bg_color(row_cont, lv_color_hex(0x1C3040), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(row_cont, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_flag(row_cont, LV_OBJ_FLAG_CLICKABLE);
            const uint32_t packed = ((uint32_t)screen_idx << 8)
                                  | ((uint32_t)tile_idx   << 4)
                                  | (uint32_t)r;
            lv_obj_add_event_cb(row_cont, switch_event_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)packed);
        }

        // Kolorowa ikona — Switch startuje szary (OFF), sensor/presence normalny kolor
        if (icon_glyph[0] != '\0') {
            lv_obj_t *icon_lbl = lv_label_create(row_cont);
            lv_label_set_text(icon_lbl, icon_glyph);
            lv_obj_set_style_text_font(icon_lbl, &lv_font_icons_18, 0);
            // Switch: ikona szara na starcie (zanim przyjdzie pierwsza wartosc)
            lv_obj_set_style_text_color(icon_lbl,
                row->kind == EntityKind::Switch
                    ? lv_color_hex(kColorMuted)
                    : icon_color_for_row(row), 0);
            lv_obj_set_style_bg_opa(icon_lbl, LV_OPA_TRANSP, 0);
            // Pivot na środku ikony — wymagany do animacji obrotu wentylator
            if (row->kind == EntityKind::Switch && strcmp(row->domain, "fan") == 0) {
                lv_obj_set_style_transform_pivot_x(icon_lbl, 9, 0);
                lv_obj_set_style_transform_pivot_y(icon_lbl, 9, 0);
            }
            s_icon_labels[screen_idx][tile_idx][r] = icon_lbl;
        } else {
            s_icon_labels[screen_idx][tile_idx][r] = nullptr;
        }

        lv_obj_t *lbl = lv_label_create(row_cont);
        s_row_labels[screen_idx][tile_idx][r] = lbl;

        char initial[96] = {};
        if (row->unit[0] != '\0')
            snprintf(initial, sizeof(initial), "%s: -- %s", row->label, row->unit);
        else
            snprintf(initial, sizeof(initial), "%s: --", row->label);

        lv_label_set_text(lbl, initial);

        // Wszystkie wiersze startuja teal; switch zmieni sie po pierwszej wartosci
        style_value_label(lbl, kColorAccent);
        lv_obj_set_style_flex_grow(lbl, 1, 0);

        // Sensor/Presence: strzalka trendu (dziecko etykiety, wyrownana do prawej)
        if (row->kind != EntityKind::Switch) {
            lv_obj_t *trend = lv_label_create(lbl);
            s_trend_labels[screen_idx][tile_idx][r] = trend;
            lv_label_set_text(trend, "");
            lv_obj_set_style_text_font(trend, &lv_font_montserrat_20, 0);
            lv_obj_set_style_bg_opa(trend, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(trend, LV_OPA_TRANSP, 0);
            lv_obj_set_size(trend, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_align(trend, LV_ALIGN_RIGHT_MID, -8, 0);
        } else {
            s_trend_labels[screen_idx][tile_idx][r] = nullptr;
        }

        // Switch nie ma osobnego przycisku — wiersz jest dotykowy (patrz wyzej)
        s_switch_btns[screen_idx][tile_idx][r]    = nullptr;
        s_switch_circles[screen_idx][tile_idx][r] = nullptr;
        s_device_dots[screen_idx][tile_idx][r]    = nullptr;
        s_arc_widgets[screen_idx][tile_idx][r]    = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Zawartosc jednego ekranu (siatka kafelkow)
// ---------------------------------------------------------------------------
static void make_screen_content(lv_obj_t *tab_content, size_t screen_idx)
{
    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen) return;
    ESP_LOGI(TAG, "make_screen_content: screen=%u name=%s tiles=%u",
             (unsigned)screen_idx, screen->name, (unsigned)screen->tile_count);

    lv_obj_set_style_bg_color(tab_content, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(tab_content, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(tab_content, LV_SCROLLBAR_MODE_AUTO);

    // Jesli pierwszy kafelek ma jawna pozycje — tryb absolutny (bez flex)
    const bool absolute_mode = (screen->tile_count > 0 &&
                                 screen->tiles[0].x >= 0);
    if (absolute_mode) {
        lv_obj_set_style_pad_all(tab_content, 0, 0);
        // Bez flex — kazdy kafelek pozycjonowany przez lv_obj_set_pos()
    } else {
        lv_obj_set_style_pad_all(tab_content, 16, 0);
        lv_obj_set_style_pad_gap(tab_content, 14, 0);
        lv_obj_set_flex_flow(tab_content, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(tab_content, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }
    lv_obj_set_style_bg_color(tab_content, lv_color_hex(kColorAccent),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tab_content, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(tab_content, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_border_width(tab_content, 0, LV_PART_SCROLLBAR);

    if (screen->tile_count == 0) {
        lv_obj_t *empty = lv_label_create(tab_content);
        lv_label_set_text(empty, "No tiles configured. Set up in web panel.");
        lv_obj_set_style_text_font(empty, &lv_font_polish_22, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(kColorMuted), 0);
        return;
    }

    for (size_t t = 0; t < screen->tile_count; ++t) {
        make_tile_card(tab_content, screen_idx, t);
    }
}

// ---------------------------------------------------------------------------
// Glowna funkcja tworzaca UI
// ---------------------------------------------------------------------------
void panel_ui_create(UiCommandCallback command_callback)
{
    s_command_callback = command_callback;

    // Fallback ikon ustawiony statycznie w plikach .c fontow (polish -> icons_18 -> montserrat)
    memset(s_row_labels,     0, sizeof(s_row_labels));
    memset(s_tile_cards,     0, sizeof(s_tile_cards));
    memset(s_switch_btns,    0, sizeof(s_switch_btns));
    memset(s_switch_circles, 0, sizeof(s_switch_circles));
    memset(s_switch_states,  0, sizeof(s_switch_states));
    memset(s_icon_labels,    0, sizeof(s_icon_labels));
    memset(s_trend_labels,   0, sizeof(s_trend_labels));
    memset(s_arc_widgets,    0, sizeof(s_arc_widgets));
    memset(s_device_dots,    0, sizeof(s_device_dots));
    memset(s_prev_num_value, 0, sizeof(s_prev_num_value));
    memset(s_has_num_value,  0, sizeof(s_has_num_value));
    memset(s_trend_dir,      0, sizeof(s_trend_dir));   // TrendDir::None = 0
    memset(s_trend_ts,       0, sizeof(s_trend_ts));

    // Timer co 60s sprawdzający czy któraś wartość ustabilizowała się na 30 minut
    s_trend_timer = lv_timer_create(trend_timer_cb, 60000, nullptr);

    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    make_status_bar(root);

    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, nullptr);
    clock_timer_cb(nullptr);

    if (ha_screen_count() == 0) {
        lv_obj_t *empty = lv_label_create(root);
        lv_label_set_text(empty,
            "No screens configured. Set up dashboard in web panel.");
        lv_obj_set_style_text_font(empty, &lv_font_polish_22, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(kColorMuted), 0);
        lv_obj_set_style_pad_all(empty, 20, 0);
        return;
    }

    s_tabview = lv_tabview_create(root);
    lv_obj_set_flex_grow(s_tabview, 1);
    lv_obj_set_width(s_tabview, LV_PCT(100));
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(s_tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tabview, 0, 0);
    lv_obj_set_style_pad_all(s_tabview, 0, 0);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, 44);

    // Kontener trésci (rodzic wszystkich tabów) — musi byc ciemny
    // UWAGA: NIE ustawiamy pad_all na tv_content — to jest wewnetrzny lv_tileview
    // i zmiana paddingu powoduje bledne obliczenia scroll range / layout loop w LVGL 9.
    lv_obj_t *tv_content = lv_tabview_get_content(s_tabview);
    if (tv_content) {
        lv_obj_set_style_bg_color(tv_content, lv_color_hex(kColorBackground), 0);
        lv_obj_set_style_bg_opa(tv_content, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tv_content, 0, 0);
    }

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(kColorHeader), 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_border_width(tab_bar, 1, 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_polish_18, 0);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(kColorAccent), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(kColorAccent),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);

    for (size_t s = 0; s < ha_screen_count(); ++s) {
        const HaScreen *screen = ha_screen_get(s);
        lv_obj_t *tab = lv_tabview_add_tab(s_tabview,
                                            screen ? screen->name : "?");
        make_screen_content(tab, s);
    }

    lv_tabview_set_act(s_tabview, (uint32_t)ha_default_screen(), LV_ANIM_OFF);
    ESP_LOGI(TAG, "panel_ui_create done: %u screens, default=%u",
             (unsigned)ha_screen_count(), (unsigned)ha_default_screen());
}

// ---------------------------------------------------------------------------
// Aktualizacja stanu wiersza
// ---------------------------------------------------------------------------
void panel_ui_update_row(size_t screen_idx, size_t tile_idx,
                         size_t row_idx, const char *value, bool online)
{
    if (screen_idx >= kHaMaxScreens || tile_idx >= kHaMaxTiles ||
        row_idx >= kHaMaxRows)
        return;

    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen || tile_idx >= screen->tile_count) return;
    if (row_idx >= screen->tiles[tile_idx].row_count) return;
    const HaRow *row = &screen->tiles[tile_idx].rows[row_idx];

    lv_obj_t *lbl = s_row_labels[screen_idx][tile_idx][row_idx];
    // Uwaga: lbl moze byc nullptr dla Device / Arc (sprawdzamy nizej)

    const bool is_switch   = (row->kind == EntityKind::Switch);
    const bool is_presence = (row->kind == EntityKind::Presence);
    const bool is_device   = (row->kind == EntityKind::Device);
    const bool is_arc      = (row->kind == EntityKind::Sensor &&
                               row->display_mode == RowDisplayMode::Arc);
    bool is_on = false;
    if (is_switch) {
        is_on = payload_is_on(value);
        s_switch_states[screen_idx][tile_idx][row_idx] = is_on;
    }

    board_display_lock();

    // ── Device: aktualizuj tylko kolo koloru ────────────────────────────────
    if (is_device) {
        lv_obj_t *dot = s_device_dots[screen_idx][tile_idx][row_idx];
        if (dot) {
            const bool active = value && (
                strcmp(value, "home")        == 0 ||
                strcasecmp(value, "on")      == 0 ||
                strcasecmp(value, "true")    == 0 ||
                strcmp(value, "active")      == 0
            );
            lv_obj_set_style_bg_color(dot,
                lv_color_hex(active ? 0x22C55E : 0x4A5568), 0);
        }
        lv_obj_t *card = s_tile_cards[screen_idx][tile_idx];
        if (card) {
            lv_obj_set_style_bg_color(card,
                online ? lv_color_hex(kColorCard) : lv_color_hex(kColorCardOffline), 0);
            lv_obj_set_style_border_color(card,
                online ? lv_color_hex(kColorAccent) : lv_color_hex(kColorDanger), 0);
        }
        board_display_unlock();
        return;
    }

    // ── Arc sensor: aktualizuj wskaznik kolowy ───────────────────────────────
    if (is_arc) {
        lv_obj_t *arc = s_arc_widgets[screen_idx][tile_idx][row_idx];
        if (arc) {
            float fval;
            const bool has_val = value && parse_first_float(value, &fval);
            if (has_val) {
                lv_arc_set_value(arc, (int16_t)fval);
                lv_obj_t *val_lbl = s_row_labels[screen_idx][tile_idx][row_idx];
                if (val_lbl) {
                    char buf[24];
                    if (row->unit[0])
                        snprintf(buf, sizeof(buf), "%.1f%s", fval, row->unit);
                    else
                        snprintf(buf, sizeof(buf), "%.1f", fval);
                    lv_label_set_text(val_lbl, buf);
                    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, 0);
                }
                // Dynamiczny kolor wskaznika dla temperatury / wilgotnosci
                if (row->sensor_type == SensorType::Temperature ||
                    row->sensor_type == SensorType::Humidity) {
                    lv_obj_set_style_arc_color(arc,
                        icon_color_for_value(row->sensor_type, fval),
                        LV_PART_INDICATOR);
                }
            }
        }
        lv_obj_t *card = s_tile_cards[screen_idx][tile_idx];
        if (card) {
            lv_obj_set_style_bg_color(card,
                online ? lv_color_hex(kColorCard) : lv_color_hex(kColorCardOffline), 0);
            lv_obj_set_style_border_color(card,
                online ? lv_color_hex(kColorAccent) : lv_color_hex(kColorDanger), 0);
        }
        board_display_unlock();
        return;
    }

    // ── Tekstowy tryb (Switch / Presence / Sensor) ───────────────────────────
    if (!lbl) { board_display_unlock(); return; }

    const bool compact = (screen->tiles[tile_idx].layout == TileLayout::Horizontal);
    char text[96] = {};
    format_row_value(row, value, text, sizeof(text), compact);

    lv_label_set_text(lbl, text);

    if (is_presence) {
        // Kolory: home=zielony, not_home=szary (wyszarzony), strefa=pomaranczowy
        uint32_t col;
        if      (value && strcmp(value, "home")     == 0) col = 0x22C55E; // zielony
        else if (value && strcmp(value, "not_home") == 0) col = kColorMuted; // szary
        else                                               col = 0xF6B84A; // pomaranczowy (np. "praca")
        lv_obj_set_style_text_color(lbl, lv_color_hex(col), 0);
        sensor_label_flash(lbl);
    } else if (is_switch) {
        // Kolor etykiety: ON=zielony, OFF=teal + krotki blask
        switch_label_flash(lbl, is_on);

        // Ikona: ON=kolor domenowy (zolta zarowka, niebieski wentylator itp.), OFF=szara
        lv_obj_t *sw_icon = s_icon_labels[screen_idx][tile_idx][row_idx];
        if (sw_icon) {
            // Przywroc pelna widocznosc (bulb_off moglby przyciemnic)
            lv_obj_set_style_opa(sw_icon, LV_OPA_COVER, 0);
            lv_anim_delete(sw_icon, nullptr);

            const char *d = row->domain;
            if (is_on) {
                // ON: kolor ikony wg domeny + animacja dla zarowki i wentylatora
                lv_obj_set_style_text_color(sw_icon, icon_color_for_row(row), 0);
                if      (strcmp(d, "light") == 0) icon_anim_bulb_on(sw_icon);
                else if (strcmp(d, "fan")   == 0) icon_anim_fan_on(sw_icon);
            } else {
                // OFF: szara ikona, brak animacji
                lv_obj_set_style_text_color(sw_icon, lv_color_hex(kColorMuted), 0);
                if (strcmp(d, "fan") == 0) icon_anim_stop(sw_icon);
                // zarowka: szara opacita pełna (nie dimujemy — kolor wystarczy)
            }
        }
    } else {
        // Sensor: krotki blask teal na etykiecie
        sensor_label_flash(lbl);

        // Strzalka trendu: porownaj z poprzednia wartoscia, ustaw kierunek na stale
        float new_val;
        if (parse_first_float(value, &new_val)) {
            lv_obj_t  *trend = s_trend_labels[screen_idx][tile_idx][row_idx];
            TrendDir  &dir   = s_trend_dir[screen_idx][tile_idx][row_idx];
            time_t    &ts    = s_trend_ts[screen_idx][tile_idx][row_idx];

            if (!s_has_num_value[screen_idx][tile_idx][row_idx]) {
                // Pierwsza wartosc: zapamiętaj, poczekaj na kolejną do porownania
                s_has_num_value[screen_idx][tile_idx][row_idx] = true;
                dir = TrendDir::None;
                time(&ts);
            } else {
                const float prev = s_prev_num_value[screen_idx][tile_idx][row_idx];
                time_t now;
                time(&now);

                if (new_val > prev + 0.05f) {
                    dir = TrendDir::Up;
                    time(&ts);
                } else if (new_val < prev - 0.05f) {
                    dir = TrendDir::Down;
                    time(&ts);
                } else if (now > 1577836800 &&
                           (now - ts) >= kTrendStableSec) {
                    // Brak zmiany przez 30 minut → stabilna
                    dir = TrendDir::Stable;
                }
                // else: wartosc taka sama ale <30min → zachowaj poprzedni kierunek
            }

            s_prev_num_value[screen_idx][tile_idx][row_idx] = new_val;
            update_trend_label(trend, dir);

            // Dynamiczna zmiana koloru ikony (temperatura / wilgotnosc)
            lv_obj_t *icon_lbl = s_icon_labels[screen_idx][tile_idx][row_idx];
            if (icon_lbl && (row->sensor_type == SensorType::Temperature ||
                             row->sensor_type == SensorType::Humidity)) {
                lv_obj_set_style_text_color(icon_lbl,
                    icon_color_for_value(row->sensor_type, new_val), 0);
            }
        }
    }

    // Karta: tlo i lewa krawedz zalezne od stanu online/offline
    lv_obj_t *card = s_tile_cards[screen_idx][tile_idx];
    if (card) {
        lv_obj_set_style_bg_color(card,
            online ? lv_color_hex(kColorCard) : lv_color_hex(kColorCardOffline), 0);
        lv_obj_set_style_border_color(card,
            online ? lv_color_hex(kColorAccent) : lv_color_hex(kColorDanger), 0);
    }

    board_display_unlock();
}

// ---------------------------------------------------------------------------
// Status bary
// ---------------------------------------------------------------------------
void panel_ui_set_wifi_status(const char *text)
{
    if (!s_wifi_label) return;
    board_display_lock();
    lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: %s", text);
    board_display_unlock();
}

void panel_ui_set_ha_status(const char *text)
{
    if (!s_mqtt_label) return;
    board_display_lock();
    lv_label_set_text_fmt(s_mqtt_label, "HA: %s", text);
    board_display_unlock();
}

void panel_ui_set_battery_status(const char *text)
{
    if (!s_battery_label) return;
    board_display_lock();
    lv_label_set_text_fmt(s_battery_label, "BAT: %s", text);
    board_display_unlock();
}
