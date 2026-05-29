#include "battery_monitor.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include <algorithm>

static const char *TAG = "battery_monitor";
static constexpr int kBatteryGpio = 52;
static constexpr float kDividerRatio = (68.0f + 100.0f) / 100.0f;
static constexpr int kSampleCount = 8;

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t         s_adc_cali_handle;
static adc_unit_t s_adc_unit;
static adc_channel_t s_adc_channel;
static bool s_initialized;

static int estimate_percent(int millivolts)
{
    struct Point {
        int mv;
        int pct;
    };

    static constexpr Point curve[] = {
        {3300, 0},
        {3500, 5},
        {3600, 15},
        {3700, 30},
        {3800, 50},
        {3900, 65},
        {4000, 80},
        {4100, 92},
        {4200, 100},
    };

    if (millivolts <= curve[0].mv) {
        return curve[0].pct;
    }
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (millivolts <= curve[i].mv) {
            const int mv_span = curve[i].mv - curve[i - 1].mv;
            const int pct_span = curve[i].pct - curve[i - 1].pct;
            return curve[i - 1].pct + ((millivolts - curve[i - 1].mv) * pct_span) / mv_span;
        }
    }
    return 100;
}

esp_err_t battery_monitor_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = adc_oneshot_io_to_channel(kBatteryGpio, &s_adc_unit, &s_adc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not mapped to an ADC channel: %s", kBatteryGpio, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = s_adc_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_pull_mode(static_cast<gpio_num_t>(kBatteryGpio), GPIO_FLOATING);

    // Kalibracja ADC — ESP32-P4 uzywa curve fitting.
    // Bez kalibracji raw*3300/4095 zaklada bledna skale i bateria zawsze
    // pokazuje 100% (np. 3.8V -> ~4323mV po blednym przeliczeniu).
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    {
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id  = s_adc_unit,
            .chan     = s_adc_channel,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        esp_err_t cerr = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
        if (cerr != ESP_OK) {
            ESP_LOGW(TAG, "ADC calibration failed (%s), using fallback", esp_err_to_name(cerr));
            s_adc_cali_handle = nullptr;
        } else {
            ESP_LOGI(TAG, "ADC calibration (curve fitting) OK");
        }
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id  = s_adc_unit,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        esp_err_t cerr = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali_handle);
        if (cerr != ESP_OK) {
            ESP_LOGW(TAG, "ADC calibration failed (%s), using fallback", esp_err_to_name(cerr));
            s_adc_cali_handle = nullptr;
        } else {
            ESP_LOGI(TAG, "ADC calibration (line fitting) OK");
        }
    }
#else
    s_adc_cali_handle = nullptr;
    ESP_LOGW(TAG, "No ADC calibration scheme available, using fallback");
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "Battery monitor on GPIO%d ADC unit %d channel %d", kBatteryGpio, s_adc_unit, s_adc_channel);
    return ESP_OK;
}

BatteryReading battery_monitor_read(void)
{
    BatteryReading reading = {};
    if (!s_initialized || s_adc_handle == nullptr) {
        return reading;
    }

    int total_mv = 0;
    int valid_samples = 0;
    for (int i = 0; i < kSampleCount; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, s_adc_channel, &raw) != ESP_OK) {
            continue;
        }

        int adc_mv = 0;
        if (s_adc_cali_handle) {
            adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &adc_mv);
        } else {
            // Fallback: ESP32-P4 ADC_ATTEN_DB_12 full-scale ~2900mV
            adc_mv = (raw * 2900) / 4095;
        }

        total_mv += static_cast<int>(adc_mv * kDividerRatio);
        ++valid_samples;
    }

    if (valid_samples == 0) {
        return reading;
    }

    reading.valid = true;
    reading.millivolts = total_mv / valid_samples;
    reading.percent = std::clamp(estimate_percent(reading.millivolts), 0, 100);
    return reading;
}
