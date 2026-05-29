#include "panel_ui.h"

#include "board_display.h"
#include "lvgl.h"
#include "polish_fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

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
static void switch_label_flash(lv_obj_t *lbl, bool is_on)
{
    lv_obj_set_style_bg_color(lbl, lv_color_hex(is_on ? kColorSwitchOnBg : kColorSwitchOffBg), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(is_on ? kColorAccent : kColorDanger), 0);
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
    if (row->kind == EntityKind::Switch) return ICON_PLUG;

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
    if (row->kind == EntityKind::Switch) return lv_color_hex(0x81C784); // zielony — wtyczka

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
        h += 38;
        if (tile->rows[r].kind == EntityKind::Switch)
            h += 48;
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

            if (icon_glyph[0] != '\0') {
                lv_obj_t *icon_lbl = lv_label_create(cell);
                lv_label_set_text(icon_lbl, icon_glyph);
                lv_obj_set_style_text_font(icon_lbl, &lv_font_icons_18, 0);
                lv_obj_set_style_text_color(icon_lbl, icon_color_for_row(row), 0);
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

            // Brak przyciskow i trendu w trybie poziomym (brak miejsca)
            s_switch_btns[screen_idx][tile_idx][r]  = nullptr;
            s_trend_labels[screen_idx][tile_idx][r] = nullptr;
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

        // ── Kontener wiersza: [kolorowa ikona] + [etykieta wartosci] ─────────
        const char *icon_glyph = icon_for_row(row);
        lv_obj_t *row_cont = lv_obj_create(card);
        lv_obj_remove_style_all(row_cont);
        lv_obj_set_size(row_cont, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_cont, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row_cont, 6, 0);

        // Kolorowa ikona — widoczna tylko jesli encja ma przypisana ikone
        if (icon_glyph[0] != '\0') {
            lv_obj_t *icon_lbl = lv_label_create(row_cont);
            lv_label_set_text(icon_lbl, icon_glyph);
            lv_obj_set_style_text_font(icon_lbl, &lv_font_icons_18, 0);
            lv_obj_set_style_text_color(icon_lbl, icon_color_for_row(row), 0);
            lv_obj_set_style_bg_opa(icon_lbl, LV_OPA_TRANSP, 0);
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

        // Switch: amber tekst (brak danych), sensor: teal tekst
        style_value_label(lbl, row->kind == EntityKind::Switch
                               ? kColorAccent2 : kColorAccent);
        lv_obj_set_style_flex_grow(lbl, 1, 0);  // zajmij resztę szerokości kontenera

        // Sensor: strzalka trendu (dziecko etykiety, wyrownana do prawej)
        if (row->kind != EntityKind::Switch) {
            lv_obj_t *trend = lv_label_create(lbl);
            s_trend_labels[screen_idx][tile_idx][r] = trend;
            lv_label_set_text(trend, "");
            lv_obj_set_style_text_font(trend, &lv_font_montserrat_20, 0);
            lv_obj_set_style_bg_opa(trend, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(trend, LV_OPA_TRANSP, 0);  // poczatkowo niewidoczna
            lv_obj_set_size(trend, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_align(trend, LV_ALIGN_RIGHT_MID, -8, 0);
        }

        if (row->kind == EntityKind::Switch) {
            lv_obj_t *btn = lv_btn_create(card);
            s_switch_btns[screen_idx][tile_idx][r] = btn;

            // Pełna szerokość, wyższy (pill toggle)
            lv_obj_set_size(btn, LV_PCT(100), 52);
            lv_obj_set_style_radius(btn, 12, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(kColorSwitchOffBtn), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(kColorSwitchOffPress), LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_hor(btn, 14, 0);
            lv_obj_set_style_pad_ver(btn, 0, 0);
            // Flex row: kółko + tekst
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn, 12, 0);

            // Kółko wskaźnika stanu (szare = brak danych)
            lv_obj_t *circle = lv_obj_create(btn);
            s_switch_circles[screen_idx][tile_idx][r] = circle;
            lv_obj_remove_style_all(circle);
            lv_obj_set_size(circle, 16, 16);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0x3A5060), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

            // Etykieta stanu
            lv_obj_t *btn_lbl = lv_label_create(btn);
            lv_label_set_text(btn_lbl, "-- brak danych --");
            lv_obj_set_style_text_font(btn_lbl, &lv_font_polish_18, 0);
            lv_obj_set_style_text_color(btn_lbl, lv_color_hex(kColorMuted), 0);
            lv_obj_set_flex_grow(btn_lbl, 1);
            lv_obj_set_style_text_align(btn_lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(btn_lbl, LV_LABEL_LONG_DOT);
            lv_obj_clear_flag(btn_lbl, LV_OBJ_FLAG_CLICKABLE);

            const uint32_t packed = ((uint32_t)screen_idx << 8)
                                  | ((uint32_t)tile_idx   << 4)
                                  | (uint32_t)r;
            lv_obj_add_event_cb(btn, switch_event_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)packed);
        }
    }
}

// ---------------------------------------------------------------------------
// Zawartosc jednego ekranu (siatka kafelkow)
// ---------------------------------------------------------------------------
static void make_screen_content(lv_obj_t *tab_content, size_t screen_idx)
{
    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen) return;

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
        lv_label_set_text(empty, "Brak kafelkow. Skonfiguruj w panelu WWW.");
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
            "Brak ekranow. Skonfiguruj dashboard w panelu WWW.");
        lv_obj_set_style_text_font(empty, &lv_font_polish_22, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(kColorMuted), 0);
        lv_obj_set_style_pad_all(empty, 20, 0);
        return;
    }

    s_tabview = lv_tabview_create(root);
    lv_obj_set_flex_grow(s_tabview, 1);
    lv_obj_set_width(s_tabview, LV_PCT(100));
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, 44);

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

    lv_obj_t *lbl = s_row_labels[screen_idx][tile_idx][row_idx];
    if (!lbl) return;

    const HaScreen *screen = ha_screen_get(screen_idx);
    if (!screen || tile_idx >= screen->tile_count) return;
    if (row_idx >= screen->tiles[tile_idx].row_count) return;
    const HaRow *row = &screen->tiles[tile_idx].rows[row_idx];

    const bool is_switch = (row->kind == EntityKind::Switch);
    bool is_on = false;
    if (is_switch) {
        is_on = payload_is_on(value);
        s_switch_states[screen_idx][tile_idx][row_idx] = is_on;
    }

    const bool compact = (screen->tiles[tile_idx].layout == TileLayout::Horizontal);
    char text[96] = {};
    format_row_value(row, value, text, sizeof(text), compact);

    board_display_lock();

    lv_label_set_text(lbl, text);

    if (is_switch) {
        // Zmien kolor etykiety na wskaznik stanu ON/OFF + blask
        switch_label_flash(lbl, is_on);

        // Przycisk: aktualizuj kolor tla, kółko wskaźnika i tekst stanu
        lv_obj_t *btn = s_switch_btns[screen_idx][tile_idx][row_idx];
        if (btn) {
            lv_obj_set_style_bg_color(btn,
                lv_color_hex(is_on ? kColorSwitchOnBtn : kColorSwitchOffBtn), 0);
            lv_obj_set_style_bg_color(btn,
                lv_color_hex(is_on ? kColorSwitchOnPress : kColorSwitchOffPress),
                LV_STATE_PRESSED);

            // Kółko wskaźnika: zielone = ON, czerwone = OFF
            lv_obj_t *circle = s_switch_circles[screen_idx][tile_idx][row_idx];
            if (circle) {
                lv_obj_set_style_bg_color(circle,
                    lv_color_hex(is_on ? 0x4CD97B : 0xF07863), 0);
            }

            // Etykieta: WLACZONY / WYLACZONY (child[1] = po kółku)
            lv_obj_t *btn_lbl = lv_obj_get_child(btn, 1);
            if (btn_lbl) {
                lv_label_set_text(btn_lbl, is_on ? "WLACZONY" : "WYLACZONY");
                lv_obj_set_style_text_color(btn_lbl,
                    lv_color_hex(is_on ? 0x4CD97B : 0xF07863), 0);
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
