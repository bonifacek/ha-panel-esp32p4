/**
 * camera_wake.cpp
 *
 * Detekcja twarzy jako wyzwalacz budzenia ekranu.
 * Kamera SC2336 (MIPI CSI 2-lane) → esp_video (V4L2) → HumanFaceDetect (esp-dl).
 *
 * Kiedy ekran jest wygaszony i kamera wykryje twarz →
 *   board_display_notify_activity() włącza podświetlenie i resetuje timer.
 *
 * Inicjalizacja jest nieśmiertelna: jeśli kamera niedostępna, task nie startuje
 * i reszta systemu (WiFi, HA, wyświetlacz) działa normalnie.
 */

#include "camera_wake.h"
#include "board_display.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// V4L2 kamera (esp_video)
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

// Detekcja twarzy (esp-dl)
#include "human_face_detect.hpp"

// BSP — współdzielony bus I2C (ten sam co dotyk GSL3680)
#include "bsp/esp-bsp.h"

static const char *TAG = "camera_wake";

// ── Konfiguracja ────────────────────────────────────────────────────────────
// 640×480 RGB565: wystarczające dla detekcji, szybsze niż 1280×720
static constexpr int   kCamW        = 640;
static constexpr int   kCamH        = 480;
static constexpr int   kFrameBytes  = kCamW * kCamH * 2;  // RGB565
static constexpr int   kBufCount    = 2;
// Detekcja co ~1s gdy ekran śpi (zadanie trwa ~300–600ms na P4 @ 360MHz)
static constexpr int   kSleepMs     = 400;

// ── Stan ────────────────────────────────────────────────────────────────────
static int    s_video_fd = -1;
static void  *s_frame_bufs[kBufCount];
static HumanFaceDetect *s_detector = nullptr;

// ── Inicjalizacja kamery ────────────────────────────────────────────────────
static esp_err_t cam_hw_init(void)
{
    // Reużyj I2C zainicjalizowanego przez BSP (wspólny z touch, SDA=7 SCL=8)
    i2c_master_bus_handle_t i2c = bsp_i2c_get_handle();
    if (!i2c) {
        ESP_LOGE(TAG, "BSP I2C handle NULL — kamera niedostepna");
        return ESP_ERR_INVALID_STATE;
    }

    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb  = false,       // reużywamy istniejący BSP I2C
            .i2c_handle = i2c,
        },
        .reset_pin = -1,               // płytka nie podłącza RST kamery
        .pwdn_pin  = -1,               // płytka nie podłącza PWDN kamery
    };

    esp_video_init_config_t cam_cfg = {
        .csi = &csi_cfg,
    };

    esp_err_t err = esp_video_init(&cam_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init: %s", esp_err_to_name(err));
        return err;
    }

    // Otwórz urządzenie V4L2 (SC2336 rejestruje się jako /dev/video0)
    s_video_fd = open("/dev/video0", O_RDONLY, 0);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "Nie mozna otworzyc /dev/video0 (errno %d)", errno);
        return ESP_FAIL;
    }

    // Sprawdź i ustaw format: 640×480 RGB565
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &fmt) == 0) {
        ESP_LOGI(TAG, "Domyslny format kamery: %"PRIu32"x%"PRIu32,
                 fmt.fmt.pix.width, fmt.fmt.pix.height);
    }

    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = kCamW;
    fmt.fmt.pix.height       = kCamH;
    fmt.fmt.pix.pixelformat  = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_video_fd, VIDIOC_S_FMT, &fmt) != 0) {
        // Nieśmiertelne — spróbujemy z domyślnym formatem
        ESP_LOGW(TAG, "VIDIOC_S_FMT 640x480 RGB565 niedostepny, uzywam domyslnego");
    }

    // Alokuj bufory klatek w PSRAM (2 × ~614 KB)
    for (int i = 0; i < kBufCount; i++) {
        s_frame_bufs[i] = heap_caps_malloc(
            kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frame_bufs[i]) {
            ESP_LOGE(TAG, "Brak pamieci PSRAM na bufor kamery %d", i);
            return ESP_ERR_NO_MEM;
        }
    }

    // Zarejestruj bufory USERPTR w V4L2
    struct v4l2_requestbuffers req = {};
    req.count  = kBufCount;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(s_video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS errno %d", errno);
        return ESP_FAIL;
    }

    // Wstaw wszystkie bufory do kolejki kamery
    for (int i = 0; i < kBufCount; i++) {
        struct v4l2_buffer buf = {};
        buf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory     = V4L2_MEMORY_USERPTR;
        buf.index      = i;
        buf.m.userptr  = (unsigned long)s_frame_bufs[i];
        buf.length     = kFrameBytes;
        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF %d errno %d", i, errno);
            return ESP_FAIL;
        }
    }

    // Uruchom strumień kamery
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Kamera SC2336 OK (%dx%d RGB565)", kCamW, kCamH);
    return ESP_OK;
}

// ── Task detekcji twarzy ────────────────────────────────────────────────────
static void camera_wake_task(void *arg)
{
    (void)arg;

    // Inicjalizacja detektora twarzy (dwuetapowy MSR+MNP, modele w flash rodata)
    s_detector = new HumanFaceDetect();
    if (!s_detector) {
        ESP_LOGE(TAG, "Nie mozna stworzyc HumanFaceDetect — task konczy");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "HumanFaceDetect zainicjalizowany");

    struct v4l2_buffer buf = {};

    while (true) {
        // Pobierz klatkę z kamery (blokuje do ~20ms przy 50fps)
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        if (ioctl(s_video_fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_DQBUF errno %d — pomijam klatke", errno);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Detekcja twarzy na klatce RGB565
        dl::image::img_t img;
        img.data     = s_frame_bufs[buf.index];
        img.width    = kCamW;
        img.height   = kCamH;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

        auto &results = s_detector->run(img);

        if (!results.empty()) {
            ESP_LOGD(TAG, "Twarz wykryta (%zu) — budzenie ekranu", results.size());
            board_display_notify_activity();
        }

        // Zwróć bufor do kolejki kamery
        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_QBUF errno %d", errno);
        }

        // Czekaj przed następną detekcją (~1fps przy screen-off, oszczędza CPU)
        vTaskDelay(pdMS_TO_TICKS(kSleepMs));
    }
}

// ── Publiczne API ───────────────────────────────────────────────────────────
esp_err_t camera_wake_init(void)
{
    esp_err_t err = cam_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Inicjalizacja kamery nieudana (%s) — "
                      "budzenie przez kature wylaczone, reszte systemu OK",
                 esp_err_to_name(err));
        return err;
    }

    // Task na CPU1 (CPU0 = LVGL + HA), stos 12KB (HumanFaceDetect potrzebuje ~8KB)
    BaseType_t ret = xTaskCreatePinnedToCore(
        camera_wake_task, "cam_wake", 12288, nullptr, 3, nullptr, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore nieudane");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Detekcja twarzy → budzenie ekranu aktywne");
    return ESP_OK;
}
