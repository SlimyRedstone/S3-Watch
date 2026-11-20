#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *sonometer_tabs;
    lv_obj_t *amplitude_tab;
    lv_obj_t *main_container;
    lv_obj_t *sound_label;
    lv_obj_t *graph_container;
    lv_obj_t *graph_scale;
    lv_obj_t *sound_graph;
    lv_obj_t *fft_tab;
    lv_obj_t *fft_container;
    lv_obj_t *fft_frequency_scale;
    lv_obj_t *fft_graph;
    lv_obj_t *fft_peaks_list;
    lv_obj_t *fft_options;
    lv_obj_t *options_container;
    lv_obj_t *fft_bars_controls;
    lv_obj_t *n_bars_label;
    lv_obj_t *n_bars_selector;
    lv_obj_t *options_spearator_1;
    lv_obj_t *fft_range_controls;
    lv_obj_t *min_sensitivity_container;
    lv_obj_t *fft_labels_container;
    lv_obj_t *fft_sens_label;
    lv_obj_t *fft_sens_value_label;
    lv_obj_t *fft_sens_slider;
    lv_obj_t *options_spearator_3;
    lv_obj_t *fft_peaks_controls;
    lv_obj_t *fft_peak_switch_container;
    lv_obj_t *fft_peaks_options_label;
    lv_obj_t *fft_options_peaks_disabled_switch;
    lv_obj_t *fft_n_peaks_container;
    lv_obj_t *fft_n_peaks_options_label;
    lv_obj_t *fft_peaks_n_selector;
} objects_t;

extern objects_t Sonometer_objects;

typedef struct {
    lv_scale_section_t *low;
    lv_scale_section_t *mid;
    lv_scale_section_t *high;
    lv_scale_section_t *danger;
    lv_scale_section_t *bass;
    lv_scale_section_t *mids;
    lv_scale_section_t *highs;
} screen_main_state_t;

extern screen_main_state_t Sonometer_screen_main_state;

void Sonometer_create_screen_main();
void Sonometer_tick_screen_main();

void Sonometer_tick_screen_by_id(enum ScreensEnum screenId);
void Sonometer_tick_screen(int screen_index);

void Sonometer_create_screens();

// Global state variables

extern lv_style_t Sonometer_graph_scale_low_main_style;
extern lv_style_t Sonometer_graph_scale_mid_main_style;
extern lv_style_t Sonometer_graph_scale_high_main_style;
extern lv_style_t Sonometer_graph_scale_danger_main_style;
extern lv_style_t Sonometer_fft_frequency_scale_bass_main_style;
extern lv_style_t Sonometer_fft_frequency_scale_mids_main_style;
extern lv_style_t Sonometer_fft_frequency_scale_highs_main_style;

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/