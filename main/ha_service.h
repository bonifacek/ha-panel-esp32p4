#pragma once

#include "ha_config.h"
#include "ha_entities.h"

#include <stdbool.h>
#include <stddef.h>

// Callback gdy HA wysle nowy stan encji.
// screen_idx / tile_idx / row_idx — pozycja wiersza w dashboardzie.
// value — wartosc jako string ("23.5", "on", "off", "unavailable", ...)
using HaStateCallback  = void (*)(size_t screen_idx, size_t tile_idx,
                                  size_t row_idx, const char *value);

using HaStatusCallback = void (*)(bool connected);

void ha_service_start(const HaRuntimeConfig *config,
                      HaStateCallback state_cb,
                      HaStatusCallback status_cb);

// Wysyla komende turn_on / turn_off dla wiersza (screen, tile, row)
void ha_service_send_command(size_t screen_idx, size_t tile_idx,
                             size_t row_idx, bool turn_on);

bool ha_service_connected();
