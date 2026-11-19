#include "Sonometer.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Sonometer"
#include "esp_lib_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "bsp_board_extra.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// EEZ Studio generates C code; pull its entry point in with C linkage.
extern "C" {
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"
    LV_IMG_DECLARE(sonometer_icon);
}

static_assert(SOUND_ADC_PGA_GAIN_DB == CODEC_DEFAULT_ADC_VOLUME, "SOUND_ADC_PGA_GAIN_DB must match the gain bsp_extra applies (CODEC_DEFAULT_ADC_VOLUME)");

namespace {

static const char *TAG = "Sonometer";

// Sound task lifecycle: started by run(), stopped by close()
static volatile bool s_running = false;
static volatile TaskHandle_t s_sound_task = nullptr;
static int32_t *s_rx_buf = nullptr;                     // I2S_MIC_READ_FRAMES stereo int32 frames

// Sound graph: peak level of the current period, written by sound_task, read and reset by graph_timer_cb
static SemaphoreHandle_t graph_mux = nullptr;           // protects graph_peak_db and graph_has_value
static float graph_peak_db = 0.0f;                      // highest level since graph_timer_cb last read it
static bool graph_has_value = false;                    // at least one level since graph_timer_cb last read it
static lv_chart_series_t *graph_series = nullptr;       // owned by graph_timer_cb (LVGL task), reset on every run()

// LVGL timers, live only while the app is open
static lv_timer_t *s_ui_timer = nullptr;
static lv_timer_t *s_graph_timer = nullptr;

// EEZ variables: written by sound_task, read by Sonometer_ui_tick() in the LVGL task
static volatile int32_t s_meter_value = 0;
static char s_sound_level[16] = {0};                    // written under s_text_lock
static char s_sound_level_ui[16] = {0};                 // LVGL-side copy, so a label never reads a half-written string
static portMUX_TYPE s_text_lock = portMUX_INITIALIZER_UNLOCKED;

/*
@brief Mean square of the 94 dB SPL reference at the ADC output, normalised to [-1, 1)
@note Same form as the INMP441 version ((0.050118723 * 0.70710678)^2 for -26 dBFS), with the dBFS value derived from the hardware
*/
static float sound_ref_ms(void) {
    const float ref_dbfs = SOUND_MIC_SENSITIVITY_DBV + SOUND_ADC_PGA_GAIN_DB - 20.0f * log10f(SOUND_ADC_FULL_SCALE_VRMS);
    const float amp = powf(10.0f, ref_dbfs / 20.0f) * 0.70710678f;   // full-scale sine amplitude -> rms
    return amp * amp;
}

/*
@brief Opens the ES7210 at 48 kHz / 32-bit stereo through bsp_extra (also applies the PGA gain)
*/
static esp_err_t sound_input_start(void) {
    esp_err_t err = bsp_extra_codec_init();                                 // no-op if another app already did it
    if (err != ESP_OK) return err;
    return bsp_extra_codec_set_fs(I2S_MIC_SAMPLE_RATE, I2S_MIC_BITS_PER_CHANNEL, I2S_SLOT_MODE_STEREO);
}

/*
@brief Keeps the peak level of the current graph period, called by sound_task after each update
@note Short timeout: the audio loop must never stall on the graph
*/
static void sound_graph_publish(float db) {
    if (graph_mux == nullptr) return;
    if (xSemaphoreTake(graph_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (!graph_has_value || db > graph_peak_db) graph_peak_db = db;
        graph_has_value = true;
        xSemaphoreGive(graph_mux);
    }
}

/*
@brief Pushes the peak level of each SOUND_GRAPH_PERIOD_MS into Sonometer_objects.sound_graph
@note LVGL timer: runs in the LVGL task, so the chart calls need no lock
*/
static void graph_timer_cb(lv_timer_t *timer) {
    (void)timer;
    lv_obj_t *chart = Sonometer_objects.sound_graph;
    if (chart == nullptr || graph_mux == nullptr) return;                  // the UI has no sound_graph chart

    // read the peak and reset it in one step, so the next period starts fresh
    float db = 0.0f;
    bool has_value = false;
    if (xSemaphoreTake(graph_mux, portMAX_DELAY) == pdTRUE) {
        db = graph_peak_db;
        has_value = graph_has_value;
        graph_has_value = false;
        xSemaphoreGive(graph_mux);
    }

    int32_t value;
    if (has_value) {
        int32_t v = (int32_t)lroundf(db);
        if (v < SOUND_LEVEL_MINIMUM) v = SOUND_LEVEL_MINIMUM;
        if (v > SOUND_LEVEL_MAXIMUM) v = SOUND_LEVEL_MAXIMUM;
        value = v;
    } else if (graph_series == nullptr) {
        return;                                                             // sonometer still warming up: graph stays empty
    } else {
        value = LV_CHART_POINT_NONE;                                        // no level this period (I2S error): leave a gap
    }

    if (graph_series == nullptr) {                                          // the UI creates the chart without a series
        graph_series = lv_chart_add_series(chart, lv_color_hex(SOUND_GRAPH_COLOR_HEX), LV_CHART_AXIS_PRIMARY_Y);
    }
    if (graph_series != nullptr) {
        lv_chart_set_next_value(chart, graph_series, value);               // shift mode: scrolls left
    }
}

/*
@brief Chart settings of the standalone sonometer, in LVGL 9.2 form (was hand-written in screens.c)
@note LVGL 9 has no chart axis ticks (lv_chart_set_axis_tick): dB labels need an lv_scale next to the chart
*/
static void sound_graph_setup(lv_obj_t *chart) {
    if (chart == nullptr) return;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, -SOUND_GRAPH_POINTS, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, SOUND_GRAPH_Y_MIN, SOUND_GRAPH_Y_MAX);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);                 // no point markers
    lv_chart_set_point_count(chart, SOUND_GRAPH_POINTS);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);           // new points enter on the right
    lv_chart_set_div_line_count(chart, SOUND_GRAPH_DIV_H, SOUND_GRAPH_DIV_V);
}

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    Sonometer_ui_tick();                                                    // EEZ: pushes meter_value / sound_level into the widgets
}

} // namespace

// ===========================================================================
// EEZ native variables (were in display_core.c)
// ===========================================================================
extern "C" {

int32_t get_var_meter_value() {
    return s_meter_value;
}
void set_var_meter_value(int32_t value) {
    s_meter_value = value;
}

const char *get_var_sound_level() {
    portENTER_CRITICAL(&s_text_lock);
    memcpy(s_sound_level_ui, s_sound_level, sizeof(s_sound_level_ui));
    portEXIT_CRITICAL(&s_text_lock);
    return s_sound_level_ui;
}
void set_var_sound_level(const char *value) {
    portENTER_CRITICAL(&s_text_lock);
    strncpy(s_sound_level, value, sizeof(s_sound_level) - 1);
    portEXIT_CRITICAL(&s_text_lock);
}

} // extern "C"

namespace {

void sound_task(void *args) {
    (void)args;

    if (sound_input_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the ES7210 input, exiting Sound core");
        s_sound_task = nullptr;
        vTaskDelete(NULL);
        return;
    }

    float decibels = 0.0f;
    int32_t decibels_int = 0;
    char text[16] = {0};

    // State kept between windows for as long as the app is open (function-static in the standalone version)
    float aw_state[3][2] = {{0.0f}};                                        // filter memory, kept between windows
    size_t warmup_left = I2S_MIC_SAMPLE_RATE / 2;                           // drop 500 ms: codec start + filter settling
    float fast_ms = 0.0f;                                                   // Fast-weighted mean square
    bool fast_seeded = false;

    while (s_running) {
        if (get_var_meter_value() >= SOUND_LEVEL_MAXIMUM) set_var_meter_value(SOUND_LEVEL_MINIMUM);
        // A-weighted, Fast time-weighted level from the ES7210 (32-bit slots, 24-bit data left-aligned, stereo)
        // Updates SOUND_UPDATE_RATE_HZ times per second; the loop is paced by the blocking I2S read
        const size_t SONO_WINDOW = I2S_MIC_SAMPLE_RATE / SOUND_UPDATE_RATE_HZ;  // samples per update (4800 @ 10 Hz)
        const float FAST_TAU_S = 0.125f;                                    // "Fast" time constant
        const float FAST_ALPHA = 1.0f - expf(-1.0f / ((float)SOUND_UPDATE_RATE_HZ * FAST_TAU_S));

        // A-weighting, 3 biquads @ 48 kHz: {b0, b1, b2, a1, a2}, also removes the mic's DC offset
        static const float AW[3][5] = {
            { 5.6163625296e-01f,  3.5898683987e-01f, 5.6869113538e-02f, -1.7453986961e-01f, 7.6160415207e-03f },
            { 1.0f, -2.0f, 1.0f, -1.8966451036e+00f, 8.9793579293e-01f },
            { 1.0f, -2.0f, 1.0f, -1.9946784588e+00f, 9.9468553762e-01f },
        };
        // 94 dB SPL reference mean square, from the mic sensitivity, PGA gain and ADC full scale
        static const float REF_MS = sound_ref_ms();

        float sum_sq = 0.0f;
        size_t n = 0;
        bool read_ok = true;

        while (n < SONO_WINDOW && s_running) {
            // request only what completes this window, so every update is exactly SONO_WINDOW samples
            size_t need = (SONO_WINDOW - n) + warmup_left;
            size_t want = std::min<size_t>(I2S_MIC_READ_FRAMES, need) * I2S_MIC_FRAME_BYTES;
            size_t bytes_read = 0;
            esp_err_t err = bsp_extra_i2s_read(s_rx_buf, want, &bytes_read, 50);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "I2S read failed: %d", (int)err);
                read_ok = false;
                vTaskDelay(pdMS_TO_TICKS(100));                             // no busy loop if I2S keeps failing
                break;
            }

            const size_t count = bytes_read / I2S_MIC_FRAME_BYTES;
            for (size_t i = 0; i < count; i++) {
                // 24-bit ADC result, already left-aligned in the 32-bit slot (keeps sign) -> [-1, 1)
                int32_t s = s_rx_buf[i * I2S_MIC_CHANNELS + SOUND_MIC_CHANNEL];
                float x = (float)s * (1.0f / 2147483648.0f);

                for (int k = 0; k < 3; k++) {                               // transposed direct form II
                    float y = AW[k][0] * x + aw_state[k][0];
                    aw_state[k][0] = AW[k][1] * x - AW[k][3] * y + aw_state[k][1];
                    aw_state[k][1] = AW[k][2] * x - AW[k][4] * y;
                    x = y;
                }

                if (warmup_left > 0) {
                    warmup_left--;
                    continue;
                }
                sum_sq += x * x;
                n++;
            }
        }

        if (read_ok && n > 0) {
            float block_ms = sum_sq / (float)n;                             // Leq of this block
            if (!fast_seeded) {
                fast_ms = block_ms;
                fast_seeded = true;
            } else {
                fast_ms += FAST_ALPHA * (block_ms - fast_ms);               // exponential "Fast" weighting
            }

            float ms = (fast_ms < 1e-20f) ? 1e-20f : fast_ms;
            decibels = 94.0f + 10.0f * log10f(ms / REF_MS);                 // calibrated reference, no correction

            decibels_int = (int32_t)lroundf(decibels);
            if (decibels_int < SOUND_LEVEL_MINIMUM) decibels_int = SOUND_LEVEL_MINIMUM;
            if (decibels_int > SOUND_LEVEL_MAXIMUM) decibels_int = SOUND_LEVEL_MAXIMUM;
            snprintf(text, sizeof(text), "%0.1fdB", decibels);
            sound_graph_publish(decibels);                                  // feeds the per-second peak graph
        }

        set_var_meter_value(decibels_int);
        set_var_sound_level((const char*)text);
    }

    bsp_extra_codec_dev_stop();                                             // powers the codecs down until the next run()
    s_sound_task = nullptr;
    vTaskDelete(NULL);
}

/*
@brief Waits for sound_task to leave, bounded: called from the LVGL task
*/
static bool sound_wait_stopped(void) {
    for (int t = 0; s_sound_task != nullptr && t < SOUND_TASK_STOP_TIMEOUT_MS; t += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return s_sound_task == nullptr;
}

static void sound_start(void) {
    if (!sound_wait_stopped()) {                                            // previous run still closing
        ESP_LOGE(TAG, "Previous sound task did not stop, not restarting");
        return;
    }
    if (graph_mux == nullptr) {
        graph_mux = xSemaphoreCreateMutex();
        if (graph_mux == nullptr) ESP_LOGE(TAG, "Failed to create the graph mutex, continuing without the graph");
    }
    if (s_rx_buf == nullptr) {
        s_rx_buf = (int32_t *)heap_caps_malloc(I2S_MIC_READ_FRAMES * I2S_MIC_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_rx_buf == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate the I2S buffer");
            return;
        }
    }
    graph_has_value = false;

    s_running = true;
    // the handle is written before the task can run, so a task that exits at once still clears it last
    if (xTaskCreatePinnedToCore(sound_task, "sound", SOUND_TASK_STACK_SIZE, nullptr,
                                SOUND_TASK_PRIORITY, (TaskHandle_t *)&s_sound_task, SOUND_TASK_CORE) != pdPASS) {
        s_sound_task = nullptr;
        s_running = false;
        ESP_LOGE(TAG, "Failed to start the sound task");
    }
}

static void sound_stop(void) {
    s_running = false;
    if (!sound_wait_stopped()) ESP_LOGW(TAG, "Sound task still running after %d ms", SOUND_TASK_STOP_TIMEOUT_MS);
}

} // namespace

// ===========================================================================
// Brookesia App lifecycle
// ===========================================================================
namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "Sonometer";

Sonometer *Sonometer::_instance = nullptr;

Sonometer *Sonometer::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new Sonometer(use_status_bar, use_navigation_bar);
    return _instance;
}

Sonometer::Sonometer(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &sonometer_icon,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar) {}

Sonometer::~Sonometer() { _instance = nullptr; }

bool Sonometer::run()
{
    Sonometer_ui_init();                                                    // EEZ Studio entry. Builds widgets on the active screen.
    sound_graph_setup(Sonometer_objects.sound_graph);
    graph_series = nullptr;                                                 // new chart: its series is added on the first point

    if (s_ui_timer == nullptr) s_ui_timer = lv_timer_create(ui_timer_cb, SOUND_UI_TICK_MS, nullptr);
    if (s_graph_timer == nullptr) s_graph_timer = lv_timer_create(graph_timer_cb, SOUND_GRAPH_PERIOD_MS, nullptr);

    sound_start();
    return true;
}

bool Sonometer::back()
{
    notifyCoreClosed();
    return true;
}

bool Sonometer::close()
{
    // Stop touching widgets that are about to be destroyed, then stop the microphone
    if (s_ui_timer != nullptr) {
        lv_timer_delete(s_ui_timer);
        s_ui_timer = nullptr;
    }
    if (s_graph_timer != nullptr) {
        lv_timer_delete(s_graph_timer);
        s_graph_timer = nullptr;
    }
    graph_series = nullptr;
    sound_stop();
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Sonometer, APP_NAME, []()
{
    return std::shared_ptr<Sonometer>(Sonometer::requestInstance(), [](Sonometer *p) {});
})

} // namespace esp_brookesia::apps
