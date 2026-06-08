#pragma once
#include "esp_err.h"
#include <stdint.h>

// Predefiniowane dzwieki panelu
enum class SpeakerSound : uint8_t {
    Beep  = 0,   // Krotki pisk — potwierdzenie dotyku przycisku switch
    Chime = 1,   // Dwunutowy dzwon — HA polaczony / powiadomienie
    Alert = 2,   // Narastajacy alert — trzy tony (A4 C5 E5)
    Error = 3,   // Opadajacy blad — dwa tony (E5 A4)
};

// Inicjalizacja (wywolac raz przy starcie gdy hw_features.speaker = true)
esp_err_t speaker_init(void);

// Odtwarzanie predefiniowanego dzwieku — nieblokujace (task w tle)
// Jesli kolejka pelna, zgloszenie jest ignorowane (nie blokuje UI)
void speaker_play(SpeakerSound sound);

// Ustawienie glosnosci 0-100 (domyslnie 70)
void speaker_set_volume(uint8_t vol_0_100);

// ── TTS: wylaczne uzycie kodeka przez zewnetrzny task ──────────────────────
// Blokuje az kodek bedzie wolny (timeout_ms=0 → czeka bez limitu).
// Zwraca uchwyt lub nullptr przy timeout.
// MUSI byc sparowane z speaker_unlock_dev() po zakonczeniu!
struct SpeakerDevLease {
    void *dev;          // esp_codec_dev_handle_t (void* unika wciagania naglowkow)
    bool  valid;
};
SpeakerDevLease speaker_lock_dev(uint32_t timeout_ms);
void            speaker_unlock_dev(void);
