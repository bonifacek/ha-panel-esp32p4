#include "board_display.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct LvglPortTouchContext {
    esp_lcd_touch_handle_t handle;
};

static lv_display_t *s_display;
static constexpr uint32_t kRotatedDrawBufferLines = 10;
static constexpr int64_t kBacklightIdleTimeoutUs = 60LL * 1000LL * 1000LL;
static const char *TAG = "board_display";
static esp_timer_handle_t s_backlight_timer;
static int64_t s_last_touch_us;
static bool s_backlight_off;

static int32_t clamp_coord(int32_t value, int32_t max)
{
    if (value < 0) {
        return 0;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void landscape_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    auto *touch_context = static_cast<LvglPortTouchContext *>(lv_indev_get_driver_data(indev));
    if (touch_context == nullptr || touch_context->handle == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t touch_count = 0;

    esp_lcd_touch_read_data(touch_context->handle);
    const bool pressed = esp_lcd_touch_get_coordinates(
        touch_context->handle,
        &x,
        &y,
        nullptr,
        &touch_count,
        1);

    if (!pressed || touch_count == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    s_last_touch_us = esp_timer_get_time();
    if (s_backlight_off) {
        bsp_display_backlight_on();
        s_backlight_off = false;
        ESP_LOGI(TAG, "Backlight woke up by touch");
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const int32_t mapped_x = clamp_coord(BSP_LCD_H_RES - 1 - x, BSP_LCD_H_RES - 1);
    const int32_t mapped_y = clamp_coord(y, BSP_LCD_V_RES - 1);
    data->point.x = mapped_x;
    data->point.y = mapped_y;
    data->state = LV_INDEV_STATE_PRESSED;
}

static void backlight_idle_timer_cb(void *arg)
{
    (void)arg;
    if (s_backlight_off) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_touch_us >= kBacklightIdleTimeoutUs) {
        bsp_display_backlight_off();
        s_backlight_off = true;
        ESP_LOGI(TAG, "Backlight turned off after 60 seconds of inactivity");
    }
}

static esp_err_t start_backlight_idle_timer()
{
    if (s_backlight_timer != nullptr) {
        return ESP_OK;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = backlight_idle_timer_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "backlight_idle",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_backlight_timer);
    if (err != ESP_OK) {
        return err;
    }
    return esp_timer_start_periodic(s_backlight_timer, 1000 * 1000);
}

static void configure_landscape_touch_270()
{
    lv_indev_t *input = bsp_display_get_input_dev();
    if (input == nullptr) {
        ESP_LOGW(TAG, "Touch input device is not available");
        return;
    }

    auto *touch_context = static_cast<LvglPortTouchContext *>(lv_indev_get_driver_data(input));
    if (touch_context == nullptr || touch_context->handle == nullptr) {
        ESP_LOGW(TAG, "Touch context is not available");
        return;
    }

    esp_lcd_touch_set_swap_xy(touch_context->handle, false);
    esp_lcd_touch_set_mirror_x(touch_context->handle, false);
    esp_lcd_touch_set_mirror_y(touch_context->handle, false);
    lv_indev_set_read_cb(input, landscape_touch_read_cb);
    ESP_LOGI(TAG, "Touch rotation set by custom landscape callback");
}

esp_err_t board_display_init(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * kRotatedDrawBufferLines,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = true,
        },
    };
    // Zwiekszamy stos tasku LVGL: domyslne 7168B nie wystarczy dla sw_rotate +
    // 2 zakladek + glebokiego drzewa obiektow. 20KB = bezpieczny margines.
    cfg.lvgl_port_cfg.task_stack = 20480;

    s_display = bsp_display_start_with_config(&cfg);
    if (s_display == nullptr) {
        return ESP_FAIL;
    }
    bsp_display_rotate(s_display, LV_DISPLAY_ROTATION_270);

    // With sw_rotate=true the lvgl_port flush path uses synchronous CPU memcpy to PSRAM
    // (DMA2D disabled in BSP — see esp32_p4_function_ev_board.c).  flush_ready() is called
    // synchronously inside draw_bitmap, so flushing=0 before flush_cb returns — no wait needed.
    // Do NOT install flush_wait_cb; it would add a spurious 1 ms vTaskDelay per tile for no gain.
    // Note: if DMA2D is ever re-enabled, reinstate:
    //   lv_display_set_flush_wait_cb(s_display, [](lv_display_t *){ vTaskDelay(pdMS_TO_TICKS(1)); });

    configure_landscape_touch_270();
    bsp_display_backlight_on();
    s_backlight_off = false;
    s_last_touch_us = esp_timer_get_time();
    ESP_ERROR_CHECK(start_backlight_idle_timer());
    return ESP_OK;
}

void board_display_lock(void)
{
    bsp_display_lock(0);
}

void board_display_unlock(void)
{
    bsp_display_unlock();
}

void board_display_notify_activity(void)
{
    s_last_touch_us = esp_timer_get_time();
    if (s_backlight_off) {
        bsp_display_backlight_on();
        s_backlight_off = false;
        ESP_LOGI(TAG, "Backlight woke up by camera");
    }
}
