#include "camera_snapshot.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cam_snap";

// Parsuje wymiary z nagłówka JPEG (szuka markera SOF0/SOF2).
// Wymagane bo esp_driver_jpeg 5.4 nie ma jpeg_decoder_get_picture_info().
static bool jpeg_parse_dimensions(const uint8_t *buf, size_t len,
                                   uint16_t *out_w, uint16_t *out_h)
{
    for (size_t i = 0; i + 3 < len; ) {
        if (buf[i] != 0xFF) { i++; continue; }
        uint8_t marker = buf[i + 1];
        if (marker == 0xD8) { i += 2; continue; }  // SOI
        if (marker == 0xD9) break;                  // EOI
        if (i + 3 >= len) break;
        uint16_t seg = ((uint16_t)buf[i+2] << 8) | buf[i+3];
        // SOF0=C0, SOF1=C1, SOF2=C2 — zawierają wymiary w pozycji +3/+4/+5/+6
        if ((marker == 0xC0 || marker == 0xC1 || marker == 0xC2) && seg >= 9) {
            if (i + 8 >= len) break;
            *out_h = ((uint16_t)buf[i+5] << 8) | buf[i+6];
            *out_w = ((uint16_t)buf[i+7] << 8) | buf[i+8];
            return (*out_w > 0 && *out_h > 0);
        }
        i += 2 + seg;
    }
    return false;
}

// Rozmiary buforow PSRAM
static constexpr size_t kJpegBufMax = 2 * 1024 * 1024; // 2 MB na surowe JPEG (2K ~0.5-1.5 MB)
static constexpr size_t kRgbBufMax  = 2560 * 1440 * 2; // max 2K RGB565 (~7.2 MB)
static constexpr size_t kOutBufMax  = 1280 * 800 * 2;  // max display RGB565 po skalowaniu

static char     s_base_url[140];   // "http://192.168.x.x:8123"
static char     s_auth[280];       // "Bearer <token>"

static uint8_t *s_jpeg_buf = nullptr;
static uint8_t *s_rgb_buf  = nullptr;
static uint8_t *s_out_buf  = nullptr;
static lv_img_dsc_t s_img_dsc;

// ---------------------------------------------------------------------------

esp_err_t cam_snap_init(const HaRuntimeConfig *cfg)
{
    // Skonstruuj bazowy URL HTTP z adresu WebSocket
    // "ws://192.168.1.100:8123/api/websocket"  ->  "http://192.168.1.100:8123"
    if (strncmp(cfg->url, "ws://", 5) == 0) {
        snprintf(s_base_url, sizeof(s_base_url), "http://%s", cfg->url + 5);
    } else if (strncmp(cfg->url, "wss://", 6) == 0) {
        snprintf(s_base_url, sizeof(s_base_url), "https://%s", cfg->url + 6);
    } else {
        strlcpy(s_base_url, cfg->url, sizeof(s_base_url));
    }
    char *p = strstr(s_base_url, "/api/websocket");
    if (p) *p = '\0';

    snprintf(s_auth, sizeof(s_auth), "Bearer %s", cfg->token);

    // Bufory PSRAM alokowane leniwie przy pierwszym cam_snap_fetch,
    // zeby nie fragmentowac heapu gdy nie ma kafelka kamery na dashboardzie.
    ESP_LOGI(TAG, "init OK, base_url=%s (bufory odlozone)", s_base_url);
    return ESP_OK;
}

static bool ensure_buffers()
{
    if (s_jpeg_buf) return true;  // juz zaalokowane
    // PPA i JPEG decoder wymagaja wyrownania do linii cache L2 = 128 B
    s_jpeg_buf = (uint8_t *)heap_caps_aligned_alloc(128, kJpegBufMax, MALLOC_CAP_SPIRAM);
    s_rgb_buf  = (uint8_t *)heap_caps_aligned_alloc(128, kRgbBufMax,  MALLOC_CAP_SPIRAM);
    s_out_buf  = (uint8_t *)heap_caps_aligned_alloc(128, kOutBufMax,  MALLOC_CAP_SPIRAM);
    if (!s_jpeg_buf || !s_rgb_buf || !s_out_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed");
        heap_caps_free(s_jpeg_buf); s_jpeg_buf = nullptr;
        heap_caps_free(s_rgb_buf);  s_rgb_buf  = nullptr;
        heap_caps_free(s_out_buf);  s_out_buf  = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "bufory kamery zaalokowane (%.1f MB PSRAM)",
             (kJpegBufMax + kRgbBufMax + kOutBufMax) / 1048576.0f);
    return true;
}

// ---------------------------------------------------------------------------

static int fetch_jpeg(const char *entity_id, size_t *out_len)
{
    char url[200];
    snprintf(url, sizeof(url), "%s/api/camera_proxy/%s", s_base_url, entity_id);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.timeout_ms = 8000;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Authorization", s_auth);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);

    int total = 0, chunk;
    while ((chunk = esp_http_client_read(client,
                (char *)s_jpeg_buf + total,
                (int)(kJpegBufMax - total))) > 0) {
        total += chunk;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200 || total <= 0) {
        ESP_LOGE(TAG, "HTTP %d, len=%d", status, total);
        return -1;
    }

    *out_len = (size_t)total;
    return 0;
}

// ---------------------------------------------------------------------------

const lv_img_dsc_t *cam_snap_fetch(const char *entity_id,
                                    uint16_t out_w, uint16_t out_h)
{
    if (!ensure_buffers()) return nullptr;

    // 1. Pobierz JPEG
    size_t jpeg_len = 0;
    if (fetch_jpeg(entity_id, &jpeg_len) != 0)
        return nullptr;

    // 2. Dekoduj JPEG → RGB565 (hardware decoder, ESP-IDF 5.4 driver/jpeg_decode.h)
    jpeg_decode_engine_cfg_t eng_cfg = {};
    eng_cfg.intr_priority = 0;
    eng_cfg.timeout_ms    = 2000;

    jpeg_decoder_handle_t jpgd;
    if (jpeg_new_decoder_engine(&eng_cfg, &jpgd) != ESP_OK) {
        ESP_LOGE(TAG, "decoder engine failed");
        return nullptr;
    }

    // Wyciągnij wymiary z nagłówka JPEG (SOF marker)
    uint16_t src_w = 0, src_h = 0;
    if (!jpeg_parse_dimensions(s_jpeg_buf, jpeg_len, &src_w, &src_h)) {
        ESP_LOGE(TAG, "cannot parse JPEG dimensions");
        jpeg_del_decoder_engine(jpgd);
        return nullptr;
    }

    // HW decoder wyrównuje wymiary do 16 px — bufor musi pomieścić wyrównany rozmiar
    uint16_t aligned_w = (uint16_t)((src_w + 15u) & ~15u);
    uint16_t aligned_h = (uint16_t)((src_h + 15u) & ~15u);
    uint32_t rgb_size  = (uint32_t)aligned_w * aligned_h * 2;
    if (rgb_size > kRgbBufMax) {
        ESP_LOGE(TAG, "frame too large: %ux%u (aligned %ux%u)", src_w, src_h, aligned_w, aligned_h);
        jpeg_del_decoder_engine(jpgd);
        return nullptr;
    }

    jpeg_decode_cfg_t dec_cfg = {};
    dec_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    dec_cfg.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;

    uint32_t decoded_size = 0;
    esp_err_t err = jpeg_decoder_process(jpgd, &dec_cfg, s_jpeg_buf, (uint32_t)jpeg_len,
                               s_rgb_buf, rgb_size, &decoded_size);
    jpeg_del_decoder_engine(jpgd);

    if (err != ESP_OK || decoded_size == 0) {
        ESP_LOGE(TAG, "decode failed: %s", esp_err_to_name(err));
        return nullptr;
    }

    // 3. Skaluj PPA do zadanego rozmiaru overlay
    // src_w / src_h już zdefiniowane powyżej z parsera SOF

    // Zachowaj proporcje (letterbox)
    float sx = (float)out_w / src_w;
    float sy = (float)out_h / src_h;
    float scale = (sx < sy) ? sx : sy;
    uint16_t dst_w = (uint16_t)(src_w * scale);
    uint16_t dst_h = (uint16_t)(src_h * scale);
    // Wyrownaj do 2px (wymog PPA)
    dst_w &= ~1u;
    dst_h &= ~1u;
    if (dst_w < 2) dst_w = 2;
    if (dst_h < 2) dst_h = 2;

    ppa_client_handle_t ppa_client;
    ppa_client_config_t ppa_client_cfg = {};
    ppa_client_cfg.oper_type = PPA_OPERATION_SRM;
    if (ppa_register_client(&ppa_client_cfg, &ppa_client) != ESP_OK) {
        ESP_LOGE(TAG, "ppa_register_client failed");
        return nullptr;
    }

    ppa_srm_oper_config_t srm = {};
    srm.in.buffer           = s_rgb_buf;
    srm.in.pic_w            = aligned_w;   // faktyczny stride w buforze
    srm.in.pic_h            = aligned_h;   // faktyczna wysokosc bufora
    srm.in.block_w          = src_w;       // oryginalna szerokosc obrazu
    srm.in.block_h          = src_h;       // oryginalna wysokosc obrazu
    srm.in.block_offset_x   = 0;
    srm.in.block_offset_y   = 0;
    srm.in.srm_cm           = PPA_SRM_COLOR_MODE_RGB565;
    srm.out.buffer          = s_out_buf;
    srm.out.buffer_size     = kOutBufMax;
    srm.out.pic_w           = dst_w;
    srm.out.pic_h           = dst_h;
    srm.out.block_offset_x  = 0;
    srm.out.block_offset_y  = 0;
    srm.out.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
    srm.rotation_angle      = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x             = (float)dst_w / src_w;
    srm.scale_y             = (float)dst_h / src_h;
    srm.mirror_x            = false;
    srm.mirror_y            = false;
    srm.rgb_swap            = false;
    srm.byte_swap           = true;  // HW JPEG decoder -> big-endian RGB565, LVGL -> little-endian
    srm.mode                = PPA_TRANS_MODE_BLOCKING;

    err = ppa_do_scale_rotate_mirror(ppa_client, &srm);
    ppa_unregister_client(ppa_client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ppa scale failed: %s", esp_err_to_name(err));
        return nullptr;
    }

    // 4. Wypelnij deskryptor obrazu LVGL
    s_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.w      = dst_w;
    s_img_dsc.header.h      = dst_h;
    s_img_dsc.header.stride = dst_w * 2;
    s_img_dsc.data_size     = (uint32_t)dst_w * dst_h * 2;
    s_img_dsc.data          = s_out_buf;

    ESP_LOGI(TAG, "snap OK: %s  src=%ux%u dst=%ux%u",
             entity_id, src_w, src_h, dst_w, dst_h);
    return &s_img_dsc;
}
