#pragma once
#include "esp_err.h"
#include "ha_config.h"

// ---------------------------------------------------------------------------
// Moduł TTS — pobiera audio z HA (Piper/Google TTS) i odtwarza przez ES8311
//
// Wymagania po stronie HA:
//   - Skonfigurowany silnik TTS (zalecany: Piper — lokalny, WAV, język PL)
//   - rest_command w configuration.yaml (patrz: README_TTS.md)
//   - Long-Lived Token z dostępem do tts_get_url
//
// Przepływ:
//   HA Automation → POST /api/speak → tts_task
//   tts_task → POST {ha}/api/tts_get_url → URL
//   tts_task → GET {url} + Bearer → WAV stream → ES8311
// ---------------------------------------------------------------------------

// Inicjalizacja — wywołać po speaker_init(), jeśli hw_features.speaker = true
// ha_cfg:      konfiguracja HA (URL WebSocket + token)
// engine_id:   np. "tts.piper" lub "tts.google_translate_en_com"
// language:    np. "pl-PL"
esp_err_t tts_init(const HaRuntimeConfig *ha_cfg,
                   const char *engine_id,
                   const char *language);

// Odtworzenie tekstu przez TTS — nieblokujące (task w tle)
// Gdy HA niedostępne lub brak konfiguracji: loguje błąd i zwraca błąd.
esp_err_t tts_speak(const char *text);

// Odtworzenie bezpośrednio z URL (WAV) — nieblokujące
// Przydatne do testów lub gdy HA samo zwróci URL w automatyzacji.
esp_err_t tts_play_url(const char *url);

// Konfiguracja silnika TTS — zapisywana w NVS, natychmiastowa zmiana
// voice: np. "pl_PL-gosia-medium" dla Piper; "" = brak (HA użyje domyślnego)
esp_err_t tts_set_engine(const char *engine_id, const char *language,
                          const char *voice);
void      tts_get_engine(char *engine_id, size_t eid_len,
                          char *language,  size_t lang_len,
                          char *voice,     size_t voice_len);
