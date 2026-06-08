#pragma once

#include "esp_err.h"

// Home Assistant connection config
// url  – np. "ws://192.168.1.100:8123/api/websocket"
//         lub "wss://moj-ha.duckdns.org/api/websocket"
// token – Long-Lived Access Token z HA (Profil -> Tokeny)
struct HaRuntimeConfig {
    char url[128];       // WebSocket URL
    char token[256];     // Long-Lived Access Token
};

void     ha_config_load(HaRuntimeConfig *config);
esp_err_t ha_config_save(const HaRuntimeConfig *config);
bool     ha_config_ready(const HaRuntimeConfig *config);
