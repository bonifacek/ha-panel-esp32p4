#pragma once

#include "ha_entities.h"
#include <stddef.h>

// Callback wywolywany gdy uzytkownik kliknie przycisk na wierszu switch
using UiCommandCallback = void (*)(size_t screen_idx, size_t tile_idx,
                                   size_t row_idx, bool turn_on);

// Tworzy caly interfejs (status bar + tabview z ekranami)
void panel_ui_create(UiCommandCallback command_callback);

// Aktualizuje stan wiersza (text + kolor kafelka)
void panel_ui_update_row(size_t screen_idx, size_t tile_idx,
                         size_t row_idx, const char *value, bool online);

// Status bary
void panel_ui_set_wifi_status(const char *text);
void panel_ui_set_ha_status(const char *text);
void panel_ui_set_battery_status(const char *text);
