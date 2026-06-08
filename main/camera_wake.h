#pragma once

#include "esp_err.h"

/**
 * @brief Inicjalizuje kamerę SC2336 (MIPI CSI) i uruchamia task detekcji twarzy.
 *
 * Task w tle:
 *   - pobiera klatkę 640×480 RGB565 co ~1s
 *   - uruchamia HumanFaceDetect (esp-dl)
 *   - gdy twarz wykryta → board_display_notify_activity() budzi ekran
 *
 * Błąd inicjalizacji jest nieśmiertelny — reszta systemu działa normalnie.
 *
 * @return ESP_OK, lub kod błędu jeśli kamera niedostępna (system kontynuuje).
 */
esp_err_t camera_wake_init(void);
