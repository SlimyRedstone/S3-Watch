#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *height_selector;
    lv_obj_t *height_spinbox_decr_button;
    lv_obj_t *height_spinbox_incr_button;
    lv_obj_t *main_tab;
    lv_obj_t *options_tab;
    lv_obj_t *sd_save_switch_label;
    lv_obj_t *sdcard_option_switch;
    lv_obj_t *sensitivity_label;
    lv_obj_t *sensitivity_slider;
    lv_obj_t *step_count_label;
    lv_obj_t *step_count_percent_bar;
    lv_obj_t *step_count_selector;
    lv_obj_t *step_icon_img;
    lv_obj_t *step_spinbox_decr_button;
    lv_obj_t *step_spinbox_incr_button;
} objects_t;

extern objects_t Podometer_objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void Podometer_create_screen_main();
void Podometer_tick_screen_main();

void Podometer_create_screens();
void Podometer_tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/