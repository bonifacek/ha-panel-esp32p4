#include "features_config.h"
#include "nvs.h"

static constexpr const char *kNs = "hw_features";

void hw_features_load(HwFeatures *f)
{
    f->camera  = true;
    f->battery = true;
    f->speaker = false;

    nvs_handle_t nvs = 0;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) return;

    uint8_t v;
    if (nvs_get_u8(nvs, "camera",  &v) == ESP_OK) f->camera  = (v != 0);
    if (nvs_get_u8(nvs, "battery", &v) == ESP_OK) f->battery = (v != 0);
    if (nvs_get_u8(nvs, "speaker", &v) == ESP_OK) f->speaker = (v != 0);

    nvs_close(nvs);
}

esp_err_t hw_features_save(const HwFeatures *f)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    nvs_set_u8(nvs, "camera",  f->camera  ? 1 : 0);
    nvs_set_u8(nvs, "battery", f->battery ? 1 : 0);
    nvs_set_u8(nvs, "speaker", f->speaker ? 1 : 0);
    err = nvs_commit(nvs);

    nvs_close(nvs);
    return err;
}
