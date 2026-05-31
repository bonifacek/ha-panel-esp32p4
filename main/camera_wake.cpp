/**
 * camera_wake.cpp
 *
 * Detekcja RUCHU jako wyzwalacz budzenia ekranu.
 * Kamera OV02C10 (MIPI CSI) → esp_video (V4L2) → porównanie klatek.
 *
 * Algorytm: co kSleepMs próbkuje klatkę 80×45 pikseli (kanał zielony jako luma),
 * porównuje z poprzednią klatką. Jeśli >kMtnCountThresh pikseli zmieniło się
 * o więcej niż kMtnPixThresh → ruch wykryty → ekran budzi się.
 *
 * Zalety vs detekcja twarzy:
 *   - Działa niezależnie od ekspozycji i kalibracji kolorów ISP
 *   - Zero zależności od esp-dl / modeli ML
 *   - ~3.5 KB RAM zamiast >2 MB PSRAM
 *   - Niezawodność ~100% dla ruchu w polu widzenia kamery
 *
 * Inicjalizacja nieśmiertelna: jeśli kamera niedostępna, task nie startuje.
 */

#include "camera_wake.h"
#include "board_display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_video_init.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "bsp/esp-bsp.h"

static const char *TAG = "camera_wake";

// ── Parametry kamery ──────────────────────────────────────────────────────────
static constexpr int kBufCount = 3;
static constexpr int kSleepMs  = 500;  // 2 fps — wystarczy dla detekcji ruchu

// ── Parametry detekcji ruchu ──────────────────────────────────────────────────
// Rozmiar mapy ruchu: 80×45 = 3600 pikseli (16× subsample z 1288×728)
static constexpr int kMtnW = 80;
static constexpr int kMtnH = 45;
// Próg na piksel: |luma_curr - luma_prev| > kMtnPixThresh → zmieniony
// 8  = czuły (dalekie obiekty), 15 = średni, 25 = tylko bliskie duże zmiany
static constexpr int kMtnPixThresh   = 15;
// Minimalna liczba zmienionych pikseli żeby uznać ruch
// 200 = 5.5% kadru (dalekie), 500 = 14% (średnie), 800 = 22% (tylko blisko)
static constexpr int kMtnCountThresh = 500;

// ── Stan kamery ───────────────────────────────────────────────────────────────
static size_t   s_frame_bytes = 0;
static uint32_t s_cam_w       = 1288;
static uint32_t s_cam_h       = 728;
static int      s_video_fd    = -1;
static void    *s_frame_bufs[kBufCount];

// Poprzednia klatka (luma 80×45) — w SRAM, ~3.5 KB
static uint8_t  s_prev_luma[kMtnW * kMtnH];
static bool     s_prev_valid = false;

// ── Inicjalizacja kamery ──────────────────────────────────────────────────────
static esp_err_t cam_hw_init(void)
{
    i2c_master_bus_handle_t bsp_i2c = bsp_i2c_get_handle();
    if (!bsp_i2c) {
        ESP_LOGE(TAG, "BSP I2C handle NULL");
        return ESP_ERR_INVALID_STATE;
    }

    esp_video_init_csi_config_t csi_cfg = {};
    csi_cfg.sccb_config.init_sccb  = false;
    csi_cfg.sccb_config.i2c_handle = bsp_i2c;
    csi_cfg.sccb_config.freq       = 100000;
    csi_cfg.reset_pin = -1;
    csi_cfg.pwdn_pin  = -1;

    esp_video_init_config_t cam_cfg = {};
    cam_cfg.csi = &csi_cfg;

    esp_err_t err = esp_video_init(&cam_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init: %s", esp_err_to_name(err));
        return err;
    }

    s_video_fd = open("/dev/video0", O_RDONLY | O_NONBLOCK, 0);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "Nie mozna otworzyc /dev/video0 (errno %d)", errno);
        return ESP_FAIL;
    }

    // Pobierz aktualny format — akceptujemy cokolwiek driver daje
    struct v4l2_format actual = {};
    actual.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &actual) == 0) {
        s_cam_w       = actual.fmt.pix.width  ? actual.fmt.pix.width  : s_cam_w;
        s_cam_h       = actual.fmt.pix.height ? actual.fmt.pix.height : s_cam_h;
        bool is565    = (actual.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565 ||
                         actual.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565X);
        uint32_t bpp  = is565 ? 2 : 3;
        s_frame_bytes = actual.fmt.pix.sizeimage > 0
                        ? actual.fmt.pix.sizeimage
                        : s_cam_w * s_cam_h * bpp;
        ESP_LOGI(TAG, "Format: %" PRIu32 "x%" PRIu32 " fmt=0x%08" PRIx32 " size=%zu",
                 s_cam_w, s_cam_h, actual.fmt.pix.pixelformat, s_frame_bytes);
    } else {
        s_frame_bytes = s_cam_w * s_cam_h * 2;
        ESP_LOGW(TAG, "G_FMT failed, zakladam RGB565 1288x728");
    }

    struct v4l2_requestbuffers req = {};
    req.count  = kBufCount;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS errno %d", errno);
        return ESP_FAIL;
    }

    for (int i = 0; i < (int)req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(s_video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF %d errno %d", i, errno);
            return ESP_FAIL;
        }
        s_frame_bufs[i] = mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               s_video_fd, buf.m.offset);
        if (!s_frame_bufs[i]) {
            ESP_LOGE(TAG, "mmap %d failed", i);
            return ESP_FAIL;
        }
        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF %d errno %d", i, errno);
            return ESP_FAIL;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Kamera OV02C10 OK (%" PRIu32 "x%" PRIu32 ")", s_cam_w, s_cam_h);
    return ESP_OK;
}

// ── Task detekcji ruchu ───────────────────────────────────────────────────────
static void camera_wake_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Detekcja ruchu aktywna (mapa %dx%d, prog=%d/%d px)",
             kMtnW, kMtnH, kMtnPixThresh, kMtnCountThresh);

    // Bufor tymczasowy dla bieżącej klatki luma — na stosie (~3.5 KB)
    uint8_t curr_luma[kMtnW * kMtnH];

    const int sx = (int)s_cam_w / kMtnW;   // krok X (≈16)
    const int sy = (int)s_cam_h / kMtnH;   // krok Y (≈16)

    struct v4l2_buffer buf = {};
    while (true) {
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(s_video_fd, VIDIOC_DQBUF, &buf) != 0) {
            if (errno == EAGAIN) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "VIDIOC_DQBUF errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── Buduj mapę luma 80×45 ─────────────────────────────────────────────
        // LE RGB565: kanał zielony = bity [10:5] (6-bit, zakres 0-63)
        // Używamy go jako przybliżenia luminancji — niezależne od AWB i ekspozycji
        // (względna zmiana działa nawet przy ciemnym obrazie).
        const uint16_t *px = (const uint16_t *)s_frame_bufs[buf.index];
        for (int y = 0; y < kMtnH; y++) {
            for (int x = 0; x < kMtnW; x++) {
                uint16_t v = px[y * sy * (int)s_cam_w + x * sx];
                curr_luma[y * kMtnW + x] = (uint8_t)((v >> 5) & 0x3F); // 0-63
            }
        }

        // ── Porównaj z poprzednią klatką ──────────────────────────────────────
        if (s_prev_valid) {
            int changed = 0;
            for (int i = 0; i < kMtnW * kMtnH; i++) {
                int diff = (int)curr_luma[i] - (int)s_prev_luma[i];
                if (diff < 0) diff = -diff;
                if (diff > kMtnPixThresh) changed++;
            }

            if (changed >= kMtnCountThresh) {
                ESP_LOGI(TAG, "Ruch wykryty! (%d/%d pikseli)", changed, kMtnW * kMtnH);
                board_display_notify_activity();
            } else {
                ESP_LOGD(TAG, "Bez ruchu (%d)", changed);
            }
        }

        // Zapamiętaj bieżącą klatkę jako poprzednią
        memcpy(s_prev_luma, curr_luma, sizeof(s_prev_luma));
        s_prev_valid = true;

        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_QBUF errno %d", errno);
        }

        vTaskDelay(pdMS_TO_TICKS(kSleepMs));
    }
}

// ── Task startujący z opóźnieniem ─────────────────────────────────────────────
static void camera_init_task(void *arg)
{
    // Czekamy 10s na stabilizację systemu
    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_err_t err = cam_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Kamera niedostepna (%s) — system dziala normalnie",
                 esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    // Core 1: detekcja ruchu nie blokuje LVGL (Core 0)
    BaseType_t ret = xTaskCreatePinnedToCore(
        camera_wake_task, "cam_wake", 8192, nullptr, 2, nullptr, 1);
    if (ret == pdPASS) {
        ESP_LOGI(TAG, "cam_wake uruchomiony na Core 1");
    } else {
        ESP_LOGE(TAG, "Nie mozna uruchomic cam_wake");
    }
    vTaskDelete(nullptr);
}

// ── Publiczne API ─────────────────────────────────────────────────────────────
esp_err_t camera_wake_init(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        camera_init_task, "cam_init", 6144, nullptr, 2, nullptr, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Nie mozna uruchomic cam_init");
        return ESP_FAIL;
    }
    return ESP_OK;
}
