#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t Podometer_objects;
lv_obj_t *Podometer_tick_value_change_obj;

static void Podometer_event_handler_cb_main_sensitivity_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (Podometer_tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_sensitivity_slider_value(value);
        }
    }
}

static void Podometer_event_handler_cb_main_height_selector(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (Podometer_tick_value_change_obj != ta) {
            int32_t value = lv_spinbox_get_value(ta);
            set_var_height_value(value);
        }
    }
}

void Podometer_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    Podometer_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_tabview_create(parent_obj);
            lv_tabview_set_tab_bar_position(obj, LV_DIR_TOP);
            lv_tabview_set_tab_bar_size(obj, 40);
            lv_obj_set_pos(obj, 0, 40);
            lv_obj_set_size(obj, LV_PCT(100), 462);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // MainTab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Main");
                    Podometer_objects.main_tab = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // step_icon_img
                            lv_obj_t *obj = lv_img_create(parent_obj);
                            Podometer_objects.step_icon_img = obj;
                            lv_obj_set_pos(obj, 125, LV_PCT(15));
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_img_set_src(obj, &Podometer_img_step_icon);
                        }
                        {
                            lv_obj_t *obj = lv_spangroup_create(parent_obj);
                            lv_obj_set_pos(obj, 37, 210);
                            lv_obj_set_size(obj, 305, 77);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 17, 4);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "Today's step count:");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // step_count_label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    Podometer_objects.step_count_label = obj;
                                    lv_obj_set_pos(obj, 135, 44);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // step_count_percent_bar
                            lv_obj_t *obj = lv_bar_create(parent_obj);
                            Podometer_objects.step_count_percent_bar = obj;
                            lv_obj_set_pos(obj, 37, 322);
                            lv_obj_set_size(obj, 305, 34);
                            lv_bar_set_value(obj, 10, LV_ANIM_ON);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffbaf34e), LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffbaf34e), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
                {
                    // OptionsTab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Options");
                    Podometer_objects.options_tab = obj;
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, -36, 33);
                            lv_obj_set_size(obj, 355, 121);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 11, 12);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "Daily step count to reach");
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    lv_obj_set_pos(obj, 0, 41);
                                    lv_obj_set_size(obj, 341, 79);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // step_spinbox_decr_button
                                            lv_obj_t *obj = lv_btn_create(parent_obj);
                                            Podometer_objects.step_spinbox_decr_button = obj;
                                            lv_obj_set_pos(obj, 29, 22);
                                            lv_obj_set_size(obj, 50, 50);
                                            lv_obj_add_event_cb(obj, action_step_spinbox_decrement, LV_EVENT_PRESSED, (void *)0);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_PRESSED);
                                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_text(obj, "-");
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                }
                                            }
                                        }
                                        {
                                            // step_count_selector
                                            lv_obj_t *obj = lv_spinbox_create(parent_obj);
                                            Podometer_objects.step_count_selector = obj;
                                            lv_obj_set_pos(obj, 101, 9);
                                            lv_obj_set_size(obj, 120, 60);
                                            lv_spinbox_set_digit_format(obj, 5, 0);
                                            lv_spinbox_set_range(obj, 0, 99999);
                                            lv_spinbox_set_rollover(obj, true);
                                            lv_spinbox_set_step(obj, 10);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // step_spinbox_incr_button
                                            lv_obj_t *obj = lv_btn_create(parent_obj);
                                            Podometer_objects.step_spinbox_incr_button = obj;
                                            lv_obj_set_pos(obj, 255, 10);
                                            lv_obj_set_size(obj, 50, 50);
                                            lv_obj_add_event_cb(obj, action_step_spinbox_increment, LV_EVENT_PRESSED, (void *)0);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_PRESSED);
                                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_text(obj, "+");
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 21, 58);
                            lv_obj_set_size(obj, 341, 82);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // sensitivity_slider
                                    lv_obj_t *obj = lv_slider_create(parent_obj);
                                    Podometer_objects.sensitivity_slider = obj;
                                    lv_obj_set_pos(obj, LV_PCT(5), 7);
                                    lv_obj_set_size(obj, LV_PCT(90), 20);
                                    lv_obj_add_event_cb(obj, action_sensitivity_slider, LV_EVENT_VALUE_CHANGED, (void *)0);
                                    lv_obj_add_event_cb(obj, Podometer_event_handler_cb_main_sensitivity_slider, LV_EVENT_ALL, 0);
                                    lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                                    lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
                                }
                                {
                                    // sensitivity_label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    Podometer_objects.sensitivity_label = obj;
                                    lv_obj_set_pos(obj, 64, 43);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 23, 152);
                            lv_obj_set_size(obj, 341, 109);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 125, 5);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "Height");
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    lv_obj_set_pos(obj, 0, 29);
                                    lv_obj_set_size(obj, 341, 79);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // height_spinbox_decr_button
                                            lv_obj_t *obj = lv_btn_create(parent_obj);
                                            Podometer_objects.height_spinbox_decr_button = obj;
                                            lv_obj_set_pos(obj, 29, 22);
                                            lv_obj_set_size(obj, 50, 50);
                                            lv_obj_add_event_cb(obj, action_height_spinbox_decrement, LV_EVENT_PRESSED, (void *)0);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_PRESSED);
                                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_text(obj, "-");
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                }
                                            }
                                        }
                                        {
                                            // height_selector
                                            lv_obj_t *obj = lv_spinbox_create(parent_obj);
                                            Podometer_objects.height_selector = obj;
                                            lv_obj_set_pos(obj, -373, 9);
                                            lv_obj_set_size(obj, 84, 60);
                                            lv_spinbox_set_digit_format(obj, 3, 1);
                                            lv_spinbox_set_range(obj, 140, 250);
                                            lv_spinbox_set_rollover(obj, true);
                                            lv_spinbox_set_step(obj, 1);
                                            lv_obj_add_event_cb(obj, Podometer_event_handler_cb_main_height_selector, LV_EVENT_ALL, 0);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // height_spinbox_incr_button
                                            lv_obj_t *obj = lv_btn_create(parent_obj);
                                            Podometer_objects.height_spinbox_incr_button = obj;
                                            lv_obj_set_pos(obj, 255, 10);
                                            lv_obj_set_size(obj, 50, 50);
                                            lv_obj_add_event_cb(obj, action_height_spinbox_increment, LV_EVENT_PRESSED, (void *)0);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1e8de4), LV_PART_MAIN | LV_STATE_PRESSED);
                                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_text(obj, "+");
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 23, 427);
                            lv_obj_set_size(obj, 341, 42);
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
                                    // sdcard_option_switch
                                    lv_obj_t *obj = lv_switch_create(parent_obj);
                                    Podometer_objects.sdcard_option_switch = obj;
                                    lv_obj_set_pos(obj, 25, 8);
                                    lv_obj_set_size(obj, 50, 26);
                                    lv_obj_add_event_cb(obj, action_save_sdcard_option_switch, LV_EVENT_VALUE_CHANGED, (void *)0);
                                }
                                {
                                    // sd_save_switch_label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    Podometer_objects.sd_save_switch_label = obj;
                                    lv_obj_set_pos(obj, 89, 7);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "Save to SD Card");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void Podometer_tick_screen_main() {
    {
        const char *new_val = get_var_step_count_str();
        const char *cur_val = lv_label_get_text(Podometer_objects.step_count_label);
        if (strcmp(new_val, cur_val) != 0) {
            Podometer_tick_value_change_obj = Podometer_objects.step_count_label;
            lv_label_set_text(Podometer_objects.step_count_label, new_val);
            Podometer_tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_sensitivity_slider_value();
        int32_t cur_val = lv_slider_get_value(Podometer_objects.sensitivity_slider);
        if (new_val != cur_val) {
            Podometer_tick_value_change_obj = Podometer_objects.sensitivity_slider;
            lv_slider_set_value(Podometer_objects.sensitivity_slider, new_val, LV_ANIM_OFF);
            Podometer_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_sensitivity_slider_label();
        const char *cur_val = lv_label_get_text(Podometer_objects.sensitivity_label);
        if (strcmp(new_val, cur_val) != 0) {
            Podometer_tick_value_change_obj = Podometer_objects.sensitivity_label;
            lv_label_set_text(Podometer_objects.sensitivity_label, new_val);
            Podometer_tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_height_value();
        int32_t cur_val = lv_spinbox_get_value(Podometer_objects.height_selector);
        if (new_val != cur_val) {
            Podometer_tick_value_change_obj = Podometer_objects.height_selector;
            lv_spinbox_set_value(Podometer_objects.height_selector, new_val);
            Podometer_tick_value_change_obj = NULL;
        }
    }
}


void Podometer_create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    Podometer_create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t Podometer_tick_screen_funcs[] = {
    Podometer_tick_screen_main,
};

void Podometer_tick_screen(int screen_index) {
    Podometer_tick_screen_funcs[screen_index]();
}
