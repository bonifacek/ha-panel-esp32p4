#pragma once
#include "esp_err.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Harmonogram głośności głośnika
// Każdy slot definiuje przedział godzinowy + dni tygodnia → głośność.
// Sloty sprawdzane od indeksu 0, wygrywa pierwszy pasujący.
// Gdy żaden slot nie pasuje → default_volume.
// ---------------------------------------------------------------------------

#define SCHED_MAX_SLOTS 8

// bit0=Pn, bit1=Wt, bit2=Śr, bit3=Cz, bit4=Pt, bit5=Sb, bit6=Nd
// Dopasowanie do tm_wday: 0=Nd→bit6, 1=Pn→bit0, ..., 6=Sb→bit5
#define SCHED_ALL_DAYS  0x7F   // wszystkie 7 dni

struct VolumeSlot {
    uint8_t active;     // 0 = wyłączony, 1 = aktywny
    uint8_t days_mask;  // bitmaska dni tygodnia (patrz wyżej)
    uint8_t hour_from;  // godzina początku (0-23)
    uint8_t min_from;   // minuta początku (0-59)
    uint8_t hour_to;    // godzina końca (0-23)
    uint8_t min_to;     // minuta końca (0-59)
    uint8_t volume;     // głośność 0-100 (0 = cisza / mute)
};

struct SpeakerSchedule {
    uint8_t    default_volume;          // głośność gdy brak pasującego slotu
    uint8_t    slot_count;              // liczba zdefiniowanych slotów (0..8)
    VolumeSlot slots[SCHED_MAX_SLOTS];
};

// Ładuje harmonogram z NVS (lub ustawia wartości domyślne gdy brak wpisu).
// Aktualizuje wewnętrzną kopię statyczną używaną przez sched_get_volume().
esp_err_t sched_load(SpeakerSchedule *out);   // out może być nullptr

// Zapisuje harmonogram do NVS i aktualizuje wewnętrzną kopię.
esp_err_t sched_save(const SpeakerSchedule *s);

// Parsuje JSON (format { dv, slots:[{a,d,hf,mf,ht,mt,v},...] }) → struct + NVS.
esp_err_t sched_save_json(const char *json);

// Serializuje wewnętrzną kopię do JSON (cJSON_free() po użyciu!)
char *sched_to_json(void);

// Zwraca głośność dla bieżącej chwili (używa lokalnego czasu + harmonogramu).
// Gdy NTP niezsynch → zwraca default_volume.
uint8_t sched_get_volume(void);
