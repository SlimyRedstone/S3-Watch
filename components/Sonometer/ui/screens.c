#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t Sonometer_objects;

//
// Event handlers
//

lv_obj_t *Sonometer_tick_value_change_obj;

screen_main_state_t Sonometer_screen_main_state;

lv_style_t Sonometer_graph_scale_low_main_style;
static bool Sonometer_graph_scale_low_main_style_initialized;
lv_style_t Sonometer_graph_scale_mid_main_style;
static bool Sonometer_graph_scale_mid_main_style_initialized;
lv_style_t Sonometer_graph_scale_high_main_style;
static bool Sonometer_graph_scale_high_main_style_initialized;
lv_style_t Sonometer_graph_scale_danger_main_style;
static bool Sonometer_graph_scale_danger_main_style_initialized;

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
            lv_obj_t *obj = lv_obj_create(parent_obj);
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 410, 502);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // sound_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    Sonometer_objects.sound_label = obj;
                    lv_obj_set_pos(obj, 113, 251);
                    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    
    Sonometer_tick_screen_main();
}

void Sonometer_tick_screen_main() {
    {
        const char *new_val = get_var_sound_level();
        const char *cur_val = lv_label_get_text(Sonometer_objects.sound_label);
        if (strcmp(new_val, cur_val) != 0) {
            Sonometer_tick_value_change_obj = Sonometer_objects.sound_label;
            lv_label_set_text(Sonometer_objects.sound_label, new_val);
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
