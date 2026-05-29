#pragma once

#include "esp_err.h"

esp_err_t board_display_init(void);
void board_display_lock(void);
void board_display_unlock(void);
