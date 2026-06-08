#pragma once
#include "esp_err.h"

struct HwFeatures {
    bool camera;   // detekcja ruchu / budzenie ekranu
    bool battery;  // monitor baterii (ADC)
    bool speaker;  // glosnik (zarezerwowane)
};

// Domyslne: camera=true, battery=true, speaker=false.
// Jesli klucza nie ma w NVS (pierwsze uruchomienie), uzywa domyslnych.
void      hw_features_load(HwFeatures *f);
esp_err_t hw_features_save(const HwFeatures *f);
