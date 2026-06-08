#pragma once

#include "esp_err.h"

struct BatteryReading {
    bool valid;
    int millivolts;
    int percent;
};

esp_err_t battery_monitor_init(void);
BatteryReading battery_monitor_read(void);
