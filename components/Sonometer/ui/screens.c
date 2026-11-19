#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t Sonometer_objects;

screen_main_state_t Sonometer_screen_main_state;

// Global state variables

lv_style_t Sonometer_graph_scale_low_main_style;
static bool Sonometer_graph_scale_low_main_style_initialized;
lv_style_t Sonometer_graph_scale_mid_main_style;
static bool Sonometer_graph_scale_mid_main_style_initialized;
lv_style_t Sonometer_graph_scale_high_main_style;
static bool Sonometer_graph_scale_high_main_style_initialized;
lv_style_t Sonometer_graph_scale_danger_main_style;
static bool Sonometer_graph_scale_danger_main_style_initialized;
lv_style_t Sonometer_fft_frequency_scale_bass_main_style;
static bool Sonometer_fft_frequency_scale_bass_main_style_initialized;
lv_style_t Sonometer_fft_frequency_scale_mids_main_style;
static bool Sonometer_fft_frequency_scale_mids_main_style_initialized;
lv_style_t Sonometer_fft_frequency_scale_highs_main_style;
static bool Sonometer_fft_frequency_scale_highs_main_style_initialized;

//
// Event handlers
//

lv_obj_t *Sonometer_tick_value_change_obj;

//
// Screens
//

void Sonometer_create_screen_main() {
    screen_main_state_t *state = &Sonometer_screen_main_state;
    (void)state;
    lv_obj_t *obj = lv_obj_create(0);
    Sonometer_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // sonometer_tabs
            lv_obj_t *obj = lv_tabview_create(parent_obj);
            Sonometer_objects.sonometer_tabs = obj;
            lv_obj_set_pos(obj, 0, 40);
            lv_obj_set_size(obj, LV_PCT(100), 462);
            lv_tabview_set_tab_bar_position(obj, LV_DIR_TOP);
            lv_tabview_set_tab_bar_size(obj, 40);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // amplitude_tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Amplitude");
                    Sonometer_objects.amplitude_tab = obj;
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // main_container
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            Sonometer_objects.main_container = obj;
                            lv_obj_set_pos(obj, -16, -16);
                            lv_obj_set_size(obj, 410, 422);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // sound_label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    Sonometer_objects.sound_label = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "");
                                }
                                {
                                    // graph_container
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    Sonometer_objects.graph_container = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, 410, 288);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // graph_scale
                                            lv_obj_t *obj = lv_scale_create(parent_obj);
                                            Sonometer_objects.graph_scale = obj;
                                            lv_obj_set_pos(obj, 0, 26);
                                            lv_obj_set_size(obj, 37, 240);
                                            lv_scale_set_mode(obj, LV_SCALE_MODE_VERTICAL_LEFT);
                                            lv_scale_set_range(obj, 30, 120);
                                            lv_scale_set_angle_range(obj, 0);
                                            lv_scale_set_rotation(obj, 0);
                                            lv_scale_set_total_tick_count(obj, 19);
                                            lv_scale_set_major_tick_every(obj, 6);
                                            lv_scale_set_post_draw(obj, true);
                                            lv_scale_set_draw_ticks_on_top(obj, true);
                                            lv_scale_set_label_show(obj, true);
                                            {
                                                state->low = lv_scale_add_section(obj);
                                                lv_scale_section_set_range(state->low, 30, 65);
                                                {
                                                    if (!Sonometer_graph_scale_low_main_style_initialized) {
                                                        lv_style_init(&Sonometer_graph_scale_low_main_style);
                                                        Sonometer_graph_scale_low_main_style_initialized = true;
                                                        lv_style_set_line_width(&Sonometer_graph_scale_low_main_style, 18);
                                                        lv_style_set_line_color(&Sonometer_graph_scale_low_main_style, lv_color_hex(0x04ff00));
                                                        lv_style_set_line_opa(&Sonometer_graph_scale_low_main_style, 160);
                                                    }
                                                    lv_scale_section_set_style(state->low, LV_PART_MAIN, &Sonometer_graph_scale_low_main_style);
                                                }
                                            }
                                            {
                                                state->mid = lv_scale_add_section(obj);
                                                lv_scale_section_set_range(state->mid, 65, 85);
                                                {
                                                    if (!Sonometer_graph_scale_mid_main_style_initialized) {
                                                        lv_style_init(&Sonometer_graph_scale_mid_main_style);
                                                        Sonometer_graph_scale_mid_main_style_initialized = true;
                                                        lv_style_set_line_width(&Sonometer_graph_scale_mid_main_style, 18);
                                                        lv_style_set_line_color(&Sonometer_graph_scale_mid_main_style, lv_color_hex(0xffe200));
                                                        lv_style_set_line_opa(&Sonometer_graph_scale_mid_main_style, 160);
                                                    }
                                                    lv_scale_section_set_style(state->mid, LV_PART_MAIN, &Sonometer_graph_scale_mid_main_style);
                                                }
                                            }
                                            {
                                                state->high = lv_scale_add_section(obj);
                                                lv_scale_section_set_range(state->high, 85, 105);
                                                {
                                                    if (!Sonometer_graph_scale_high_main_style_initialized) {
                                                        lv_style_init(&Sonometer_graph_scale_high_main_style);
                                                        Sonometer_graph_scale_high_main_style_initialized = true;
                                                        lv_style_set_line_width(&Sonometer_graph_scale_high_main_style, 18);
                                                        lv_style_set_line_color(&Sonometer_graph_scale_high_main_style, lv_color_hex(0xff0000));
                                                        lv_style_set_line_opa(&Sonometer_graph_scale_high_main_style, 160);
                                                    }
                                                    lv_scale_section_set_style(state->high, LV_PART_MAIN, &Sonometer_graph_scale_high_main_style);
                                                }
                                            }
                                            {
                                                state->danger = lv_scale_add_section(obj);
                                                lv_scale_section_set_range(state->danger, 105, 120);
                                                {
                                                    if (!Sonometer_graph_scale_danger_main_style_initialized) {
                                                        lv_style_init(&Sonometer_graph_scale_danger_main_style);
                                                        Sonometer_graph_scale_danger_main_style_initialized = true;
                                                        lv_style_set_line_width(&Sonometer_graph_scale_danger_main_style, 18);
                                                        lv_style_set_line_color(&Sonometer_graph_scale_danger_main_style, lv_color_hex(0xf100ff));
                                                        lv_style_set_line_opa(&Sonometer_graph_scale_danger_main_style, 160);
                                                    }
                                                    lv_scale_section_set_style(state->danger, LV_PART_MAIN, &Sonometer_graph_scale_danger_main_style);
                                                }
                                            }
                                            lv_obj_set_style_pad_top(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_bottom(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_length(obj, 8, LV_PART_INDICATOR);
                                            lv_obj_set_style_line_opa(obj, 255, LV_PART_INDICATOR);
                                        }
                                        {
                                            // sound_graph
                                            lv_obj_t *obj = lv_chart_create(parent_obj);
                                            Sonometer_objects.sound_graph = obj;
                                            lv_obj_set_pos(obj, 37, 26);
                                            lv_obj_set_size(obj, 362, 240);
                                            lv_obj_set_style_line_opa(obj, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_line_dash_gap(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_line_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_line_dash_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_line_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_margin_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_margin_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_margin_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_margin_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_row(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0x4c4c4c), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                {
                    // fft_tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "FFT");
                    Sonometer_objects.fft_tab = obj;
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // fft_container
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            Sonometer_objects.fft_container = obj;
                            lv_obj_set_pos(obj, -16, -16);
                            lv_obj_set_size(obj, 410, 422);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // fft_frequency_scale
                                    lv_obj_t *obj = lv_scale_create(parent_obj);
                                    Sonometer_objects.fft_frequency_scale = obj;
                                    lv_obj_set_pos(obj, 13, 20);
                                    lv_obj_set_size(obj, 384, 280);
                                    lv_scale_set_mode(obj, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
                                    lv_scale_set_range(obj, 20, 20000);
                                    lv_scale_set_angle_range(obj, 0);
                                    lv_scale_set_rotation(obj, 0);
                                    lv_scale_set_total_tick_count(obj, 41);
                                    lv_scale_set_major_tick_every(obj, 4);
                                    lv_scale_set_post_draw(obj, true);
                                    lv_scale_set_draw_ticks_on_top(obj, true);
                                    lv_scale_set_label_show(obj, true);
                                    static const char *label_texts[12] = {
                                        "31",
                                        "63",
                                        "125",
                                        "250",
                                        "500",
                                        "1k",
                                        "2k",
                                        "4k",
                                        "8k",
                                        "16k",
                                        "20k",
                                        NULL
                                    };
                                    lv_scale_set_text_src(obj, label_texts);
                                    {
                                        state->bass = lv_scale_add_section(obj);
                                        lv_scale_section_set_range(state->bass, 20, 300);
                                        {
                                            if (!Sonometer_fft_frequency_scale_bass_main_style_initialized) {
                                                lv_style_init(&Sonometer_fft_frequency_scale_bass_main_style);
                                                Sonometer_fft_frequency_scale_bass_main_style_initialized = true;
                                                lv_style_set_line_width(&Sonometer_fft_frequency_scale_bass_main_style, 252);
                                                lv_style_set_line_color(&Sonometer_fft_frequency_scale_bass_main_style, lv_color_hex(0xff0000));
                                                lv_style_set_line_opa(&Sonometer_fft_frequency_scale_bass_main_style, 80);
                                            }
                                            lv_scale_section_set_style(state->bass, LV_PART_MAIN, &Sonometer_fft_frequency_scale_bass_main_style);
                                        }
                                    }
                                    {
                                        state->mids = lv_scale_add_section(obj);
                                        lv_scale_section_set_range(state->mids, 300, 6000);
                                        {
                                            if (!Sonometer_fft_frequency_scale_mids_main_style_initialized) {
                                                lv_style_init(&Sonometer_fft_frequency_scale_mids_main_style);
                                                Sonometer_fft_frequency_scale_mids_main_style_initialized = true;
                                                lv_style_set_line_width(&Sonometer_fft_frequency_scale_mids_main_style, 252);
                                                lv_style_set_line_color(&Sonometer_fft_frequency_scale_mids_main_style, lv_color_hex(0xffe200));
                                                lv_style_set_line_opa(&Sonometer_fft_frequency_scale_mids_main_style, 80);
                                            }
                                            lv_scale_section_set_style(state->mids, LV_PART_MAIN, &Sonometer_fft_frequency_scale_mids_main_style);
                                        }
                                    }
                                    {
                                        state->highs = lv_scale_add_section(obj);
                                        lv_scale_section_set_range(state->highs, 6000, 20000);
                                        {
                                            if (!Sonometer_fft_frequency_scale_highs_main_style_initialized) {
                                                lv_style_init(&Sonometer_fft_frequency_scale_highs_main_style);
                                                Sonometer_fft_frequency_scale_highs_main_style_initialized = true;
                                                lv_style_set_line_width(&Sonometer_fft_frequency_scale_highs_main_style, 252);
                                                lv_style_set_line_color(&Sonometer_fft_frequency_scale_highs_main_style, lv_color_hex(0x00beff));
                                                lv_style_set_line_opa(&Sonometer_fft_frequency_scale_highs_main_style, 80);
                                            }
                                            lv_scale_section_set_style(state->highs, LV_PART_MAIN, &Sonometer_fft_frequency_scale_highs_main_style);
                                        }
                                    }
                                    lv_obj_set_style_line_width(obj, 2, LV_PART_MAIN);
                                    lv_obj_set_style_length(obj, 250, LV_PART_ITEMS);
                                    lv_obj_set_style_line_width(obj, 1, LV_PART_ITEMS);
                                    lv_obj_set_style_line_opa(obj, 30, LV_PART_ITEMS);
                                    lv_obj_set_style_length(obj, 250, LV_PART_INDICATOR);
                                    lv_obj_set_style_line_width(obj, 1, LV_PART_INDICATOR);
                                    lv_obj_set_style_line_opa(obj, 180, LV_PART_INDICATOR);
                                }
                                {
                                    // fft_graph
                                    lv_obj_t *obj = lv_chart_create(parent_obj);
                                    Sonometer_objects.fft_graph = obj;
                                    lv_obj_set_pos(obj, 13, 20);
                                    lv_obj_set_size(obj, 384, 252);
                                    lv_obj_set_style_line_opa(obj, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_gap(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_width(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_row(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0x4c4c4c), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_ITEMS | LV_STATE_DEFAULT);
                                }
                                {
                                    // fft_peaks_list
                                    lv_obj_t *obj = lv_list_create(parent_obj);
                                    Sonometer_objects.fft_peaks_list = obj;
                                    lv_obj_set_pos(obj, 25, 302);
                                    lv_obj_set_size(obj, 360, 100);
                                }
                            }
                        }
                    }
                }
                {
                    // fft_options
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Options");
                    Sonometer_objects.fft_options = obj;
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // options_container
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            Sonometer_objects.options_container = obj;
                            lv_obj_set_pos(obj, -16, -16);
                            lv_obj_set_size(obj, 410, 422);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // fft_bars_controls
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    Sonometer_objects.fft_bars_controls = obj;
                                    lv_obj_set_pos(obj, 142, 62);
                                    lv_obj_set_size(obj, 410, 67);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_START, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_left(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // n_bars_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Sonometer_objects.n_bars_label = obj;
                                            lv_obj_set_pos(obj, 10, 32);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text_static(obj, "Number of bars:");
                                        }
                                        {
                                            // n_bars_selector
                                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                                            Sonometer_objects.n_bars_selector = obj;
                                            lv_obj_set_pos(obj, 181, 24);
                                            lv_obj_set_size(obj, 67, LV_SIZE_CONTENT);
                                            lv_dropdown_set_options_static(obj, "16\n32\n64\n128\n512");
                                            lv_dropdown_set_dir(obj, LV_DIR_RIGHT);
                                            lv_dropdown_set_symbol(obj, LV_SYMBOL_RIGHT);
                                            lv_dropdown_set_selected(obj, 0);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                                {
                                    // options_spearator_1
                                    lv_obj_t *obj = lv_line_create(parent_obj);
                                    Sonometer_objects.options_spearator_1 = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    static lv_point_precise_t Sonometer_line_points[] = {
                                        { 0, 0 },
                                        { 410, 0 }
                                    };
                                    lv_line_set_points(obj, Sonometer_line_points, 2);
                                    lv_obj_set_style_line_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_gap(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_width(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // fft_range_controls
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    Sonometer_objects.fft_range_controls = obj;
                                    lv_obj_set_pos(obj, -420, 76);
                                    lv_obj_set_size(obj, 410, 168);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // min_sensitivity_container
                                            lv_obj_t *obj = lv_obj_create(parent_obj);
                                            Sonometer_objects.min_sensitivity_container = obj;
                                            lv_obj_set_pos(obj, 0, 0);
                                            lv_obj_set_size(obj, 410, 75);
                                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    // fft_min_sens_label
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    Sonometer_objects.fft_min_sens_label = obj;
                                                    lv_obj_set_pos(obj, 15, 4);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text_static(obj, "Min. sensitivity:");
                                                }
                                                {
                                                    // fft_min_sens_slider
                                                    lv_obj_t *obj = lv_slider_create(parent_obj);
                                                    Sonometer_objects.fft_min_sens_slider = obj;
                                                    lv_obj_set_pos(obj, 15, 45);
                                                    lv_obj_set_size(obj, 379, 19);
                                                    lv_slider_set_range(obj, -100, 30);
                                                    lv_slider_set_value(obj, -48, LV_ANIM_OFF);
                                                    lv_obj_add_event_cb(obj, action_fft_options_slider_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
                                                }
                                                {
                                                    // fft_min_sens_value_label
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    Sonometer_objects.fft_min_sens_value_label = obj;
                                                    lv_obj_set_pos(obj, 205, 4);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "");
                                                }
                                            }
                                        }
                                        {
                                            // options_spearator_2
                                            lv_obj_t *obj = lv_line_create(parent_obj);
                                            Sonometer_objects.options_spearator_2 = obj;
                                            lv_obj_set_pos(obj, 0, LV_PCT(0));
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            static lv_point_precise_t Sonometer_line_points[] = {
                                                { 15, 0 },
                                                { 394, 0 }
                                            };
                                            lv_line_set_points(obj, Sonometer_line_points, 2);
                                            lv_obj_set_style_line_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_line_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // max_sensitivity_container
                                            lv_obj_t *obj = lv_obj_create(parent_obj);
                                            Sonometer_objects.max_sensitivity_container = obj;
                                            lv_obj_set_pos(obj, 0, 0);
                                            lv_obj_set_size(obj, 410, 75);
                                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    // fft_max_sens_label
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    Sonometer_objects.fft_max_sens_label = obj;
                                                    lv_obj_set_pos(obj, 15, 4);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text_static(obj, "Max. sensitivity:");
                                                }
                                                {
                                                    // fft_max_sens_slider
                                                    lv_obj_t *obj = lv_slider_create(parent_obj);
                                                    Sonometer_objects.fft_max_sens_slider = obj;
                                                    lv_obj_set_pos(obj, 15, 45);
                                                    lv_obj_set_size(obj, 379, 19);
                                                    lv_slider_set_range(obj, 0, 90);
                                                    lv_slider_set_value(obj, 16, LV_ANIM_OFF);
                                                    lv_obj_add_event_cb(obj, action_fft_options_slider_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
                                                }
                                                {
                                                    // fft_max_sens_value_label
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    Sonometer_objects.fft_max_sens_value_label = obj;
                                                    lv_obj_set_pos(obj, 205, 4);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "");
                                                }
                                            }
                                        }
                                    }
                                }
                                {
                                    // options_spearator_3
                                    lv_obj_t *obj = lv_line_create(parent_obj);
                                    Sonometer_objects.options_spearator_3 = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    static lv_point_precise_t Sonometer_line_points[] = {
                                        { 0, 0 },
                                        { 410, 0 }
                                    };
                                    lv_line_set_points(obj, Sonometer_line_points, 2);
                                    lv_obj_set_style_line_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_gap(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_line_dash_width(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // fft_peaks_controls
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    Sonometer_objects.fft_peaks_controls = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, 410, 120);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // fft_peaks_options_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Sonometer_objects.fft_peaks_options_label = obj;
                                            lv_obj_set_pos(obj, 15, 9);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text_static(obj, "Hide FFT peaks:");
                                        }
                                        {
                                            // fft_options_peaks_disabled_switch
                                            lv_obj_t *obj = lv_switch_create(parent_obj);
                                            Sonometer_objects.fft_options_peaks_disabled_switch = obj;
                                            lv_obj_set_pos(obj, 205, 8);
                                            lv_obj_set_size(obj, 50, 25);
                                        }
                                        {
                                            // fft_n_peaks_options_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Sonometer_objects.fft_n_peaks_options_label = obj;
                                            lv_obj_set_pos(obj, 15, 59);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text_static(obj, "Number of peaks:");
                                        }
                                        {
                                            // fft_peaks_n_selector
                                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                                            Sonometer_objects.fft_peaks_n_selector = obj;
                                            lv_obj_set_pos(obj, 227, 49);
                                            lv_obj_set_size(obj, 57, LV_SIZE_CONTENT);
                                            lv_dropdown_set_options_static(obj, "1\n3\n5\n10");
                                            lv_dropdown_set_dir(obj, LV_DIR_RIGHT);
                                            lv_dropdown_set_symbol(obj, LV_SYMBOL_RIGHT);
                                            lv_dropdown_set_selected(obj, 1);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    Sonometer_tick_screen_main();
}

void Sonometer_tick_screen_main() {
    screen_main_state_t *state = &Sonometer_screen_main_state;
    (void)state;
    {
        const char *new_val = get_var_sound_level();
        const char *cur_val = lv_label_get_text(Sonometer_objects.sound_label);
        if (strcmp(new_val, cur_val) != 0) {
            Sonometer_tick_value_change_obj = Sonometer_objects.sound_label;
            lv_label_set_text(Sonometer_objects.sound_label, new_val);
            Sonometer_tick_value_change_obj = NULL;
        }
    }
    {
        bool new_val = get_var_fft_peaks_list_disabled();
        bool cur_val = lv_obj_has_flag(Sonometer_objects.fft_peaks_list, LV_OBJ_FLAG_HIDDEN);
        if (new_val != cur_val) {
            Sonometer_tick_value_change_obj = Sonometer_objects.fft_peaks_list;
            if (new_val) {
                lv_obj_add_flag(Sonometer_objects.fft_peaks_list, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(Sonometer_objects.fft_peaks_list, LV_OBJ_FLAG_HIDDEN);
            }
            Sonometer_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_fft_min_sensitivity_value();
        const char *cur_val = lv_label_get_text(Sonometer_objects.fft_min_sens_value_label);
        if (strcmp(new_val, cur_val) != 0) {
            Sonometer_tick_value_change_obj = Sonometer_objects.fft_min_sens_value_label;
            lv_label_set_text(Sonometer_objects.fft_min_sens_value_label, new_val);
            Sonometer_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_fft_max_sensitivity_value();
        const char *cur_val = lv_label_get_text(Sonometer_objects.fft_max_sens_value_label);
        if (strcmp(new_val, cur_val) != 0) {
            Sonometer_tick_value_change_obj = Sonometer_objects.fft_max_sens_value_label;
            lv_label_set_text(Sonometer_objects.fft_max_sens_value_label, new_val);
            Sonometer_tick_value_change_obj = NULL;
        }
    }
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t Sonometer_tick_screen_funcs[] = {
    Sonometer_tick_screen_main,
};
void Sonometer_tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        Sonometer_tick_screen_funcs[screen_index]();
    }
}
void Sonometer_tick_screen_by_id(enum ScreensEnum screenId) {
    Sonometer_tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t Sonometer_fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t Sonometer_active_theme_index = 0;

//
//
//

void Sonometer_create_screens() {

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    Sonometer_create_screen_main();
}