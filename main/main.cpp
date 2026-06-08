#include "ha_service.h"
#include "ha_config.h"
#include "ha_entities.h"
#include "battery_monitor.h"
#include "board_display.h"
#include "panel_ui.h"
#include "web_config.h"
#include "features_config.h"
#include "speaker.h"
#include "speaker_sched.h"
#include "tts_player.h"
#include "esp_hosted_ota.h"
#include "esp_hosted.h"

// ── C6 OTA ──────────────────────────────────────────────────────────────────
// Ustaw na 1, zbuduj i flashuj P4, odczekaj aż C6 się zaktualizuje (reboot),
// a potem ustaw z powrotem na 0 i ponownie flashuj.
// URL: http://<IP_KOMPUTERA>:<PORT>/network_adapter.bin
// Uruchom serwer: cd ..\managed_components\espressif__esp_hosted\slave\build
//                 python -m http.server 8080
#define ENABLE_C6_OTA   0
#define C6_OTA_URL      "http://192.168.1.100:8080/network_adapter.bin"
// ────────────────────────────────────────────────────────────────────────────

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <ssid_manager.h>
#include <wifi_configuration_ap.h>
#include <wifi_station.h>

#include "esp_sntp.h"
#include <cstring>
#include <time.h>

static const char *TAG = "ha_panel";
static HaRuntimeConfig s_ha_config;
static HwFeatures s_hw_features;

// Wywolywane przez panel_ui po dotknięciu przycisku switch
static void handle_command(size_t screen_idx, size_t tile_idx,
                           size_t row_idx, bool turn_on)
{
    speaker_play(SpeakerSound::Beep);   // krotki pisk — potwierdzenie dotyku
    ha_service_send_command(screen_idx, tile_idx, row_idx, turn_on);
}

// Wywolywane przez ha_service gdy przyjdzie nowy stan encji
static void handle_ha_state(size_t screen_idx, size_t tile_idx,
                            size_t row_idx, const char *value)
{
    panel_ui_update_row(screen_idx, tile_idx, row_idx, value, true);
}

static void handle_ha_status(bool connected)
{
    panel_ui_set_ha_status(connected ? "online" : "offline");
    if (connected)
        speaker_play(SpeakerSound::Chime);  // dzwon przy polaczeniu z HA
}

static void start_wifi_config_portal()
{
    panel_ui_set_wifi_status("AP config");
    auto &ap = WifiConfigurationAp::GetInstance();
    ap.SetSsidPrefix("HA-PANEL");
    ap.Start();
    ESP_LOGW(TAG, "Wi-Fi not configured. Connect to AP HA-PANEL-* -> http://192.168.4.1");
}

static void network_task(void *arg)
{
    (void)arg;

    const auto &ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty()) {
        start_wifi_config_portal();
        vTaskDelete(nullptr);
        return;
    }

    panel_ui_set_wifi_status("laczenie");

    auto &wifi = WifiStation::GetInstance();
    wifi.OnConnect([](const std::string &) {
        panel_ui_set_wifi_status("laczenie");
    });
    wifi.OnConnected([](const std::string &ssid) {
        ESP_LOGI(TAG, "Wi-Fi: %s", ssid.c_str());
        panel_ui_set_wifi_status("online");
    });
    wifi.Start();

    if (!wifi.WaitForConnected(60000)) {
        ESP_LOGW(TAG, "Wi-Fi timeout");
        wifi.Stop();
        start_wifi_config_portal();
        vTaskDelete(nullptr);
        return;
    }

    // Synchronizacja czasu NTP (Polska: CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP uruchomiony");

    ESP_ERROR_CHECK(web_config_start(&s_ha_config));

    if (!ha_config_ready(&s_ha_config)) {
        panel_ui_set_ha_status("konfiguracja WWW");
        ESP_LOGW(TAG, "HA not configured. Open device IP in browser.");
        vTaskDelete(nullptr);
        return;
    }

    // Krótka pauza po WiFi — daje czas routingowi i ARP na stabilizację
    vTaskDelay(pdMS_TO_TICKS(2000));

#if ENABLE_C6_OTA
    panel_ui_set_ha_status("C6 OTA...");
    ESP_LOGI(TAG, "C6 OTA: pobieranie %s", C6_OTA_URL);
    esp_err_t ota_ret = esp_hosted_slave_ota(C6_OTA_URL);
    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "C6 OTA: sukces, restartuję...");
        panel_ui_set_ha_status("C6 OTA ok, restart...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "C6 OTA: blad %s", esp_err_to_name(ota_ret));
        panel_ui_set_ha_status("C6 OTA blad!");
    }
#endif

    panel_ui_set_ha_status("laczenie");
    ha_service_start(&s_ha_config, handle_ha_state, handle_ha_status);

    vTaskDelete(nullptr);
}

static void battery_task(void *arg)
{
    (void)arg;
    if (battery_monitor_init() != ESP_OK) {
        panel_ui_set_battery_status("brak ADC");
        vTaskDelete(nullptr);
        return;
    }
    while (true) {
        const BatteryReading reading = battery_monitor_read();
        if (reading.valid) {
            char text[32] = {};
            snprintf(text, sizeof(text), "%.2fV %d%%",
                     reading.millivolts / 1000.0f, reading.percent);
            panel_ui_set_battery_status(text);
        } else {
            panel_ui_set_battery_status("brak odczytu");
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void init_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path            = "/storage",
        .partition_label      = "storage",
        .max_files            = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI("main", "SPIFFS zamontowany (/storage)");
    } else {
        // Nie blokuj startu — dashboard bedzie pusty, ale UI zadziala
        ESP_LOGW("main", "SPIFFS blad: %s — dashboard niedostepny", esp_err_to_name(err));
    }
}

extern "C" void app_main(void)
{
    init_nvs();
    init_spiffs();       // montuje /storage (SPIFFS 7 MB) — musi byc przed ha_dashboard_load
    ha_config_load(&s_ha_config);
    hw_features_load(&s_hw_features);
    sched_load(nullptr); // ładuje harmonogram głośności (z NVS lub domyślne)

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // esp_hosted_init() MUST run post-scheduler (SDIO sdmmc_host_init_slot needs FreeRTOS).
    // Auto-init in port_esp_hosted_host_init.c is disabled (no-op). See that file for details.
    ESP_ERROR_CHECK(esp_hosted_init());

    // Zaladuj dashboard z SPIFFS zanim zbudujemy UI
    ESP_ERROR_CHECK(ha_dashboard_load());

    ESP_ERROR_CHECK(board_display_init());
    board_display_lock();
    panel_ui_create(handle_command);
    board_display_unlock();

    if (s_hw_features.speaker) {
        speaker_init();
        // TTS init: silnik domyslny = tts.piper, jezyk pl-PL
        // Uzytkownik moze nadpisac przez panel WWW -> /api/tts_cfg
        tts_init(&s_ha_config, "tts.piper", "pl-PL");
    }
    if (s_hw_features.battery) xTaskCreatePinnedToCore(battery_task, "battery_task", 8192, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(network_task, "network_task", 8192,  nullptr, 5, nullptr, 0);
}
