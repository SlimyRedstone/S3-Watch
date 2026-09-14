#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t Recorder_objects;
lv_obj_t *Recorder_tick_value_change_obj;

static void Recorder_event_handler_cb_main_record_led(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
}

void Recorder_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    Recorder_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
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
                    // record_tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Record");
                    Recorder_objects.record_tab = obj;
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_PCT(100), 406);
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
                                    // record_led
                                    lv_obj_t *obj = lv_led_create(parent_obj);
                                    Recorder_objects.record_led = obj;
                                    lv_obj_set_pos(obj, LV_PCT(90), LV_PCT(5));
                                    lv_obj_set_size(obj, 24, 24);
                                    lv_led_set_color(obj, lv_color_hex(4278255377));
                                    lv_obj_add_event_cb(obj, Recorder_event_handler_cb_main_record_led, LV_EVENT_ALL, 0);
                                }
                                {
                                    // spectrum_canvas
                                    lv_obj_t *obj = lv_canvas_create(parent_obj);
                                    Recorder_objects.spectrum_canvas = obj;
                                    lv_obj_set_pos(obj, LV_PCT(2), LV_PCT(25));
                                    lv_obj_set_size(obj, LV_PCT(96), LV_PCT(45));
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    lv_obj_set_pos(obj, 0, 73);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
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
                                            // record_time_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.record_time_label = obj;
                                            lv_obj_set_pos(obj, 0, 0);
                                            lv_obj_set_size(obj, 182, 27);
                                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_line_space(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // sd_card_size_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.sd_card_size_label = obj;
                                            lv_obj_set_pos(obj, 182, 0);
                                            lv_obj_set_size(obj, 180, 27);
                                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_line_space(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                                {
                                    // record_button
                                    lv_obj_t *obj = lv_btn_create(parent_obj);
                                    Recorder_objects.record_button = obj;
                                    lv_obj_set_pos(obj, 94, 309);
                                    lv_obj_set_size(obj, 188, 70);
                                    lv_obj_add_event_cb(obj, action_record_button_pressed, LV_EVENT_CLICKED, (void *)0);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0003af), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 160, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff49b8df), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00af79), LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // record_button_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.record_button_label = obj;
                                            lv_obj_set_pos(obj, -1, 0);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                {
                    // playback_tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Playback");
                    Recorder_objects.playback_tab = obj;
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
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
                                    // record_files_list
                                    lv_obj_t *obj = lv_list_create(parent_obj);
                                    Recorder_objects.record_files_list = obj;
                                    lv_obj_set_pos(obj, LV_PCT(2), LV_PCT(12));
                                    lv_obj_set_size(obj, LV_PCT(96), LV_PCT(45));
                                }
                                {
                                    // volume_slider
                                    lv_obj_t *obj = lv_slider_create(parent_obj);
                                    Recorder_objects.volume_slider = obj;
                                    lv_obj_set_pos(obj, 26, 260);
                                    lv_obj_set_size(obj, 323, 23);
                                    lv_slider_set_value(obj, 50, LV_ANIM_ON);
                                    lv_obj_add_event_cb(obj, action_volume_slider_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0003af), LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff49b8df), LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_stop(obj, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 160, LV_PART_KNOB | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_KNOB | LV_STATE_PRESSED);
                                }
                                {
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    lv_obj_set_pos(obj, 0, 17);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
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
                                            // playback_time_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.playback_time_label = obj;
                                            lv_obj_set_pos(obj, 7, 0);
                                            lv_obj_set_size(obj, 182, 27);
                                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_line_space(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // sd_card_size_label2
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.sd_card_size_label2 = obj;
                                            lv_obj_set_pos(obj, 189, 0);
                                            lv_obj_set_size(obj, 180, 27);
                                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_line_space(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                    }
                                }
                                {
                                    // play_button
                                    lv_obj_t *obj = lv_btn_create(parent_obj);
                                    Recorder_objects.play_button = obj;
                                    lv_obj_set_pos(obj, 94, 309);
                                    lv_obj_set_size(obj, 188, 70);
                                    lv_obj_add_event_cb(obj, action_play_button_pressed, LV_EVENT_CLICKED, (void *)0);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0003af), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 160, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff49b8df), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00af79), LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_PRESSED);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // play_button_label
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            Recorder_objects.play_button_label = obj;
                                            lv_obj_set_pos(obj, -1, 0);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_label_set_text(obj, "");
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    }
}

void Recorder_tick_screen_main() {
    {
        int32_t new_val = get_var_recording_led_brightness();
        if (new_val < 0) new_val = 0;
        else if (new_val > 255) new_val = 255;
        int32_t cur_val = lv_led_get_brightness(Recorder_objects.record_led);
        if (new_val != cur_val) {
            Recorder_tick_value_change_obj = Recorder_objects.record_led;
            lv_led_set_brightness(Recorder_objects.record_led, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_record_time_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.record_time_label);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.record_time_label;
            lv_label_set_text(Recorder_objects.record_time_label, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_record_time_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.sd_card_size_label);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.sd_card_size_label;
            lv_label_set_text(Recorder_objects.sd_card_size_label, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_play_button_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.record_button_label);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.record_button_label;
            lv_label_set_text(Recorder_objects.record_button_label, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_playback_time_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.playback_time_label);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.playback_time_label;
            lv_label_set_text(Recorder_objects.playback_time_label, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_sd_card_size_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.sd_card_size_label2);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.sd_card_size_label2;
            lv_label_set_text(Recorder_objects.sd_card_size_label2, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_play_button_text();
        const char *cur_val = lv_label_get_text(Recorder_objects.play_button_label);
        if (strcmp(new_val, cur_val) != 0) {
            Recorder_tick_value_change_obj = Recorder_objects.play_button_label;
            lv_label_set_text(Recorder_objects.play_button_label, new_val);
            Recorder_tick_value_change_obj = NULL;
        }
    }
}


void Recorder_create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    Recorder_create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t Recorder_tick_screen_funcs[] = {
    Recorder_tick_screen_main,
};

void Recorder_tick_screen(int screen_index) {
    Recorder_tick_screen_funcs[screen_index]();
}
