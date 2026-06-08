#include "speaker.h"
#include "speaker_sched.h"

#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "speaker";

// ---------------------------------------------------------------------------
// Konfiguracja audio — musi zgadzac sie z BSP_I2S_DUPLEX_MONO_CFG(22050)
// ---------------------------------------------------------------------------
#define SAMPLE_RATE      22050
#define BITS_PER_SAMPLE  16
#define CHANNELS         1       // Mono — I2S_SLOT_MODE_MONO w BSP
#define DEF_VOLUME       70      // Domyslna glosnosc (0-100)

// Bufor na najdluzszy pojedynczy dzwiek: 600ms
// 22050 Hz * 0,6 s * 2 B = 26 460 B — alokujemy z PSRAM
#define BUF_MS      600
#define BUF_SAMPLES ((SAMPLE_RATE * BUF_MS) / 1000)   // 13 230
#define BUF_BYTES   (BUF_SAMPLES * 2)                  // 26 460 B (int16_t)

// ---------------------------------------------------------------------------
// Stan modulu
// ---------------------------------------------------------------------------
static esp_codec_dev_handle_t s_spk_dev   = nullptr;
static QueueHandle_t          s_queue     = nullptr;
static int16_t               *s_buf       = nullptr;
static uint8_t                s_volume    = DEF_VOLUME;
// Mutex chroniacy kodek — trzymany tylko podczas aktywnego odtwarzania tonow
// Zwalniany podczas oczekiwania na kolejce, zeby TTS mogl przejac kodek
static SemaphoreHandle_t      s_dev_mutex = nullptr;

// ---------------------------------------------------------------------------
// Definicje melodii — { freq_hz, duration_ms }
// freq_hz == 0 oznacza pauze (cisza)
// ---------------------------------------------------------------------------
struct Note { uint32_t freq; uint32_t ms; };

// Beep — krotki pisk potwierdzenia
static const Note kBeep[] = {
    { 880, 90 },
};

// Chime — dwunutowy dzwon (E5 → G5)
static const Note kChime[] = {
    { 659, 180 },
    {   0,  40 },  // pauza
    { 784, 240 },
};

// Alert — narastajacy trojdzwiek (A4 C5 E5)
static const Note kAlert[] = {
    { 440, 110 },
    {   0,  25 },
    { 523, 110 },
    {   0,  25 },
    { 659, 150 },
};

// Error — opadajace dwa tony (E5 → A4)
static const Note kError[] = {
    { 659, 150 },
    {   0,  40 },
    { 440, 220 },
};

struct SoundDef { const Note *notes; size_t count; };
static const SoundDef kSounds[] = {
    { kBeep,  sizeof(kBeep)  / sizeof(kBeep[0])  },  // SpeakerSound::Beep
    { kChime, sizeof(kChime) / sizeof(kChime[0]) },  // SpeakerSound::Chime
    { kAlert, sizeof(kAlert) / sizeof(kAlert[0]) },  // SpeakerSound::Alert
    { kError, sizeof(kError) / sizeof(kError[0]) },  // SpeakerSound::Error
};

// ---------------------------------------------------------------------------
// Generowanie sinusoidy z obwiednia (fade-in 5ms / fade-out 10ms)
// Eliminuje "klikniecia" na poczatku i koncu kazdej nuty.
// ---------------------------------------------------------------------------
static void generate_note(int16_t *buf, size_t n_samples, uint32_t freq_hz)
{
    const float amp  = 32767.0f * s_volume / 100.0f;
    const float dphi = (float)(2.0 * M_PI * freq_hz) / SAMPLE_RATE;
    float phase = 0.0f;
    for (size_t i = 0; i < n_samples; ++i) {
        buf[i] = (int16_t)(amp * sinf(phase));
        phase += dphi;
        if (phase >= (float)(2.0 * M_PI)) phase -= (float)(2.0 * M_PI);
    }

    // Obwiednia liniowa: fade-in 5ms, fade-out 10ms
    const size_t fi = (SAMPLE_RATE * 5)  / 1000;   // 110 prob
    const size_t fo = (SAMPLE_RATE * 10) / 1000;   // 220 prob
    const size_t fi_capped = (fi + fo < n_samples) ? fi : n_samples / 4;
    const size_t fo_capped = (fi + fo < n_samples) ? fo : n_samples / 4;

    for (size_t i = 0; i < fi_capped; ++i)
        buf[i] = (int16_t)((int32_t)buf[i] * (int32_t)i / (int32_t)fi_capped);

    for (size_t i = 0; i < fo_capped; ++i) {
        size_t idx = n_samples - fo_capped + i;
        buf[idx] = (int16_t)((int32_t)buf[idx]
                              * (int32_t)(fo_capped - i) / (int32_t)fo_capped);
    }
}

static void play_note(const Note *note)
{
    if (!s_spk_dev || !s_buf) return;
    uint32_t n = (SAMPLE_RATE * note->ms) / 1000;
    if (n > BUF_SAMPLES) n = BUF_SAMPLES;
    if (n == 0) return;

    if (note->freq == 0) {
        memset(s_buf, 0, n * 2);  // cisza
    } else {
        generate_note(s_buf, n, note->freq);
    }
    esp_codec_dev_write(s_spk_dev, s_buf, (int)(n * 2));
}

// ---------------------------------------------------------------------------
// Task odtwarzacza — blokuje sie na kolejce, wysyla PCM do kodeka
// ---------------------------------------------------------------------------
static void player_task(void *)
{
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = BITS_PER_SAMPLE;
    fs.channel         = CHANNELS;
    fs.sample_rate     = SAMPLE_RATE;
    if (esp_codec_dev_open(s_spk_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed");
        vTaskDelete(nullptr);
        return;
    }
    esp_codec_dev_set_out_vol(s_spk_dev, s_volume);
    ESP_LOGI(TAG, "Player task ready");

    uint8_t last_vol = s_volume;

    SpeakerSound req;
    for (;;) {
        // Czekamy na dzwiek BEZ trzymania muteksu — TTS moze przejac kodek
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        uint8_t sched_vol = sched_get_volume();
        if (sched_vol == 0) continue;  // godziny ciszy

        // Przejmij kodek (TTS moze byc w trakcie — czekamy max 10s)
        xSemaphoreTake(s_dev_mutex, pdMS_TO_TICKS(10000));

        // Po przejęciu: upewnij sie ze sample rate to 22050 Hz (TTS mógł zmienić)
        // i zaktualizuj glosnosc
        if (sched_vol != last_vol) {
            esp_codec_dev_sample_info_t fs = {};
            fs.bits_per_sample = BITS_PER_SAMPLE;
            fs.channel         = CHANNELS;
            fs.sample_rate     = SAMPLE_RATE;
            esp_codec_dev_open(s_spk_dev, &fs);
            esp_codec_dev_set_out_vol(s_spk_dev, sched_vol);
            last_vol = sched_vol;
        }

        uint8_t idx = (uint8_t)req;
        if (idx < sizeof(kSounds) / sizeof(kSounds[0])) {
            const SoundDef &def = kSounds[idx];
            for (size_t i = 0; i < def.count; ++i)
                play_note(&def.notes[i]);
        }

        xSemaphoreGive(s_dev_mutex);
    }
}

// ---------------------------------------------------------------------------
// Publiczne API
// ---------------------------------------------------------------------------
esp_err_t speaker_init(void)
{
    s_spk_dev = bsp_audio_codec_speaker_init();
    if (!s_spk_dev) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init failed");
        return ESP_FAIL;
    }

    // Bufor audio preferujemy w PSRAM — 26 KB nie obciazone wewnetrznym SRAM
    s_buf = static_cast<int16_t *>(
        heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_buf)
        s_buf = static_cast<int16_t *>(malloc(BUF_BYTES));
    if (!s_buf) {
        ESP_LOGE(TAG, "Brak pamieci na bufor audio (%d B)", BUF_BYTES);
        return ESP_ERR_NO_MEM;
    }

    s_queue = xQueueCreate(4, sizeof(SpeakerSound));
    if (!s_queue) {
        heap_caps_free(s_buf);
        s_buf = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_dev_mutex = xSemaphoreCreateMutex();
    if (!s_dev_mutex) {
        heap_caps_free(s_buf);
        s_buf = nullptr;
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(player_task, "spk_player", 4096, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Glosnik OK — ES8311, MONO %d Hz %d-bit, vol=%d%%",
             SAMPLE_RATE, BITS_PER_SAMPLE, s_volume);
    return ESP_OK;
}

void speaker_play(SpeakerSound sound)
{
    if (!s_queue) return;
    // Nieblokujace — jesli kolejka pelna, pomijamy (UI nie czeka)
    xQueueSend(s_queue, &sound, 0);
}

void speaker_set_volume(uint8_t vol_0_100)
{
    s_volume = (vol_0_100 > 100) ? 100 : vol_0_100;
    if (s_spk_dev) esp_codec_dev_set_out_vol(s_spk_dev, s_volume);
}

SpeakerDevLease speaker_lock_dev(uint32_t timeout_ms)
{
    SpeakerDevLease lease = { s_spk_dev, false };
    if (!s_dev_mutex || !s_spk_dev) return lease;
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    lease.valid = (xSemaphoreTake(s_dev_mutex, ticks) == pdTRUE);
    return lease;
}

void speaker_unlock_dev(void)
{
    if (s_dev_mutex) xSemaphoreGive(s_dev_mutex);
}
