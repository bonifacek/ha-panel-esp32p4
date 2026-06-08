#include "ha_config.h"
#include "sdkconfig.h"
#include "nvs.h"
#include <string.h>

static constexpr const char *kNamespace = "supla_cfg";

void ha_config_load(HaRuntimeConfig *config)
{
    if (config == nullptr) return;
    config->url[0]   = '\0';
    config->token[0] = '\0';

    // Domyslne wartosci z menuconfig jesli sa
#ifdef CONFIG_HA_WS_URL
    strlcpy(config->url, CONFIG_HA_WS_URL, sizeof(config->url));
#endif
#ifdef CONFIG_HA_TOKEN
    strlcpy(config->token, CONFIG_HA_TOKEN, sizeof(config->token));
#endif

    nvs_handle_t nvs = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &nvs) != ESP_OK) return;

    size_t len;
    len = sizeof(config->url);
    nvs_get_str(nvs, "ha_url",   config->url,   &len);
    len = sizeof(config->token);
    nvs_get_str(nvs, "ha_token", config->token, &len);

    nvs_close(nvs);
}

esp_err_t ha_config_save(const HaRuntimeConfig *config)
{
    if (config == nullptr) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, "ha_url",   config->url);
    if (err == ESP_OK)
        err = nvs_set_str(nvs, "ha_token", config->token);
    if (err == ESP_OK)
        err = nvs_commit(nvs);

    nvs_close(nvs);
    return err;
}

bool ha_config_ready(const HaRuntimeConfig *config)
{
    return config != nullptr &&
           config->url[0]   != '\0' &&
           config->token[0] != '\0';
}
