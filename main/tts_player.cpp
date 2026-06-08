#include "tts_player.h"
#include "speaker.h"
#include "speaker_sched.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#include <string.h>

// ---------------------------------------------------------------------------
// minimp3 — jednoheaderowy dekoder MP3 (MIT License, lieff/minimp3)
// Pobierz: https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
// i wrzuc do katalogu main/
// ---------------------------------------------------------------------------
#if __has_include("minimp3.h")
#  define MINIMP3_IMPLEMENTATION
#  define MINIMP3_ONLY_MP3       // tylko MP3, bez MP1/MP2 — mniejszy kod
#  define MINIMP3_NO_SIMD        // RISC-V: bez SSE/NEON
#  include "minimp3.h"
#  define HAS_MINIMP3 1
#else
#  define HAS_MINIMP3 0
#  pragma message("minimp3.h nie znaleziony — MP3 TTS nie bedzie dzialac")
#endif

static const char *TAG = "tts";

// ---------------------------------------------------------------------------
// Konfiguracja
// ---------------------------------------------------------------------------
#define TTS_QUEUE_LEN      4
#define TTS_MAX_TEXT       256     // max dlugos tekstu TTS
#define TTS_MAX_URL        384     // max URL audio
#define TTS_HTTP_BUF       4096   // bufor HTTP streamingu (bajty)
#define TTS_HTTP_TIMEOUT   8000   // timeout HTTP (ms)

#define WAV_HEADER_SIZE    44     // standardowy RIFF PCM WAV

// ---------------------------------------------------------------------------
// Stan modulu
// ---------------------------------------------------------------------------
static char s_ha_base[128]   = {};   // np. "http://192.168.1.100:8123"
static char s_ha_token[260]  = {};   // Long-Lived Token
static char s_engine[64]     = "tts.piper";
static char s_lang[16]       = "pl-PL";
static char s_voice[64]      = {};   // np. "pl_PL-gosia-medium" (opcjonalne)

struct TtsRequest {
    bool is_url;            // true = gotowy URL, false = tekst do zapytania HA
    char payload[TTS_MAX_URL]; // URL lub tekst
};

static QueueHandle_t s_tts_queue = nullptr;

// ---------------------------------------------------------------------------
// WAV header (44 bajty, standard PCM RIFF)
// ---------------------------------------------------------------------------
struct WavHdr {
    uint8_t  riff[4];       // "RIFF"
    uint32_t file_size;
    uint8_t  wave[4];       // "WAVE"
    uint8_t  fmt_id[4];     // "fmt "
    uint32_t fmt_size;      // 16 dla PCM
    uint16_t audio_fmt;     // 1 = PCM
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
    uint8_t  data_id[4];    // "data"
    uint32_t data_size;
} __attribute__((packed));

// ---------------------------------------------------------------------------
// Pomocnicze: konwersja ws:// URL → base HTTP URL
// "ws://192.168.1.100:8123/api/websocket" → "http://192.168.1.100:8123"
// ---------------------------------------------------------------------------
static void ws_url_to_http_base(const char *ws_url, char *out, size_t out_len)
{
    const char *host = nullptr;
    bool is_secure = false;

    if (strncmp(ws_url, "wss://", 6) == 0) { host = ws_url + 6; is_secure = true; }
    else if (strncmp(ws_url, "ws://", 5) == 0) { host = ws_url + 5; }
    else { strlcpy(out, ws_url, out_len); return; }

    snprintf(out, out_len, "%s://%s", is_secure ? "https" : "http", host);

    // Obetnij sciezke /api/websocket
    char *slash = strstr(out + 8, "/");  // szukaj za "http://"
    if (slash) *slash = '\0';
}

// ---------------------------------------------------------------------------
// Krok 1: Zapytanie HA o URL audio dla danego tekstu
// Zwraca ESP_OK i wypełnia url_out gdy sukces.
// ---------------------------------------------------------------------------
static esp_err_t fetch_tts_url(const char *text,
                                char *url_out, size_t url_len)
{
    if (!s_ha_base[0] || !s_ha_token[0]) {
        ESP_LOGW(TAG, "Brak konfiguracji HA (base=%s)", s_ha_base[0] ? "ok" : "pusty");
        return ESP_ERR_INVALID_STATE;
    }

    char endpoint[160];
    snprintf(endpoint, sizeof(endpoint), "%s/api/tts_get_url", s_ha_base);

    // Buduj JSON: engine_id, message, language + prefer_wav + opcjonalny voice
    // prefer_wav=true: HA zwraca URL do pliku .wav zamiast .mp3
    // (Piper natywnie generuje WAV — HA domyslnie konwertuje do MP3 przez proxy)
    cJSON *req  = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "engine_id", s_engine);
    cJSON_AddStringToObject(req, "message",   text);
    cJSON_AddStringToObject(req, "language",  s_lang);
    cJSON_AddTrueToObject(req, "prefer_wav");   // wymuszamy WAV — ESP32 nie dekoduje MP3
    if (s_voice[0]) {
        cJSON *opts = cJSON_AddObjectToObject(req, "options");
        cJSON_AddStringToObject(opts, "voice", s_voice);
    }
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    int body_len = (int)strlen(body);
    ESP_LOGI(TAG, "tts_get_url POST %s | body: %s", endpoint, body);

    char auth[280];
    snprintf(auth, sizeof(auth), "Bearer %s", s_ha_token);

    esp_http_client_config_t cfg = {};
    cfg.url               = endpoint;
    cfg.method            = HTTP_METHOD_POST;
    cfg.timeout_ms        = TTS_HTTP_TIMEOUT;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size       = 2048;

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Authorization", auth);

    // Użyj open+write+read — pozwala odczytać body odpowiedzi (nawet gdy błąd)
    esp_err_t err = esp_http_client_open(cli, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open blad: %s", esp_err_to_name(err));
        cJSON_free(body);
        esp_http_client_cleanup(cli);
        return err;
    }
    esp_http_client_write(cli, body, body_len);
    cJSON_free(body);

    esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);

    // Zawsze czytaj body — potrzebne zarówno do URL jak i do logowania błędu
    char resp[640] = {};
    esp_http_client_read_response(cli, resp, sizeof(resp) - 1);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

    if (status != 200) {
        // Loguj pełną odpowiedź HA — to powie co jest nie tak
        ESP_LOGW(TAG, "tts_get_url status %d | odpowiedz HA: %s", status, resp);
        ESP_LOGW(TAG, "Sprawdz: engine_id='%s' lang='%s' voice='%s'",
                 s_engine, s_lang, s_voice);
        return ESP_FAIL;
    }

    // Parsuj {"url":"...","path":"..."}
    cJSON *jroot = cJSON_Parse(resp);
    if (!jroot) {
        ESP_LOGW(TAG, "Niepoprawny JSON odpowiedzi: %.120s", resp);
        return ESP_FAIL;
    }

    bool ok = false;
    cJSON *jurl = cJSON_GetObjectItemCaseSensitive(jroot, "url");
    if (cJSON_IsString(jurl) && jurl->valuestring) {
        const char *u = jurl->valuestring;
        // HA moze zwrocic relatywny path ("/api/tts_proxy/...") lub pelny URL
        if (u[0] == '/') {
            snprintf(url_out, url_len, "%s%s", s_ha_base, u);
        } else {
            strlcpy(url_out, u, url_len);
        }
        ESP_LOGI(TAG, "TTS URL: %s", url_out);
        ok = true;
    } else {
        ESP_LOGW(TAG, "Brak pola 'url' w odpowiedzi: %s", resp);
    }
    cJSON_Delete(jroot);
    return ok ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// Helpers: przywrócenie standardowego trybu kodeka po TTS
// ---------------------------------------------------------------------------
static void restore_codec_22050(esp_codec_dev_handle_t dev)
{
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel         = 1;
    fs.sample_rate     = 22050;
    esp_codec_dev_open(dev, &fs);
}

// ---------------------------------------------------------------------------
// MP3: bufferuje cały plik i dekoduje przez minimp3 ramka po ramce
// Typowy komunikat TTS (5–15 s) = 40–120 KB MP3 — wygodnie mieści się w PSRAM
// ---------------------------------------------------------------------------
#if HAS_MINIMP3
static void play_mp3(const uint8_t *data, size_t size, esp_codec_dev_handle_t dev)
{
    // mp3dec_t ma ~6.7 KB — alokujemy z PSRAM zamiast na stosie task'a
    // (mp3dec_decode_frame i tak uzywa dodatkowych ~7 KB stosu wewnetrznie)
    mp3dec_t *dec = static_cast<mp3dec_t *>(
        heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!dec) dec = static_cast<mp3dec_t *>(malloc(sizeof(mp3dec_t)));
    if (!dec) { ESP_LOGE(TAG, "MP3: OOM na mp3dec_t (%u B)", (unsigned)sizeof(mp3dec_t)); return; }
    mp3dec_init(dec);
    ESP_LOGI(TAG, "MP3: mp3dec_t=%u B na PSRAM", (unsigned)sizeof(mp3dec_t));

    // Bufor PCM: max 1152 sampli × 2 kanały × 2 bajty
    int16_t *pcm = static_cast<int16_t *>(
        heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pcm) {
        ESP_LOGE(TAG, "MP3: OOM na bufor PCM");
        free(dec);
        return;
    }

    bool codec_opened = false;
    size_t offset     = 0;
    int total_samples = 0;

    while (offset < size) {
        mp3dec_frame_info_t info = {};
        int samples = mp3dec_decode_frame(dec,
                                           data + offset,
                                           (int)(size - offset),
                                           pcm, &info);
        if (!info.frame_bytes) break;  // brak kolejnej ramki

        if (samples > 0) {
            if (!codec_opened) {
                ESP_LOGI(TAG, "MP3: %d Hz, %d kanal(y) — konfiguruje kodek",
                         info.hz, info.channels);
                esp_codec_dev_sample_info_t fs = {};
                fs.bits_per_sample = 16;
                fs.channel         = (uint8_t)info.channels;
                fs.sample_rate     = (uint32_t)info.hz;
                esp_codec_dev_open(dev, &fs);
                esp_codec_dev_set_out_vol(dev, sched_get_volume());
                codec_opened = true;
            }
            esp_codec_dev_write(dev, pcm, samples * info.channels * sizeof(int16_t));
            total_samples += samples;
        }
        offset += info.frame_bytes;
    }

    free(pcm);
    free(dec);
    ESP_LOGI(TAG, "MP3: zdekodowano %zu B → %d sampli PCM", size, total_samples);
}
#endif  // HAS_MINIMP3

// ---------------------------------------------------------------------------
// Krok 2: Pobranie audio (WAV lub MP3) i odtworzenie przez ES8311
// ---------------------------------------------------------------------------
static void stream_audio_url(const char *url)
{
    char auth[280];
    snprintf(auth, sizeof(auth), "Bearer %s", s_ha_token);

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.timeout_ms        = TTS_HTTP_TIMEOUT;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size       = TTS_HTTP_BUF;

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_header(cli, "Authorization", auth);

    if (esp_http_client_open(cli, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Nie mozna otworzyc URL audio");
        esp_http_client_cleanup(cli);
        return;
    }
    esp_http_client_fetch_headers(cli);

    if (esp_http_client_get_status_code(cli) != 200) {
        ESP_LOGW(TAG, "Audio HTTP status %d", esp_http_client_get_status_code(cli));
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return;
    }

    // Sprawdź harmonogram
    uint8_t vol = sched_get_volume();
    if (vol == 0) {
        ESP_LOGI(TAG, "TTS: godziny ciszy — pomijam");
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return;
    }

    // Czytaj pierwsze 44 bajty — ustal format (WAV: "RIFF", MP3: "ID3" / 0xFF)
    uint8_t head[44] = {};
    int head_read = esp_http_client_read(cli, (char *)head, sizeof(head));

    // ─── WAV PCM ───────────────────────────────────────────────────────────
    if (head_read >= 44 &&
        memcmp(head, "RIFF", 4) == 0 &&
        memcmp(head + 8, "WAVE", 4) == 0)
    {
        const auto *hdr = reinterpret_cast<const WavHdr *>(head);
        if (hdr->audio_fmt != 1) {
            ESP_LOGW(TAG, "WAV: nieobslugiwany format (audio_fmt=%u)", hdr->audio_fmt);
            goto cleanup;
        }
        ESP_LOGI(TAG, "WAV: %lu Hz, %u ch, %u bit",
                 (unsigned long)hdr->sample_rate, hdr->channels, hdr->bits);

        SpeakerDevLease lease = speaker_lock_dev(10000);
        if (!lease.valid) { ESP_LOGW(TAG, "TTS: timeout kodeka"); goto cleanup; }
        esp_codec_dev_handle_t dev = static_cast<esp_codec_dev_handle_t>(lease.dev);

        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = (uint8_t)hdr->bits;
        fs.channel         = (uint8_t)hdr->channels;
        fs.sample_rate     = hdr->sample_rate;
        esp_codec_dev_open(dev, &fs);
        esp_codec_dev_set_out_vol(dev, vol);

        char *buf = static_cast<char *>(
            heap_caps_malloc(TTS_HTTP_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buf) buf = static_cast<char *>(malloc(TTS_HTTP_BUF));
        int total = 0;
        if (buf) {
            int n;
            while ((n = esp_http_client_read(cli, buf, TTS_HTTP_BUF)) > 0) {
                esp_codec_dev_write(dev, buf, n);
                total += n;
            }
            free(buf);
        }
        ESP_LOGI(TAG, "WAV: odtworzono %d B", total);
        restore_codec_22050(dev);
        speaker_unlock_dev();
        goto cleanup;
    }

    // ─── MP3 (minimp3) ─────────────────────────────────────────────────────
    {
        bool is_mp3 = (head_read >= 3 &&
                       head[0] == 'I' && head[1] == 'D' && head[2] == '3') ||
                      (head_read >= 2 &&
                       head[0] == 0xFF && (head[1] & 0xE0) == 0xE0);

        if (!is_mp3) {
            ESP_LOGW(TAG, "Nieznany format audio: %02X %02X %02X %02X",
                     head[0], head[1], head[2], head[3]);
            goto cleanup;
        }

#if HAS_MINIMP3
        ESP_LOGI(TAG, "MP3: wykryto — bufferuje plik (max 384 KB z PSRAM)");

        // Bufferuj cały plik z PSRAM — typowy komunikat TTS to 20–120 KB
        const size_t MP3_MAX = 384 * 1024;
        uint8_t *mp3buf = static_cast<uint8_t *>(
            heap_caps_malloc(MP3_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!mp3buf) { ESP_LOGE(TAG, "OOM na bufor MP3 (%zu KB)", MP3_MAX/1024); goto cleanup; }

        // Wstaw już odczytane bajty nagłówka
        size_t mp3_size = (size_t)head_read;
        memcpy(mp3buf, head, mp3_size);

        // Pobierz resztę
        int n;
        while (mp3_size < MP3_MAX &&
               (n = esp_http_client_read(cli, (char *)(mp3buf + mp3_size),
                                          (int)(MP3_MAX - mp3_size))) > 0) {
            mp3_size += n;
        }
        ESP_LOGI(TAG, "MP3: pobrano %zu B", mp3_size);

        SpeakerDevLease lease = speaker_lock_dev(10000);
        if (!lease.valid) {
            ESP_LOGW(TAG, "TTS: timeout kodeka");
            free(mp3buf);
            goto cleanup;
        }
        esp_codec_dev_handle_t dev = static_cast<esp_codec_dev_handle_t>(lease.dev);

        play_mp3(mp3buf, mp3_size, dev);
        free(mp3buf);

        restore_codec_22050(dev);
        speaker_unlock_dev();
#else
        ESP_LOGE(TAG, "MP3 wykryto, ale minimp3.h nie znaleziony! "
                 "Pobierz z github.com/lieff/minimp3 i wrzuc do main/");
#endif
    }

cleanup:
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
}

// ---------------------------------------------------------------------------
// Task TTS — przetwarza kolejkę żądań
// ---------------------------------------------------------------------------
static void tts_task(void *)
{
    TtsRequest req;
    for (;;) {
        if (xQueueReceive(s_tts_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        char audio_url[TTS_MAX_URL] = {};

        if (req.is_url) {
            // Gotowy URL — graj bezpośrednio
            strlcpy(audio_url, req.payload, sizeof(audio_url));
        } else {
            // Tekst → zapytaj HA o URL TTS
            ESP_LOGI(TAG, "TTS: '%.*s'", 60, req.payload);
            if (fetch_tts_url(req.payload, audio_url, sizeof(audio_url)) != ESP_OK) {
                ESP_LOGW(TAG, "TTS: nie udalo sie pobrac URL");
                continue;
            }
        }

        stream_audio_url(audio_url);
    }
}

// ---------------------------------------------------------------------------
// Publiczne API
// ---------------------------------------------------------------------------
esp_err_t tts_init(const HaRuntimeConfig *ha_cfg,
                   const char *engine_id,
                   const char *language)
{
    // Konwertuj WS URL → HTTP base
    if (ha_cfg && ha_cfg->url[0])
        ws_url_to_http_base(ha_cfg->url, s_ha_base, sizeof(s_ha_base));
    if (ha_cfg && ha_cfg->token[0])
        strlcpy(s_ha_token, ha_cfg->token, sizeof(s_ha_token));

    // Załaduj konfigurację z NVS
    {
        nvs_handle_t nvs = 0;
        if (nvs_open("tts_cfg", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len;
            len = sizeof(s_engine); nvs_get_str(nvs, "engine", s_engine, &len);
            len = sizeof(s_lang);   nvs_get_str(nvs, "lang",   s_lang,   &len);
            len = sizeof(s_voice);  nvs_get_str(nvs, "voice",  s_voice,  &len);
            nvs_close(nvs);
        } else {
            // Brak wpisu NVS → użyj podanych parametrów
            if (engine_id && engine_id[0]) strlcpy(s_engine, engine_id, sizeof(s_engine));
            if (language  && language[0])  strlcpy(s_lang,   language,  sizeof(s_lang));
        }
    }

    s_tts_queue = xQueueCreate(TTS_QUEUE_LEN, sizeof(TtsRequest));
    if (!s_tts_queue) return ESP_ERR_NO_MEM;

    // Stack 32 KB: minimp3 używa ~7 KB (mp3d_scratch_t) + mp3dec_t na heap + inne ramki
    xTaskCreate(tts_task, "tts_task", 32768, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "TTS init: silnik=%s, jezyk=%s, voice=%s, base=%s",
             s_engine, s_lang, s_voice[0] ? s_voice : "(brak)", s_ha_base);
    return ESP_OK;
}

esp_err_t tts_speak(const char *text)
{
    if (!s_tts_queue || !text || !text[0]) return ESP_ERR_INVALID_ARG;

    TtsRequest req = {};
    req.is_url = false;
    strlcpy(req.payload, text, sizeof(req.payload));

    if (xQueueSend(s_tts_queue, &req, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "TTS: kolejka pelna");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tts_play_url(const char *url)
{
    if (!s_tts_queue || !url || !url[0]) return ESP_ERR_INVALID_ARG;

    TtsRequest req = {};
    req.is_url = true;
    strlcpy(req.payload, url, sizeof(req.payload));

    if (xQueueSend(s_tts_queue, &req, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "TTS URL: kolejka pelna");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tts_set_engine(const char *engine_id, const char *language,
                          const char *voice)
{
    if (engine_id && engine_id[0]) strlcpy(s_engine, engine_id, sizeof(s_engine));
    if (language  && language[0])  strlcpy(s_lang,   language,  sizeof(s_lang));
    // voice = "" to clear; nullptr to leave unchanged
    if (voice != nullptr)          strlcpy(s_voice,  voice,     sizeof(s_voice));

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("tts_cfg", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_str(nvs, "engine", s_engine);
        nvs_set_str(nvs, "lang",   s_lang);
        nvs_set_str(nvs, "voice",  s_voice);
        err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "TTS config: silnik=%s, jezyk=%s, voice=%s",
             s_engine, s_lang, s_voice[0] ? s_voice : "(brak)");
    return err;
}

void tts_get_engine(char *engine_id, size_t eid_len,
                    char *language,  size_t lang_len,
                    char *voice,     size_t voice_len)
{
    if (engine_id) strlcpy(engine_id, s_engine, eid_len);
    if (language)  strlcpy(language,  s_lang,   lang_len);
    if (voice)     strlcpy(voice,     s_voice,  voice_len);
}
