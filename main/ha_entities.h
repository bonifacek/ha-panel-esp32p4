#pragma once

#include "esp_err.h"
#include <stddef.h>

static constexpr size_t kHaMaxScreens = 8;
static constexpr size_t kHaMaxTiles   = 12;
static constexpr size_t kHaMaxRows    = 6;

enum class EntityKind { Sensor, Switch, Presence, Device };

// Sposob wyswietlania wiersza encji
enum class RowDisplayMode : uint8_t {
    Auto = 0,  // domyslny (tekst)
    Text = 1,  // zawsze tekst
    Arc  = 2,  // wskaznik kolowy (tylko dla sensorow)
};

// Typ sensora — uzywany do doboru ikony i dynamicznych kolorow
enum class SensorType : uint8_t {
    Generic     = 0,   // ogolny / nieznany
    Temperature = 1,   // temperatura (°C / °F)
    Humidity    = 2,   // wilgotnosc (%)
    Cpu         = 3,   // zuzycie procesora (%)
    Memory      = 4,   // zuzycie pamieci (%)
    Power       = 5,   // moc / energia (W, kW, kWh)
    Illuminance = 6,   // oswietlenie (lx)
};

// Jeden wiersz w kafelku — jedna encja HA
struct HaRow {
    char entity_id[64];
    char label[48];
    char attribute[32];  // "" = stan glowny ("s"), "temperature" = atrybut
    char unit[8];        // "", "°C", "%", "W", ...
    EntityKind    kind;
    SensorType    sensor_type;
    RowDisplayMode display_mode;  // sposob wyswietlania (tekst / kolo)
    char domain[24];
};

// Typ kafelka
enum class TileType : uint8_t {
    Normal = 0,  // standardowy kafelek z encjami
};

// Sposob wyswietlania kafelka
enum class TileLayout : uint8_t {
    Vertical   = 0,  // nazwa u gory, encje ponizej (domyslny)
    Horizontal = 1,  // nazwa po lewej, encje obok siebie w jednym wierszu
};

// Kafelek — agreguje wiele wierszy (encji) w jednej karcie
struct HaTile {
    char label[48];
    TileType type;            // zawsze Normal
    HaRow rows[kHaMaxRows];
    size_t row_count;
    TileLayout layout;  // sposob wyswietlania
    int16_t x;          // pozycja X w px w obszarze roboczym (-1 = auto)
    int16_t y;          // pozycja Y w px (-1 = auto)
    int16_t w;          // szerokosc w px (0 = auto)
    int16_t h;          // wysokosc w px (0 = auto)
};

// Ekran — zestaw kafelkow wyswietlanych jako jedna zakladka
struct HaScreen {
    char name[32];
    HaTile tiles[kHaMaxTiles];
    size_t tile_count;
};

// Laduje dashboard z NVS (klucz "ha_dashboard")
esp_err_t ha_dashboard_load();

// Zapisuje surowy JSON z web GUI do NVS
esp_err_t ha_dashboard_save_json(const char *json);

size_t          ha_screen_count();
size_t          ha_default_screen();
const HaScreen *ha_screen_get(size_t screen_idx);
