#pragma once

#include "lvgl.h"
#include "ha_config.h"
#include "esp_err.h"

// Inicjalizuje modul: parsuje bazowy URL HA, zapisuje token, alokuje bufory PSRAM.
// Wywolac raz po zaladowaniu konfiguracji HA.
esp_err_t cam_snap_init(const HaRuntimeConfig *cfg);

// Pobiera JPEG snapshot dla podanego entity_id i dekoduje go do bufora RGB565,
// nastepnie skaluje PPA do zadanego rozmiaru wyjsciowego.
// Wywolac z zadania (nie z LVGL task) — operacja blokujaca.
// Zwraca wskaznik na lv_img_dsc_t (wewnetrzny bufor, wazny do nastepnego wywolania)
// lub nullptr w przypadku bledu.
const lv_img_dsc_t *cam_snap_fetch(const char *entity_id,
                                    uint16_t out_w, uint16_t out_h);
