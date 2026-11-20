#include "Sonometer.hpp"
#include "Settings.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Sonometer"
#include "esp_lib_utils.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "src/widgets/scale/lv_scale_private.h"                    // section ranges: LVGL 9.2 has no public getter

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
#include "Sonometer_actions.h"                                          // EEZ actions (no actions.h in the export)

static_assert(SOUND_ADC_PGA_GAIN_DB == CODEC_DEFAULT_ADC_VOLUME,
              "SOUND_ADC_PGA_GAIN_DB must match the gain bsp_extra applies (CODEC_DEFAULT_ADC_VOLUME)");

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
static lv_timer_t *s_fft_timer = nullptr;

// FFT graph: raw samples kept by sound_task, spectrum computed by fft_task, rendered by fft_timer_cb, drawn by fft_draw_cb
static volatile TaskHandle_t s_fft_task = nullptr;
static SemaphoreHandle_t fft_mux = nullptr;             // protects the sample ring and the published spectrum
static float *s_fft_ring = nullptr;                     // last FFT_SIZE raw samples, oldest at s_fft_ring_pos
static uint32_t s_fft_ring_pos = 0;
static float *s_fft_win = nullptr;                      // Hann window, FFT_SIZE
static float s_fft_win_power = 0.0f;                    // sum of the squared window, for the dB calibration
static float s_fft_win_sum = 0.0f;                      // sum of the window, for the peak (pure tone) levels
static float *s_fft_buf = nullptr;                      // complex work buffer, 2 * FFT_SIZE floats, 16-byte aligned (esp-dsp)
static float *s_fft_edges = nullptr;                    // bar edges in bins, FFT_BINS + 1, owned by fft_task
static float *s_fft_out = nullptr;                      // published spectrum: one dB value per column
static uint32_t s_fft_out_bars = 0;                     // bar count the published spectrum was computed for
static uint32_t s_fft_out_cols = 0;                     // columns in s_fft_out
static uint32_t s_fft_out_seq = 0;                      // +1 per published spectrum
static uint32_t s_fft_shown_seq = 0;                    // last spectrum copied into the chart (LVGL task)
static volatile bool s_fft_active = false;              // FFT tab shown (fft_timer_cb): the FFT path idles otherwise
static volatile uint32_t s_fft_bars = FFT_GRAPH_BARS_DEFAULT;   // selected bar count (LVGL task)
static volatile uint32_t s_fft_plot_w = 0;              // fft_graph plot width in px = max columns (LVGL task)
static volatile float s_fft_f_lo = FFT_GRAPH_F_MIN;     // Hz, left end of the log axis (LVGL task)
static volatile float s_fft_f_hi = FFT_GRAPH_F_MAX;     // Hz, right end of the log axis (LVGL task)
static lv_image_dsc_t s_fft_img;                        // the bars, as one RGB565A8 image the size of fft_graph's plot area
static uint8_t *s_fft_img_buf = nullptr;                // RGB565 plane (bar colour, filled once) then A8 plane (the bars)
static int32_t s_fft_img_w = 0;
static int32_t s_fft_img_h = 0;
static float *s_fft_view = nullptr;                     // shown spectrum, one dB value per column (LVGL task)
static uint32_t s_fft_view_cols = 0;                    // columns of the shown spectrum (LVGL task)
static int32_t *s_fft_top = nullptr;                    // first lit row of each pixel column as drawn in the mask now (LVGL task)
static char s_fft_label_text[FFT_SCALE_LABELS_MAX][16]; // fft_frequency_scale labels, computed from the log axis
static const char *s_fft_labels[FFT_SCALE_LABELS_MAX + 1];
static int32_t s_fft_db_min = FFT_GRAPH_DB_MIN;         // dB, bottom of fft_graph, fft_sens_slider left knob (LVGL task)
static int32_t s_fft_db_max = FFT_GRAPH_DB_MAX;         // dB, top of fft_graph, fft_sens_slider right knob (LVGL task)
static constexpr float FFT_DB_EMPTY = -1000.0f;         // column with no spectrum yet: below any range

// fft_frequency_scale sections, drawn by fft_draw_cb as bands over the plot (see fft_scale_set_log)
struct fft_band_t {
    int32_t u_lo;                                       // scale units (fft_hz_to_units)
    int32_t u_hi;
    lv_color_t color;
    lv_opa_t opa;
};
static fft_band_t s_fft_bands[FFT_SCALE_BANDS_MAX];
static uint32_t s_fft_band_count = 0;
static int32_t s_fft_units_lo = 0;                      // scale range, in units, left end of the plot
static int32_t s_fft_units_hi = 0;                      // right end of the plot

// FFT peaks: found by fft_task, shown in fft_peaks_list by fft_timer_cb
struct fft_peak_t {
    float hz;
    float db;                                           // dB SPL of a pure tone at hz
};
static fft_peak_t s_fft_peaks_out[FFT_PEAKS_MAX];       // published with the spectrum, strongest first (fft_mux)
static uint32_t s_fft_peaks_out_n = 0;
static volatile bool s_fft_peaks_on = true;             // !fft_peaks_list_disabled: fft_task skips the search otherwise
static lv_obj_t *s_fft_peak_labels[FFT_PEAKS_MAX];      // one label per fft_peaks_list cell (LVGL task)
static lv_color_t s_fft_peak_colors[FFT_PEAKS_MAX];     // colour of each cell, red -> blue (LVGL task)

// Bars of the listed peaks, drawn in their cell's colour by fft_draw_cb (LVGL task)
struct fft_peak_mark_t {
    int32_t x0;                                         // image pixel columns [x0, x1) of the bar (gap column excluded)
    int32_t x1;
    uint32_t rank;                                      // cell index: colour s_fft_peak_colors[rank]
};
static fft_peak_mark_t s_fft_marks[FFT_PEAKS_MAX];
static uint32_t s_fft_mark_n = 0;
static uint32_t s_fft_peak_rows = 0;
static uint32_t s_fft_peaks_tick = 0;                   // lv_tick of the last list update

// EEZ variables of the Options tab: LVGL task only (actions, callbacks, Sonometer_ui_tick)
static char s_fft_sens_text[32] = {0};                  // fft_sens_value_label: "16dB to 54dB"
static bool s_fft_peaks_disabled = false;

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
@brief Columns drawn for a bar count: one per bar, or one per plot pixel when the bars outnumber the pixels
@note Bars narrower than a pixel cannot be told apart: a pixel column shows the highest bar it covers (reduced by fft_task)
*/
static uint32_t fft_columns(uint32_t bars, uint32_t plot_w) {
    return (plot_w > 0 && bars > plot_w) ? plot_w : bars;
}

/*
@brief Frequency (Hz) -> fft_frequency_scale value: FFT_SCALE_UNITS_PER_OCTAVE per octave, equal ratios = equal distances
*/
static int32_t fft_hz_to_units(float hz) {
    return (int32_t)lroundf((float)FFT_SCALE_UNITS_PER_OCTAVE * log2f(std::max(hz, 1.0f)));
}

/*
@brief Scale label of a frequency: ISO 266 third-octave name (31.5, 63, 1.25k...) when within 3.5 %, else 2 significant digits
*/
static void fft_format_hz(float hz, char *out, size_t len) {
    static const float R10[10] = {1.0f, 1.25f, 1.6f, 2.0f, 2.5f, 3.15f, 4.0f, 5.0f, 6.3f, 8.0f};
    const float steps = 10.0f * log10f(hz);                                 // tenths of a decade
    const float nearest = roundf(steps);
    float v;
    if (fabsf(steps - nearest) < 0.15f) {
        const int k = (int)nearest;
        const int decade = (k >= 0) ? k / 10 : (k - 9) / 10;
        v = R10[k - decade * 10] * powf(10.0f, (float)decade);
    } else {
        const float unit = powf(10.0f, floorf(log10f(hz)) - 1.0f);
        v = roundf(hz / unit) * unit;
    }
    if (v >= 1000.0f) snprintf(out, len, "%gk", v / 1000.0f);
    else snprintf(out, len, "%g", v);
}

/*
@brief Turns fft_frequency_scale (range in Hz, set in the UI) into a log axis over the same span
@note lv_scale only spaces its ticks linearly: its range and sections move to log units (fft_hz_to_units),
      and one label per major tick is computed from the frequency that tick now stands for
@note Sections: LVGL 9.2.2 draws a section's main style on horizontal scales between uninitialised x positions
      (scale_store_section_line_tick_width_compensation only records the tick's y), so nothing shows. The section line
      is hidden on the scale (zero opacity) and fft_draw_cb draws each section as a band over the plot height instead,
      with the section's line colour and opacity, from its start to its end frequency
*/
static void fft_scale_set_log(lv_obj_t *scale, float f_lo, float f_hi) {
    static lv_style_t hidden;                                               // section line skipped by lv_draw_line
    static bool hidden_ready = false;
    if (!hidden_ready) {
        lv_style_init(&hidden);
        lv_style_set_line_opa(&hidden, LV_OPA_TRANSP);
        hidden_ready = true;
    }

    lv_scale_t *s = (lv_scale_t *)scale;
    s_fft_band_count = 0;
    for (auto *sec = (lv_scale_section_t *)lv_ll_get_head(&s->section_ll); sec != nullptr;
         sec = (lv_scale_section_t *)lv_ll_get_next(&s->section_ll, sec)) {
        const int32_t u_lo = fft_hz_to_units((float)sec->minor_range);
        const int32_t u_hi = fft_hz_to_units((float)sec->major_range);
        lv_scale_section_set_range(sec, u_lo, u_hi);

        lv_style_t *style = sec->main_style;
        if (style != nullptr && style != &hidden && s_fft_band_count < FFT_SCALE_BANDS_MAX) {
            lv_style_value_t v;
            fft_band_t &band = s_fft_bands[s_fft_band_count++];
            band.u_lo = u_lo;
            band.u_hi = u_hi;
            band.color = (lv_style_get_prop(style, LV_STYLE_LINE_COLOR, &v) == LV_STYLE_RES_FOUND)
                         ? v.color : lv_obj_get_style_line_color(scale, LV_PART_MAIN);
            band.opa = (lv_style_get_prop(style, LV_STYLE_LINE_OPA, &v) == LV_STYLE_RES_FOUND)
                       ? (lv_opa_t)v.num : lv_obj_get_style_line_opa(scale, LV_PART_MAIN);
        }
        lv_scale_section_set_style(sec, LV_PART_MAIN, &hidden);
    }
    s_fft_units_lo = fft_hz_to_units(f_lo);
    s_fft_units_hi = fft_hz_to_units(f_hi);
    lv_scale_set_range(scale, s_fft_units_lo, s_fft_units_hi);

    const int32_t ticks = lv_scale_get_total_tick_count(scale);
    const int32_t major_every = lv_scale_get_major_tick_every(scale);
    size_t n = 0;
    if (ticks > 1 && major_every > 0) {
        for (int32_t i = 0; i < ticks && n < FFT_SCALE_LABELS_MAX; i += major_every) {
            const float hz = f_lo * powf(f_hi / f_lo, (float)i / (float)(ticks - 1));
            fft_format_hz(hz, s_fft_label_text[n], sizeof(s_fft_label_text[n]));
            s_fft_labels[n] = s_fft_label_text[n];
            n++;
        }
    }
    s_fft_labels[n] = nullptr;
    lv_scale_set_text_src(scale, s_fft_labels);
}

/*
@brief Number shown by a dropdown's selected option, fallback without the dropdown or a number
*/
static uint32_t fft_dropdown_number(lv_obj_t *dropdown, uint32_t fallback) {
    if (dropdown == nullptr) return fallback;
    char text[16] = {0};
    lv_dropdown_get_selected_str(dropdown, text, sizeof(text));
    const unsigned long n = strtoul(text, nullptr, 10);
    return (n > 0) ? (uint32_t)n : fallback;
}

/*
@brief Bar count chosen in Sonometer_objects.n_bars_selector (its option text), FFT_GRAPH_BARS_DEFAULT without it
*/
static uint32_t fft_selected_bars(void) {
    return fft_dropdown_number(Sonometer_objects.n_bars_selector, FFT_GRAPH_BARS_DEFAULT);
}

/*
@brief Sets both knobs of a range slider (values clamped to its range). LVGL 9.2 keeps left <= right on every call
       (lv_bar_set_start_value clamps to the current value, lv_bar_set_value to the start value): order by direction
*/
static void range_slider_set(lv_obj_t *slider, int32_t lo, int32_t hi) {
    if (slider == nullptr) return;
    if (lo > hi) std::swap(lo, hi);
    if (lo > lv_slider_get_value(slider)) {                                 // moving up: right knob first
        lv_slider_set_value(slider, hi, LV_ANIM_OFF);
        lv_slider_set_left_value(slider, lo, LV_ANIM_OFF);
    } else {                                                                // moving down: left knob first
        lv_slider_set_left_value(slider, lo, LV_ANIM_OFF);
        lv_slider_set_value(slider, hi, LV_ANIM_OFF);
    }
}

// ===========================================================================
// Options persistence (SONOMETER_PARAMS_FILE on the SD card), LVGL task only
// ===========================================================================
using esp_brookesia::apps::Settings;

static constexpr const char *PARAMS_DIR = BSP_SD_MOUNT_POINT "/apps";
static constexpr const char *PARAMS_PATH = BSP_SD_MOUNT_POINT "/" SONOMETER_PARAMS_FILE;
static constexpr const char *PARAMS_BAD_PATH = BSP_SD_MOUNT_POINT "/" SONOMETER_PARAMS_BAD_FILE;

struct sono_params_t {
    int32_t fft_bars;                                   // "bars": n_bars_selector option
    int32_t fft_db_min;                                 // "min_db": fft_sens_slider left knob
    int32_t fft_db_max;                                 // "max_db": fft_sens_slider right knob
    bool fft_peaks_hidden;                              // "hide_peaks": fft_options_peaks_disabled_switch
    int32_t fft_peaks;                                  // "peaks": fft_peaks_n_selector option
};
static sono_params_t s_params = {};                     // what the Options tab shows (kept between openings)
static bool s_params_valid = false;                     // s_params set by an opening
static bool s_params_dirty = false;                     // changed since the last successful save
static lv_timer_t *s_params_timer = nullptr;            // debounced save, paused when idle

static bool params_equal(const sono_params_t &a, const sono_params_t &b) {
    return a.fft_bars == b.fft_bars && a.fft_db_min == b.fft_db_min && a.fft_db_max == b.fft_db_max &&
           a.fft_peaks_hidden == b.fft_peaks_hidden && a.fft_peaks == b.fft_peaks;
}

/*
@brief Options tab -> parameters (the widgets are the reference: their initial values are the defaults)
*/
static sono_params_t params_from_widgets(void) {
    sono_params_t p = {};
    lv_obj_t *sens = Sonometer_objects.fft_sens_slider;
    lv_obj_t *peaks_switch = Sonometer_objects.fft_options_peaks_disabled_switch;
    p.fft_bars = (int32_t)fft_selected_bars();
    p.fft_db_min = (sens != nullptr) ? lv_slider_get_left_value(sens) : FFT_GRAPH_DB_MIN;
    p.fft_db_max = (sens != nullptr) ? lv_slider_get_value(sens) : FFT_GRAPH_DB_MAX;
    p.fft_peaks_hidden = (peaks_switch != nullptr) && lv_obj_has_state(peaks_switch, LV_STATE_CHECKED);
    p.fft_peaks = (int32_t)fft_dropdown_number(Sonometer_objects.fft_peaks_n_selector, FFT_PEAKS_DEFAULT);
    return p;
}

/*
@brief Selects the dropdown option whose number is nearest to value (options are numbers: "16\n32\n64...")
*/
static void dropdown_select_nearest(lv_obj_t *dropdown, int32_t value, const char *key) {
    if (dropdown == nullptr) return;
    const char *p = lv_dropdown_get_options(dropdown);
    uint32_t index = 0;
    uint32_t best_index = lv_dropdown_get_selected(dropdown);
    long best_number = 0;
    long best_diff = -1;
    while (p != nullptr && *p != '\0') {
        char *end = nullptr;
        const long number = strtol(p, &end, 10);
        if (end != p) {
            const long diff = labs(number - (long)value);
            if (best_diff < 0 || diff < best_diff) {
                best_diff = diff;
                best_index = index;
                best_number = number;
            }
        }
        p = strchr(p, '\n');
        if (p != nullptr) p++;
        index++;
    }
    if (best_diff > 0) ESP_LOGW(TAG, "%s: %d is not an option, %ld used", key, (int)value, best_number);
    lv_dropdown_set_selected(dropdown, best_index);
}

/*
@brief Parameters -> Options tab, before sound_graph_setup / fft_options_setup read it (no event is sent)
*/
static void params_to_widgets(const sono_params_t &p) {
    dropdown_select_nearest(Sonometer_objects.n_bars_selector, p.fft_bars, "fft.bars");
    range_slider_set(Sonometer_objects.fft_sens_slider, p.fft_db_min, p.fft_db_max);   // clamped to its range
    if (Sonometer_objects.fft_options_peaks_disabled_switch != nullptr) {
        lv_obj_set_state(Sonometer_objects.fft_options_peaks_disabled_switch, LV_STATE_CHECKED, p.fft_peaks_hidden);
    }
    dropdown_select_nearest(Sonometer_objects.fft_peaks_n_selector, p.fft_peaks, "fft.peaks");
}

static int json_take_int(cJSON *obj, const char *key, int32_t &out) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(v)) return 0;
    out = v->valueint;                                  // cJSON saturates it to the int range
    return 1;
}

static int json_take_bool(cJSON *obj, const char *key, bool &out) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsBool(v)) return 0;
    out = cJSON_IsTrue(v);
    return 1;
}

/*
@brief Reads PARAMS_PATH over p (keys it lacks keep p's values)
@return false without a readable JSON file; complete = every key present with the right type
*/
static bool params_load(sono_params_t &p, bool &complete) {
    complete = false;
    FILE *f = fopen(PARAMS_PATH, "r");
    if (f == nullptr) return false;
    char *buf = (char *)calloc(1, SONOMETER_PARAMS_FILE_MAX);
    if (buf == nullptr) {
        fclose(f);
        return false;
    }
    const size_t n = fread(buf, 1, SONOMETER_PARAMS_FILE_MAX - 1, f);
    fclose(f);
    cJSON *j = (n > 0) ? cJSON_Parse(buf) : nullptr;
    free(buf);
    if (j == nullptr) {
        remove(PARAMS_BAD_PATH);
        const bool kept = (rename(PARAMS_PATH, PARAMS_BAD_PATH) == 0);
        ESP_LOGE(TAG, "%s is not valid JSON%s, defaults used", PARAMS_PATH, kept ? " (kept as sonometer.bad.json)" : "");
        return false;
    }
    cJSON *fft = cJSON_GetObjectItem(j, "fft");
    int found = 0;
    found += json_take_int(fft, "bars", p.fft_bars);
    found += json_take_int(fft, "min_db", p.fft_db_min);
    found += json_take_int(fft, "max_db", p.fft_db_max);
    found += json_take_bool(fft, "hide_peaks", p.fft_peaks_hidden);
    found += json_take_int(fft, "peaks", p.fft_peaks);
    cJSON_Delete(j);
    complete = (found == 5);
    return true;
}

static bool params_save(const sono_params_t &p) {
    if (!Settings::isSdMounted()) return false;
    if (mkdir(PARAMS_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Options not saved: cannot create %s (errno %d)", PARAMS_DIR, errno);
        return false;
    }
    cJSON *j = cJSON_CreateObject();
    cJSON *fft = (j != nullptr) ? cJSON_AddObjectToObject(j, "fft") : nullptr;
    if (fft == nullptr) {
        cJSON_Delete(j);
        return false;
    }
    cJSON_AddNumberToObject(fft, "bars", p.fft_bars);
    cJSON_AddNumberToObject(fft, "min_db", p.fft_db_min);
    cJSON_AddNumberToObject(fft, "max_db", p.fft_db_max);
    cJSON_AddBoolToObject(fft, "hide_peaks", p.fft_peaks_hidden);
    cJSON_AddNumberToObject(fft, "peaks", p.fft_peaks);
    char *text = cJSON_Print(j);
    cJSON_Delete(j);
    if (text == nullptr) return false;

    bool ok = false;
    FILE *f = fopen(PARAMS_PATH, "w");
    if (f != nullptr) {
        ok = (fputs(text, f) >= 0);
        ok = (fclose(f) == 0) && ok;
    }
    cJSON_free(text);
    if (ok) ESP_LOGI(TAG, "Options saved to %s", PARAMS_PATH);
    else ESP_LOGW(TAG, "Options not saved: cannot write %s (card removed?)", PARAMS_PATH);
    return ok;
}

static void params_save_now(void) {
    if (s_params_dirty && params_save(s_params)) s_params_dirty = false;
}

static void params_timer_cb(lv_timer_t *timer) {
    lv_timer_pause(timer);
    params_save_now();
}

/*
@brief A widget of the Options tab changed: new parameters, saved SONOMETER_PARAMS_SAVE_DELAY_MS after the last change
*/
static void params_changed(void) {
    const sono_params_t p = params_from_widgets();
    if (s_params_valid && params_equal(p, s_params)) return;
    s_params = p;
    s_params_valid = true;
    s_params_dirty = true;
    if (s_params_timer == nullptr) {
        s_params_timer = lv_timer_create(params_timer_cb, SONOMETER_PARAMS_SAVE_DELAY_MS, nullptr);
        if (s_params_timer == nullptr) {
            params_save_now();
            return;
        }
    }
    lv_timer_reset(s_params_timer);
    lv_timer_resume(s_params_timer);
}

/*
@brief run(): file (or the last opening's values, or the UI defaults) -> Options tab, file rewritten when it differs
*/
static void params_setup(void) {
    Settings::ensureLoaded();                                               // the SD card is mounted by Settings
    sono_params_t p = s_params_valid ? s_params : params_from_widgets();
    bool complete = false;
    // a change that could not be saved (card pulled out) is newer than the file: keep it, save it again
    const bool loaded = !s_params_dirty && Settings::isSdMounted() && params_load(p, complete);
    if (!Settings::isSdMounted()) ESP_LOGW(TAG, "No SD card: options kept until the watch restarts");

    params_to_widgets(p);
    s_params = params_from_widgets();                                       // nearest dropdown options, clamped sliders
    s_params_valid = true;
    if (!loaded || !complete || !params_equal(s_params, p)) s_params_dirty = true;
    params_save_now();
    ESP_LOGI(TAG, "Options: %d bars, %d..%d dB, peaks %s (%d)", (int)s_params.fft_bars, (int)s_params.fft_db_min,
             (int)s_params.fft_db_max, s_params.fft_peaks_hidden ? "hidden" : "shown", (int)s_params.fft_peaks);
}

static void params_close(void) {
    if (s_params_timer != nullptr) {
        lv_timer_delete(s_params_timer);
        s_params_timer = nullptr;
    }
    params_save_now();                                                      // a change still in its save delay
}

/*
@brief Plot area of fft_graph (inside its padding + border), where the spectrum image is drawn
*/
static void fft_plot_area(lv_obj_t *chart, lv_area_t *area) {
    lv_obj_get_content_coords(chart, area);
    area->x2 = area->x1 + s_fft_img_w - 1;
    area->y2 = area->y1 + s_fft_img_h - 1;
}

/*
@brief Redraws image pixel columns [x0, x1) of fft_graph, whole plot height
*/
static void fft_invalidate_columns(int32_t x0, int32_t x1) {
    lv_obj_t *chart = Sonometer_objects.fft_graph;
    if (chart == nullptr || x1 <= x0) return;
    lv_area_t plot;
    fft_plot_area(chart, &plot);
    lv_area_t a;
    a.x1 = plot.x1 + x0;
    a.x2 = plot.x1 + x1 - 1;
    a.y1 = plot.y1;
    a.y2 = plot.y2;
    lv_obj_invalidate_area(chart, &a);
}

/*
@brief Peak bars back to the bar colour (peaks hidden, bar layout or cell count changed)
*/
static void fft_peaks_unmark(void) {
    for (uint32_t i = 0; i < s_fft_mark_n; i++) fft_invalidate_columns(s_fft_marks[i].x0, s_fft_marks[i].x1);
    s_fft_mark_n = 0;
}

static void fft_image_free(void) {
    heap_caps_free(s_fft_img_buf);
    heap_caps_free(s_fft_view);
    heap_caps_free(s_fft_top);
    s_fft_img_buf = nullptr;
    s_fft_view = nullptr;
    s_fft_top = nullptr;
    s_fft_img_w = 0;
    s_fft_img_h = 0;
}

/*
@brief Spectrum image the size of the plot area: RGB565 plane = bar colour, A8 plane = bars (cleared)
@note RGB565A8 variable images are blended straight from this buffer (no decoder copy, no cache), A8 alone would be copied
@note Kept between runs, reallocated only when the plot size changes
*/
static bool fft_image_setup(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) {
        fft_image_free();
        return false;
    }
    const size_t px = (size_t)w * (size_t)h;
    if (s_fft_img_buf == nullptr || w != s_fft_img_w || h != s_fft_img_h) {
        fft_image_free();
        s_fft_img_buf = (uint8_t *)heap_caps_aligned_alloc(16, px * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_fft_view = (float *)heap_caps_malloc((size_t)w * sizeof(float), MALLOC_CAP_8BIT);
        s_fft_top = (int32_t *)heap_caps_malloc((size_t)w * sizeof(int32_t), MALLOC_CAP_8BIT);
        if (s_fft_img_buf == nullptr || s_fft_view == nullptr || s_fft_top == nullptr) {
            fft_image_free();
            return false;
        }
        s_fft_img_w = w;
        s_fft_img_h = h;
    }

    uint16_t *rgb = (uint16_t *)s_fft_img_buf;
    const uint16_t color = lv_color_to_u16(lv_color_hex(FFT_GRAPH_COLOR_HEX));
    for (size_t i = 0; i < px; i++) rgb[i] = color;
    memset(s_fft_img_buf + px * 2, 0, px);
    for (int32_t x = 0; x < w; x++) s_fft_top[x] = h;                     // mask empty: no column lit

    memset(&s_fft_img, 0, sizeof(s_fft_img));
    s_fft_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_fft_img.header.cf = LV_COLOR_FORMAT_RGB565A8;
    s_fft_img.header.w = (uint32_t)w;
    s_fft_img.header.h = (uint32_t)h;
    s_fft_img.header.stride = (uint32_t)w * 2;                              // RGB565 stride, the A8 plane uses half of it
    s_fft_img.data_size = (uint32_t)(px * 3);
    s_fft_img.data = s_fft_img_buf;
    return true;
}

/*
@brief Renders the shown spectrum into the image's A8 plane: column c spans [c * w / cols, (c + 1) * w / cols)
@note Bar height: level within [s_fft_db_min, s_fft_db_max] (sensitivity sliders), clipped to the plot
@note Bars at least FFT_GRAPH_GAP_FROM_PX wide keep their last pixel column empty, as a gap
@note Incremental: s_fft_top is the first lit row of each pixel column as drawn now, only the rows between a column's old
      and new top are written. Rewriting the whole plane cost about 96 KB of PSRAM writes per spectrum (384 x 252 plot),
      on the LVGL core, and evicted the shared data cache the renderer reads fonts and images through
@param dirty when not null, receives the changed rectangle, in image coordinates
@return true if any pixel changed
*/
static bool fft_render(lv_area_t *dirty) {
    const int32_t w = s_fft_img_w;
    const int32_t h = s_fft_img_h;
    const int32_t cols = (int32_t)s_fft_view_cols;
    if (s_fft_img_buf == nullptr || s_fft_top == nullptr || cols <= 0 || cols > w) return false;

    const float db_lo = (float)s_fft_db_min;
    const float db_span = std::max((float)(s_fft_db_max - s_fft_db_min), 1.0f);   // min >= max: 1 dB range
    const bool gaps = w / cols >= FFT_GRAPH_GAP_FROM_PX;
    uint8_t *mask = s_fft_img_buf + (size_t)w * (size_t)h * 2;
    int32_t dx1 = w, dx2 = -1, dy1 = h, dy2 = -1;
    for (int32_t c = 0; c < cols; c++) {
        const int32_t x0 = c * w / cols;
        const int32_t x1 = (c + 1) * w / cols;
        const float level = std::clamp((s_fft_view[c] - db_lo) / db_span, 0.0f, 1.0f);
        const int32_t bar_top = h - (int32_t)lroundf(level * (float)h);
        for (int32_t x = x0; x < x1; x++) {
            const int32_t top = (gaps && x == x1 - 1) ? h : bar_top;
            const int32_t old = s_fft_top[x];
            if (top == old) continue;
            const int32_t y0 = std::min(top, old);
            const int32_t y1 = std::max(top, old);
            const uint8_t v = (top < old) ? 0xFF : 0x00;                    // bar grew: light [top, old), shrank: clear [old, top)
            uint8_t *p = mask + (size_t)y0 * (size_t)w + (size_t)x;
            for (int32_t y = y0; y < y1; y++, p += w) *p = v;
            s_fft_top[x] = top;
            dx1 = std::min(dx1, x);
            dx2 = std::max(dx2, x);
            dy1 = std::min(dy1, y0);
            dy2 = std::max(dy2, y1 - 1);
        }
    }
    if (dx2 < 0) return false;
    if (dirty != nullptr) {
        dirty->x1 = dx1;
        dirty->y1 = dy1;
        dirty->x2 = dx2;
        dirty->y2 = dy2;
    }
    return true;
}

/*
@brief Draws the fft_frequency_scale sections as bands, then the spectrum image, over fft_graph's background (LV_EVENT_DRAW_MAIN_END)
@note One image draw task per refresh stripe, where a bar series costs one rounded-rect draw task per bar per stripe
*/
static void fft_draw_cb(lv_event_t *e) {
    lv_obj_t *chart = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t plot;
    lv_obj_get_content_coords(chart, &plot);
    const int32_t w = lv_area_get_width(&plot);
    const int32_t span = s_fft_units_hi - s_fft_units_lo;
    if (s_fft_band_count > 0 && w > 0 && span > 0) {
        lv_draw_rect_dsc_t band;
        lv_draw_rect_dsc_init(&band);
        for (uint32_t i = 0; i < s_fft_band_count; i++) {
            const fft_band_t &b = s_fft_bands[i];
            const int32_t u0 = std::clamp(b.u_lo, s_fft_units_lo, s_fft_units_hi) - s_fft_units_lo;
            const int32_t u1 = std::clamp(b.u_hi, s_fft_units_lo, s_fft_units_hi) - s_fft_units_lo;
            lv_area_t a;
            a.x1 = plot.x1 + (int32_t)((int64_t)u0 * w / span);
            a.x2 = plot.x1 + (int32_t)((int64_t)u1 * w / span) - 1;
            a.y1 = plot.y1;
            a.y2 = plot.y2;
            if (a.x2 < a.x1) continue;
            band.bg_color = b.color;
            band.bg_opa = b.opa;
            lv_draw_rect(layer, &band, &a);
        }
    }

    if (s_fft_img_buf == nullptr || s_fft_view_cols == 0) return;
    lv_area_t area;
    fft_plot_area(chart, &area);
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &s_fft_img;
    lv_draw_image(layer, &dsc, &area);

    // peak bars in their list colour: one opaque rectangle over each, from its current top (s_fft_top, the same state
    // the image mask was rendered from) to the bottom. At most FFT_PEAKS_MAX small draw tasks, no image rewrite
    if (s_fft_mark_n == 0 || s_fft_top == nullptr) return;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = 0;
    rect.bg_opa = LV_OPA_COVER;
    for (uint32_t k = s_fft_mark_n; k-- > 0;) {                             // strongest last: on top if two share a column
        const fft_peak_mark_t &m = s_fft_marks[k];
        const int32_t top = s_fft_top[m.x0];
        if (top >= s_fft_img_h) continue;                                   // bar empty
        lv_area_t a;
        a.x1 = area.x1 + m.x0;
        a.x2 = area.x1 + m.x1 - 1;
        a.y1 = area.y1 + top;
        a.y2 = area.y1 + s_fft_img_h - 1;
        rect.bg_color = s_fft_peak_colors[m.rank];
        lv_draw_rect(layer, &rect, &a);
    }
}

/*
@brief Sets the bar count and clears the graph, fft_task publishes the next spectrum for that count
*/
static void fft_apply_bars(uint32_t bars) {
    bars = std::clamp<uint32_t>(bars, 1, FFT_BINS);
    fft_peaks_unmark();                                                     // marks were columns of the old layout
    s_fft_bars = bars;
    s_fft_view_cols = fft_columns(bars, s_fft_plot_w);
    if (s_fft_view != nullptr) {
        for (uint32_t c = 0; c < s_fft_view_cols; c++) s_fft_view[c] = FFT_DB_EMPTY;
        fft_render(nullptr);
    }
    lv_obj_t *chart = Sonometer_objects.fft_graph;
    if (chart != nullptr) lv_obj_invalidate(chart);
}

static void fft_bars_changed_cb(lv_event_t *e) {
    (void)e;
    fft_apply_bars(fft_selected_bars());
    params_changed();
}

/*
@brief Applies s_fft_db_min / s_fft_db_max to fft_graph: chart Y range (no series, kept consistent) and the bars
*/
static void fft_apply_range(void) {
    lv_obj_t *chart = Sonometer_objects.fft_graph;
    if (chart == nullptr) return;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, s_fft_db_min, s_fft_db_max);
    fft_render(nullptr);
    lv_obj_invalidate(chart);
}

/*
@brief fft_sens_slider (range mode): left knob = bottom of fft_graph, right knob = top; label "16dB to 54dB"
       (EEZ variable fft_sensitivity_value, shown in fft_sens_value_label)
@return false if the object is not fft_sens_slider
*/
static bool fft_options_slider_apply(lv_obj_t *slider) {
    if (slider == nullptr || slider != Sonometer_objects.fft_sens_slider) return false;
    s_fft_db_min = lv_slider_get_left_value(slider);
    s_fft_db_max = lv_slider_get_value(slider);
    char text[32];
    snprintf(text, sizeof(text), "%ddB to %ddB", (int)s_fft_db_min, (int)s_fft_db_max);
    set_var_fft_sensitivity_value(text);
    return true;
}

/*
@brief fft_peaks_list grid tracks: FFT_PEAKS_GRID_COLS equal columns, one content-high row per started line of cells
@note The EEZ export gives the list LV_LAYOUT_GRID with the track *sizes* {4} (rows) and {3} (columns), i.e. one 4 px row
      and one 3 px column in LVGL: the tracks are replaced here (3 columns x 4 rows for 10 peaks)
@note The arrays stay referenced by the style: static
*/
static int32_t s_peaks_col_dsc[FFT_PEAKS_GRID_COLS + 1];
static int32_t s_peaks_row_dsc[FFT_PEAKS_MAX + 1];

static void fft_peaks_grid_setup(lv_obj_t *list, uint32_t cells) {
    for (uint32_t c = 0; c < FFT_PEAKS_GRID_COLS; c++) s_peaks_col_dsc[c] = LV_GRID_FR(1);
    s_peaks_col_dsc[FFT_PEAKS_GRID_COLS] = LV_GRID_TEMPLATE_LAST;
    const uint32_t rows = std::max<uint32_t>((cells + FFT_PEAKS_GRID_COLS - 1) / FFT_PEAKS_GRID_COLS, 1);
    for (uint32_t r = 0; r < rows; r++) s_peaks_row_dsc[r] = LV_GRID_CONTENT;
    s_peaks_row_dsc[rows] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_grid_column_dsc_array(list, s_peaks_col_dsc, LV_PART_MAIN);   // = the export's MAIN | DEFAULT
    lv_obj_set_style_grid_row_dsc_array(list, s_peaks_row_dsc, LV_PART_MAIN);
    lv_obj_set_layout(list, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_hor(list, FFT_PEAKS_LIST_PAD_HOR, LV_PART_MAIN);  // wider cells: 4-digit peaks fit
}

/*
@brief Colour of cell i of n: hue from FFT_PEAKS_HUE_FIRST (red) to FFT_PEAKS_HUE_LAST (blue), one cell = one step
*/
static lv_color_t fft_peak_color(uint32_t i, uint32_t n) {
    const int32_t span = FFT_PEAKS_HUE_LAST - FFT_PEAKS_HUE_FIRST;
    const int32_t hue = FFT_PEAKS_HUE_FIRST + ((n > 1) ? span * (int32_t)i / (int32_t)(n - 1) : 0);
    return lv_color_hsv_to_rgb((uint16_t)hue, FFT_PEAKS_COLOR_SAT, FFT_PEAKS_COLOR_VAL);
}

/*
@brief Rebuilds fft_peaks_list with one cell per peak, count from fft_peaks_n_selector, filled left to right then top
       to bottom (strongest first), each cell in its colour (fft_peak_color)
@note Cells are list buttons made non-clickable (display only, the list still scrolls), "-" until a peak fills them.
      Padding reduced (FFT_PEAKS_CELL_PAD_*) so 4 rows fit the list. Labels clip instead of scrolling: a scrolling
      label restarts on every text change, so a value updated 4 times a second could not be read
*/
static void fft_peaks_build_rows(void) {
    lv_obj_t *list = Sonometer_objects.fft_peaks_list;
    fft_peaks_unmark();                                                     // colours depend on the cell count
    s_fft_peak_rows = 0;
    if (list == nullptr) return;
    lv_obj_clean(list);
    const uint32_t n = std::min<uint32_t>(fft_dropdown_number(Sonometer_objects.fft_peaks_n_selector, FFT_PEAKS_DEFAULT),
                                          FFT_PEAKS_MAX);
    fft_peaks_grid_setup(list, n);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *cell = lv_list_add_button(list, nullptr, "-");
        lv_obj_t *label = (cell != nullptr) ? lv_obj_get_child(cell, 0) : nullptr;
        if (label == nullptr) break;
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_ver(cell, FFT_PEAKS_CELL_PAD_VER, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(cell, FFT_PEAKS_CELL_PAD_HOR, LV_PART_MAIN);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, (int32_t)(i % FFT_PEAKS_GRID_COLS), 1,
                             LV_GRID_ALIGN_STRETCH, (int32_t)(i / FFT_PEAKS_GRID_COLS), 1);
        s_fft_peak_colors[i] = fft_peak_color(i, n);
        lv_obj_set_style_text_color(label, s_fft_peak_colors[i], LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        s_fft_peak_labels[s_fft_peak_rows++] = label;
    }
    s_fft_peaks_tick = lv_tick_get() - FFT_PEAKS_UPDATE_PERIOD_MS;          // filled by the next spectrum
}

/*
@brief Writes the peaks into the fft_peaks_list cells, "%0.0fHz (%0.1fdB)", strongest first; cells without a peak show "-"
*/
static void fft_peaks_show(const fft_peak_t *peaks, uint32_t n) {
    char text[48];
    for (uint32_t i = 0; i < s_fft_peak_rows; i++) {
        if (i < n) snprintf(text, sizeof(text), "%0.0fHz (%0.1fdB)", peaks[i].hz, peaks[i].db);
        else snprintf(text, sizeof(text), "-");
        lv_obj_t *label = s_fft_peak_labels[i];
        if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
    }
}

/*
@brief Image pixel columns [x0, x1) of the bar showing a peak, false when it is outside the plot
@note The bar holding the peak's bin centre (fft_band_power averages the bins centred in a bar), then the column that
      bar is drawn in (fft_columns: several bars per pixel when they outnumber the pixels), as fft_render lays it out
*/
static bool fft_peak_columns(float hz, fft_peak_mark_t &m) {
    const int32_t w = s_fft_img_w;
    const uint32_t bars = s_fft_bars;
    const uint32_t cols = s_fft_view_cols;
    const float f_lo = s_fft_f_lo;
    const float f_hi = s_fft_f_hi;
    if (w <= 0 || bars == 0 || cols == 0 || cols > (uint32_t)w || !(f_lo > 0.0f) || !(f_hi > f_lo)) return false;
    const float bin_hz = (float)I2S_MIC_SAMPLE_RATE / (float)FFT_SIZE;
    const float centre = roundf(hz / bin_hz) * bin_hz;
    if (centre < f_lo || centre >= f_hi) return false;
    const uint32_t bar = std::min<uint32_t>((uint32_t)((float)bars * log2f(centre / f_lo) / log2f(f_hi / f_lo)), bars - 1);
    const int32_t c = (int32_t)((cols == bars) ? bar : bar * cols / bars);
    m.x0 = c * w / (int32_t)cols;
    m.x1 = (c + 1) * w / (int32_t)cols;
    if (w / (int32_t)cols >= FFT_GRAPH_GAP_FROM_PX) m.x1--;                 // same gap column as fft_render
    return m.x1 > m.x0;
}

/*
@brief Bars of the listed peaks get their cell's colour (drawn by fft_draw_cb); redraws the columns that changed
*/
static void fft_peaks_mark(const fft_peak_t *peaks, uint32_t n) {
    fft_peak_mark_t marks[FFT_PEAKS_MAX];
    uint32_t count = 0;
    for (uint32_t i = 0; i < n && i < s_fft_peak_rows; i++) {
        fft_peak_mark_t m;
        if (!fft_peak_columns(peaks[i].hz, m)) continue;
        m.rank = i;
        marks[count++] = m;
    }
    bool same = (count == s_fft_mark_n);
    for (uint32_t i = 0; same && i < count; i++) {
        same = marks[i].x0 == s_fft_marks[i].x0 && marks[i].x1 == s_fft_marks[i].x1 && marks[i].rank == s_fft_marks[i].rank;
    }
    if (same) return;
    fft_peaks_unmark();                                                     // old colours away
    memcpy(s_fft_marks, marks, count * sizeof(fft_peak_mark_t));
    s_fft_mark_n = count;
    for (uint32_t i = 0; i < count; i++) fft_invalidate_columns(marks[i].x0, marks[i].x1);
}

/*
@brief fft_options_peaks_disabled_switch state: hides fft_peaks_list, disables fft_peaks_n_selector,
       sets fft_peaks_list_disabled, and stops the peak search
*/
static void fft_peaks_apply_disabled(bool disabled) {
    set_var_fft_peaks_list_disabled(disabled);
    s_fft_peaks_on = !disabled;
    if (disabled) fft_peaks_unmark();                                       // all bars white again
    lv_obj_t *list = Sonometer_objects.fft_peaks_list;
    if (list != nullptr) {
        if (disabled) lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(list, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *selector = Sonometer_objects.fft_peaks_n_selector;
    if (selector != nullptr) lv_obj_set_state(selector, LV_STATE_DISABLED, disabled);
}

static void fft_peaks_switch_cb(lv_event_t *e) {
    fft_peaks_apply_disabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    params_changed();
}

static void fft_peaks_n_changed_cb(lv_event_t *e) {
    (void)e;
    fft_peaks_build_rows();
    params_changed();
}

/*
@brief Renders the latest spectrum for Sonometer_objects.fft_graph, and enables the FFT path only while its tab is shown
@note LVGL timer: runs in the LVGL task, so the widget calls need no lock
*/
static void fft_timer_cb(lv_timer_t *timer) {
    (void)timer;
    lv_obj_t *chart = Sonometer_objects.fft_graph;
    if (chart == nullptr || s_fft_view == nullptr || fft_mux == nullptr) return;

    lv_obj_t *tabs = Sonometer_objects.sonometer_tabs;
    lv_obj_t *tab = Sonometer_objects.fft_tab;
    s_fft_active = (tabs == nullptr || tab == nullptr) || (int32_t)lv_tabview_get_tab_active(tabs) == lv_obj_get_index(tab);
    if (!s_fft_active) return;

    fft_peak_t peaks[FFT_PEAKS_MAX];
    uint32_t n_peaks = 0;
    if (xSemaphoreTake(fft_mux, 0) != pdTRUE) return;                      // fft_task publishing: next tick
    const bool fresh = s_fft_out_seq != s_fft_shown_seq && s_fft_out_bars == s_fft_bars && s_fft_out_cols == s_fft_view_cols;
    if (fresh) {
        memcpy(s_fft_view, s_fft_out, s_fft_view_cols * sizeof(float));
        s_fft_shown_seq = s_fft_out_seq;
        n_peaks = std::min<uint32_t>(s_fft_peaks_out_n, FFT_PEAKS_MAX);
        memcpy(peaks, s_fft_peaks_out, n_peaks * sizeof(fft_peak_t));
    }
    xSemaphoreGive(fft_mux);
    if (!fresh) return;

    // redraw only the rectangle the bars changed in, not the whole plot
    lv_area_t dirty;
    if (fft_render(&dirty)) {
        lv_area_t plot;
        fft_plot_area(chart, &plot);
        lv_area_move(&dirty, plot.x1, plot.y1);                            // image -> screen coordinates
        lv_obj_invalidate_area(chart, &dirty);
    }

    // peak list: slower than the bars, so the values stay readable
    if (s_fft_peaks_on && s_fft_peak_rows > 0 && lv_tick_elaps(s_fft_peaks_tick) >= FFT_PEAKS_UPDATE_PERIOD_MS) {
        s_fft_peaks_tick = lv_tick_get();
        fft_peaks_show(peaks, n_peaks);
        fft_peaks_mark(peaks, n_peaks);                                     // bars in the cells' colours, same values
    }
}

/*
@brief Options tab: sensitivity sliders (fft_graph dB range), peaks switch and peak count, applied now and on every change
@note The sliders call action_fft_options_slider_changed (EEZ action), the switch and the peak count selector get callbacks here
@note Without a slider, its range end stays FFT_GRAPH_DB_MIN / FFT_GRAPH_DB_MAX
*/
static void fft_options_setup(void) {
    s_fft_db_min = FFT_GRAPH_DB_MIN;
    s_fft_db_max = FFT_GRAPH_DB_MAX;
    fft_options_slider_apply(Sonometer_objects.fft_sens_slider);
    fft_apply_range();

    lv_obj_t *peaks_switch = Sonometer_objects.fft_options_peaks_disabled_switch;
    if (peaks_switch != nullptr) lv_obj_add_event_cb(peaks_switch, fft_peaks_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *peaks_selector = Sonometer_objects.fft_peaks_n_selector;
    if (peaks_selector != nullptr) lv_obj_add_event_cb(peaks_selector, fft_peaks_n_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    fft_peaks_build_rows();
    fft_peaks_apply_disabled(peaks_switch != nullptr && lv_obj_has_state(peaks_switch, LV_STATE_CHECKED));
}

/*
@brief Chart settings of the standalone sonometer, in LVGL 9.2 form (was hand-written in screens.c)
@note With a scale, the chart follows it: same dB range, same plotting height, horizontal grid lines on the major ticks
@note The chart plots inside its padding + border (default theme: 10 + 2 px) while a scale spreads its ticks over its
      own height minus its padding + border (0 by default): the scale gets the chart's inset, height and position
@note FFT Graph block: fft_graph keeps only its background, its bars are one image (fft_draw_cb), on the log frequency
      axis drawn by fft_frequency_scale
*/
static void sound_graph_setup(lv_obj_t *chart, lv_obj_t *scale) {
    int32_t y_min = SOUND_GRAPH_Y_MIN;
    int32_t y_max = SOUND_GRAPH_Y_MAX;
    int32_t div_h = SOUND_GRAPH_DIV_H;

    if (chart != nullptr && scale != nullptr) {
        // the scale (set in the UI) is the reference for the dB range
        y_min = lv_scale_get_range_min_value(scale);
        y_max = lv_scale_get_range_max_value(scale);

        // one horizontal grid line per major tick, when the major ticks split the range evenly
        const int32_t ticks = lv_scale_get_total_tick_count(scale);
        const int32_t major_every = lv_scale_get_major_tick_every(scale);
        if (ticks > 1 && major_every > 0 && (ticks - 1) % major_every == 0) div_h = (ticks - 1) / major_every + 1;

        // same vertical inset as the chart's plot area, same height, right edge against the chart
        lv_obj_update_layout(chart);
        const int32_t chart_border = lv_obj_get_style_border_width(chart, LV_PART_MAIN);
        const int32_t scale_border = lv_obj_get_style_border_width(scale, LV_PART_MAIN);
        const int32_t inset_top = lv_obj_get_style_pad_top(chart, LV_PART_MAIN) + chart_border - scale_border;
        const int32_t inset_bottom = lv_obj_get_style_pad_bottom(chart, LV_PART_MAIN) + chart_border - scale_border;
        lv_obj_set_style_pad_top(scale, std::max<int32_t>(inset_top, 0), LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(scale, std::max<int32_t>(inset_bottom, 0), LV_PART_MAIN);
        lv_obj_set_style_pad_top(scale, 0, LV_PART_ITEMS);                  // ticks start exactly at the inset
        lv_obj_set_style_pad_bottom(scale, 0, LV_PART_ITEMS);
        lv_obj_set_height(scale, lv_obj_get_height(chart));
        lv_obj_align_to(scale, chart, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    }
    if (chart != nullptr) {
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, -SOUND_GRAPH_POINTS, 0);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);                 // no point markers
        lv_chart_set_point_count(chart, SOUND_GRAPH_POINTS);
        lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);           // new points enter on the right
        lv_chart_set_div_line_count(chart, (uint8_t)div_h, SOUND_GRAPH_DIV_V);
    }
    {   // FFT Graph
        lv_obj_t *fft = Sonometer_objects.fft_graph;
        lv_obj_t *fft_scale = Sonometer_objects.fft_frequency_scale;
        if (fft != nullptr) {
            // the scale's range (Hz, set in the UI) is the frequency span, the axis is made logarithmic below
            float f_lo = FFT_GRAPH_F_MIN;
            float f_hi = FFT_GRAPH_F_MAX;
            if (fft_scale != nullptr) {
                const int32_t lo = lv_scale_get_range_min_value(fft_scale);
                const int32_t hi = lv_scale_get_range_max_value(fft_scale);
                if (lo > 0 && hi > lo) {
                    f_lo = (float)lo;
                    f_hi = (float)hi;
                }
            }
            s_fft_f_lo = f_lo;
            s_fft_f_hi = f_hi;
            s_fft_band_count = 0;                                           // set again by fft_scale_set_log

            // no series: the chart only draws its background, the bars are one image drawn on top (fft_draw_cb)
            lv_chart_set_div_line_count(fft, 0, 0);                         // no horizontal / vertical dividers
            lv_obj_update_layout(fft);
            const int32_t plot_w = lv_obj_get_content_width(fft);
            const int32_t plot_h = lv_obj_get_content_height(fft);
            const bool image_ready = fft_image_setup(plot_w, plot_h);
            if (!image_ready) ESP_LOGE(TAG, "FFT graph image unavailable (memory), the FFT tab stays empty");
            s_fft_plot_w = image_ready ? (uint32_t)plot_w : 0;
            lv_obj_add_event_cb(fft, fft_draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);

            if (fft_scale != nullptr) {
                fft_scale_set_log(fft_scale, f_lo, f_hi);

                // ticks span the bars: same x and width as the chart, inset by the chart's padding + border
                // (LVGL 9.2: a HORIZONTAL_BOTTOM scale offsets its first tick by pad_right and its last one by pad_left)
                const int32_t chart_border = lv_obj_get_style_border_width(fft, LV_PART_MAIN);
                const int32_t scale_border = lv_obj_get_style_border_width(fft_scale, LV_PART_MAIN);
                const int32_t inset_left = lv_obj_get_style_pad_left(fft, LV_PART_MAIN) + chart_border - scale_border;
                const int32_t inset_right = lv_obj_get_style_pad_right(fft, LV_PART_MAIN) + chart_border - scale_border;
                lv_obj_set_style_pad_right(fft_scale, std::max<int32_t>(inset_left, 0), LV_PART_MAIN);
                lv_obj_set_style_pad_left(fft_scale, std::max<int32_t>(inset_right, 0), LV_PART_MAIN);
                lv_obj_set_style_pad_left(fft_scale, 0, LV_PART_ITEMS);
                lv_obj_set_style_pad_right(fft_scale, 0, LV_PART_ITEMS);
                lv_obj_set_x(fft_scale, lv_obj_get_x(fft));
                lv_obj_set_width(fft_scale, lv_obj_get_width(fft));
            }

            // bar count from n_bars_selector, now and on every change
            if (Sonometer_objects.n_bars_selector != nullptr) {
                lv_obj_add_event_cb(Sonometer_objects.n_bars_selector, fft_bars_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
            }
            fft_apply_bars(fft_selected_bars());
        }
    }
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

// Options tab variables: LVGL task only, no lock
static void copy_text(char *dst, size_t size, const char *src) {
    const size_t n = (src != nullptr) ? strnlen(src, size - 1) : 0;
    if (n > 0) memcpy(dst, src, n);
    dst[n] = '\0';
}

const char *get_var_fft_sensitivity_value() {
    return s_fft_sens_text;
}
void set_var_fft_sensitivity_value(const char *value) {
    copy_text(s_fft_sens_text, sizeof(s_fft_sens_text), value);
}

bool get_var_fft_peaks_list_disabled() {
    return s_fft_peaks_disabled;
}
void set_var_fft_peaks_list_disabled(bool value) {
    s_fft_peaks_disabled = value;
}

/*
@brief EEZ action of fft_sens_slider (LV_EVENT_VALUE_CHANGED, either knob): value label + fft_graph range
*/
void action_fft_options_slider_changed(lv_event_t *e) {
    if (fft_options_slider_apply(lv_event_get_target_obj(e))) {
        fft_apply_range();
        params_changed();                                                   // saved once the drag stops
    }
}

} // extern "C"

namespace {

/*
@brief Appends the raw mic samples of one I2S read to the FFT ring, only while the FFT tab is shown
@note Called by sound_task: short timeout, the audio loop must never stall on the FFT
*/
static void fft_push(const int32_t *frames, size_t count) {
    if (!s_fft_active || s_fft_ring == nullptr || fft_mux == nullptr) return;
    if (xSemaphoreTake(fft_mux, pdMS_TO_TICKS(2)) != pdTRUE) return;
    uint32_t pos = s_fft_ring_pos;
    for (size_t i = 0; i < count; i++) {
        s_fft_ring[pos] = (float)frames[i * I2S_MIC_CHANNELS + SOUND_MIC_CHANNEL] * (1.0f / 2147483648.0f);
        pos = (pos + 1) & (FFT_SIZE - 1);
    }
    s_fft_ring_pos = pos;
    xSemaphoreGive(fft_mux);
}

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
            fft_push(s_rx_buf, count);                                      // raw samples for the FFT tab
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
@brief Bar edges in bins: bar i covers [edges[i], edges[i + 1]), equal frequency ratios from f_lo to f_hi
*/
static void fft_compute_edges(uint32_t bars, float f_lo, float f_hi) {
    const float lo_bins = f_lo * (float)FFT_SIZE / (float)I2S_MIC_SAMPLE_RATE;
    const float octaves = log2f(f_hi / f_lo);
    for (uint32_t i = 0; i <= bars; i++) {
        s_fft_edges[i] = lo_bins * exp2f(octaves * (float)i / (float)bars);
    }
}

/*
@brief Mean power of the bins centred in [lo, hi), or the power interpolated at the band's log-centre when it holds no bin centre
*/
static float fft_band_power(const float *power, float lo, float hi) {
    if (lo >= (float)(FFT_BINS - 1)) return 0.0f;                           // above Nyquist
    const uint32_t b0 = (uint32_t)ceilf(lo);
    const uint32_t b1 = std::min<uint32_t>((uint32_t)ceilf(hi), FFT_BINS);
    if (b1 > b0) {
        float sum = 0.0f;
        for (uint32_t b = b0; b < b1; b++) sum += power[b];
        return sum / (float)(b1 - b0);
    }
    const float centre = sqrtf(lo * hi);                                    // < hi <= b0 <= FFT_BINS - 1
    const uint32_t j = (uint32_t)centre;
    const float t = centre - (float)j;
    return power[j] * (1.0f - t) + power[j + 1] * t;
}

/*
@brief Strongest local maxima of the bin powers inside [f_lo, f_hi], strongest first, at most FFT_PEAKS_MAX
@note Frequency and level from a parabola through the peak bin and its 2 neighbours (in dB): about 1 Hz / 0.2 dB on a tone
@note Level of a pure tone at that frequency, in dB SPL (window's coherent gain): not the per-bin level of the bars
*/
static uint32_t fft_find_peaks(const float *power, float f_lo, float f_hi, float to_tone, fft_peak_t *out) {
    const float bin_hz = (float)I2S_MIC_SAMPLE_RATE / (float)FFT_SIZE;
    const uint32_t k0 = std::max<uint32_t>(2, (uint32_t)ceilf(f_lo / bin_hz));
    const uint32_t k1 = std::min<uint32_t>(FFT_BINS - 2, (uint32_t)floorf(f_hi / bin_hz));
    uint32_t idx[FFT_PEAKS_MAX];
    float pw[FFT_PEAKS_MAX];
    uint32_t n = 0;
    for (uint32_t k = k0; k <= k1; k++) {
        const float p = power[k];
        if (!(p > power[k - 1] && p >= power[k + 1])) continue;            // not a local maximum
        if (n == FFT_PEAKS_MAX && p <= pw[n - 1]) continue;                 // weaker than the kept ones
        uint32_t j = (n < FFT_PEAKS_MAX) ? n++ : FFT_PEAKS_MAX - 1;        // sorted insert, strongest first
        while (j > 0 && pw[j - 1] < p) {
            pw[j] = pw[j - 1];
            idx[j] = idx[j - 1];
            j--;
        }
        pw[j] = p;
        idx[j] = k;
    }

    const float tone_db = 94.0f + 10.0f * log10f(to_tone);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t k = idx[i];
        const float a = 10.0f * log10f(power[k - 1] + 1e-30f);
        const float b = 10.0f * log10f(power[k] + 1e-30f);
        const float c = 10.0f * log10f(power[k + 1] + 1e-30f);
        const float den = a - 2.0f * b + c;
        const float d = (den < 0.0f) ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        out[i].hz = ((float)k + d) * bin_hz;
        out[i].db = tone_db + b - 0.25f * (a - c) * d;
    }
    return n;
}

/*
@brief Spectrum of the last FFT_SIZE samples, every FFT_UPDATE_PERIOD_MS while the FFT tab is shown
@note DC removed, Hann window, esp-dsp radix-2 FFT; levels in dB SPL per bin (level calibration, no A-weighting)
@note A bar is the mean power of its bins, a column keeps the highest bar it covers (see fft_columns)
@note Peaks (fft_find_peaks) while fft_peaks_list is enabled
*/
void fft_task(void *args) {
    (void)args;
    float *buf = s_fft_buf;
    float *col = s_fft_buf + FFT_SIZE;                                      // second half: free once the bin powers are packed
    // |X[k]|^2 -> mean square of bin k / 94 dB SPL reference: one-sided spectrum, corrected for the window's power
    const float to_ref = 2.0f / ((float)FFT_SIZE * s_fft_win_power * sound_ref_ms());
    // |X[k]|^2 at a tone's frequency -> that tone's mean square / 94 dB SPL reference (window's coherent gain)
    const float to_tone = 2.0f / (s_fft_win_sum * s_fft_win_sum * sound_ref_ms());
    fft_peak_t peaks[FFT_PEAKS_MAX];
    uint32_t edges_bars = 0;
    float edges_lo = 0.0f;
    float edges_hi = 0.0f;
    TickType_t last_wake = xTaskGetTickCount();

    while (s_running) {
        if (xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FFT_UPDATE_PERIOD_MS)) == pdFALSE) {
            vTaskDelay(1);                                                  // running late: still yield to lower priorities
            last_wake = xTaskGetTickCount();
        }
        if (!s_fft_active) continue;

        // samples, oldest first, into the real parts
        if (xSemaphoreTake(fft_mux, pdMS_TO_TICKS(20)) != pdTRUE) continue;
        uint32_t pos = s_fft_ring_pos;
        for (uint32_t i = 0; i < FFT_SIZE; i++) {
            buf[2 * i] = s_fft_ring[pos];
            pos = (pos + 1) & (FFT_SIZE - 1);
        }
        xSemaphoreGive(fft_mux);

        float mean = 0.0f;
        for (uint32_t i = 0; i < FFT_SIZE; i++) mean += buf[2 * i];
        mean /= (float)FFT_SIZE;
        for (uint32_t i = 0; i < FFT_SIZE; i++) {
            buf[2 * i] = (buf[2 * i] - mean) * s_fft_win[i];               // DC offset removed, Hann window
            buf[2 * i + 1] = 0.0f;
        }
        dsps_fft2r_fc32(buf, FFT_SIZE);
        dsps_bit_rev_fc32(buf, FFT_SIZE);
        for (uint32_t k = 0; k < FFT_BINS; k++) {                           // in place: buf[k] = |X[k]|^2 (reads 2k, 2k+1 >= k)
            const float re = buf[2 * k];
            const float im = buf[2 * k + 1];
            buf[k] = re * re + im * im;
        }

        const uint32_t bars = s_fft_bars;
        const uint32_t cols = fft_columns(bars, s_fft_plot_w);
        const float f_lo = s_fft_f_lo;
        const float f_hi = s_fft_f_hi;
        const uint32_t n_peaks = s_fft_peaks_on ? fft_find_peaks(buf, f_lo, f_hi, to_tone, peaks) : 0;
        if (bars != edges_bars || f_lo != edges_lo || f_hi != edges_hi) {
            fft_compute_edges(bars, f_lo, f_hi);
            edges_bars = bars;
            edges_lo = f_lo;
            edges_hi = f_hi;
        }

        for (uint32_t c = 0; c < cols; c++) col[c] = 0.0f;
        for (uint32_t i = 0; i < bars; i++) {
            const float p = fft_band_power(buf, s_fft_edges[i], s_fft_edges[i + 1]);
            const uint32_t c = (cols == bars) ? i : i * cols / bars;
            if (p > col[c]) col[c] = p;
        }
        for (uint32_t c = 0; c < cols; c++) {
            col[c] = 94.0f + 10.0f * log10f(col[c] * to_ref + 1e-20f);      // dB SPL per bin
        }

        if (xSemaphoreTake(fft_mux, pdMS_TO_TICKS(20)) != pdTRUE) continue;
        memcpy(s_fft_out, col, cols * sizeof(float));
        memcpy(s_fft_peaks_out, peaks, n_peaks * sizeof(fft_peak_t));
        s_fft_peaks_out_n = n_peaks;
        s_fft_out_bars = bars;
        s_fft_out_cols = cols;
        s_fft_out_seq++;
        xSemaphoreGive(fft_mux);
    }

    s_fft_task = nullptr;
    vTaskDelete(NULL);
}

/*
@brief FFT buffers (about 20 bytes per FFT point, PSRAM) and the esp-dsp tables, once; false leaves the FFT tab empty
@note The esp-dsp radix-2 tables are global and shared with Recorder: dsps_fft2r_init_fc32 is a no-op once done, never deinit
*/
static bool fft_init(void) {
    if (s_fft_buf != nullptr) return true;
    if (dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) != ESP_OK) return false;

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    float *ring = (float *)heap_caps_aligned_alloc(16, FFT_SIZE * sizeof(float), caps);
    float *win = (float *)heap_caps_aligned_alloc(16, FFT_SIZE * sizeof(float), caps);
    float *buf = (float *)heap_caps_aligned_alloc(16, 2 * FFT_SIZE * sizeof(float), caps);
    float *edges = (float *)heap_caps_aligned_alloc(16, (FFT_BINS + 1) * sizeof(float), caps);
    float *out = (float *)heap_caps_aligned_alloc(16, FFT_BINS * sizeof(float), caps);
    if (ring == nullptr || win == nullptr || buf == nullptr || edges == nullptr || out == nullptr) {
        heap_caps_free(ring);
        heap_caps_free(win);
        heap_caps_free(buf);
        heap_caps_free(edges);
        heap_caps_free(out);
        return false;
    }

    memset(ring, 0, FFT_SIZE * sizeof(float));
    dsps_wind_hann_f32(win, FFT_SIZE);
    float win_power = 0.0f;
    float win_sum = 0.0f;
    for (uint32_t i = 0; i < FFT_SIZE; i++) {
        win_power += win[i] * win[i];
        win_sum += win[i];
    }

    s_fft_ring = ring;
    s_fft_win = win;
    s_fft_win_power = win_power;
    s_fft_win_sum = win_sum;
    s_fft_edges = edges;
    s_fft_out = out;
    s_fft_buf = buf;                                                        // last: marks the FFT as ready
    return true;
}

/*
@brief Waits for sound_task and fft_task to leave, bounded: called from the LVGL task
*/
static bool sound_wait_stopped(void) {
    for (int t = 0; (s_sound_task != nullptr || s_fft_task != nullptr) && t < SOUND_TASK_STOP_TIMEOUT_MS; t += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return s_sound_task == nullptr && s_fft_task == nullptr;
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
    if (fft_mux == nullptr) fft_mux = xSemaphoreCreateMutex();
    const bool fft_ready = fft_mux != nullptr && fft_init();
    if (!fft_ready) ESP_LOGE(TAG, "FFT unavailable (memory), the FFT tab stays empty");

    s_running = true;
    // the handle is written before the task can run, so a task that exits at once still clears it last
    if (xTaskCreatePinnedToCore(sound_task, "sound", SOUND_TASK_STACK_SIZE, nullptr,
                                SOUND_TASK_PRIORITY, (TaskHandle_t *)&s_sound_task, SOUND_TASK_CORE) != pdPASS) {
        s_sound_task = nullptr;
        s_running = false;
        ESP_LOGE(TAG, "Failed to start the sound task");
        return;
    }
    if (fft_ready && xTaskCreatePinnedToCore(fft_task, "fft", FFT_TASK_STACK_SIZE, nullptr,
                                             FFT_TASK_PRIORITY, (TaskHandle_t *)&s_fft_task, FFT_TASK_CORE) != pdPASS) {
        s_fft_task = nullptr;
        ESP_LOGE(TAG, "Failed to start the FFT task, the FFT tab stays empty");
    }
}

static void sound_stop(void) {
    s_running = false;
    if (!sound_wait_stopped()) ESP_LOGW(TAG, "Sound / FFT task still running after %d ms", SOUND_TASK_STOP_TIMEOUT_MS);
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
    params_setup();                                                         // apps/sonometer.json -> Options tab, read below
    sound_graph_setup(Sonometer_objects.sound_graph, Sonometer_objects.graph_scale);
    fft_options_setup();                                                    // after the FFT graph: sets its range
    graph_series = nullptr;                                                 // new chart: its series is added on the first point

    if (s_ui_timer == nullptr) s_ui_timer = lv_timer_create(ui_timer_cb, SOUND_UI_TICK_MS, nullptr);
    if (s_graph_timer == nullptr) s_graph_timer = lv_timer_create(graph_timer_cb, SOUND_GRAPH_PERIOD_MS, nullptr);
    if (s_fft_timer == nullptr) s_fft_timer = lv_timer_create(fft_timer_cb, FFT_UPDATE_PERIOD_MS / 2, nullptr);
    Settings::sleep_locked = true;
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
    if (s_fft_timer != nullptr) {
        lv_timer_delete(s_fft_timer);
        s_fft_timer = nullptr;
    }
    graph_series = nullptr;
    s_fft_active = false;
    s_fft_peak_rows = 0;                                                    // its labels go with the screen
    s_fft_mark_n = 0;                                                       // no invalidation: the chart goes too
    params_close();                                                         // pending option change -> SD card
    sound_stop();
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Sonometer, APP_NAME, []()
{
    return std::shared_ptr<Sonometer>(Sonometer::requestInstance(), [](Sonometer *p) {});
})

} // namespace esp_brookesia::apps
