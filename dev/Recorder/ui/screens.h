#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *play_button;
    lv_obj_t *play_button_label;
    lv_obj_t *playback_tab;
    lv_obj_t *playback_time_label;
    lv_obj_t *record_button;
    lv_obj_t *record_button_label;
    lv_obj_t *record_files_list;
    lv_obj_t *record_led;
    lv_obj_t *record_tab;
    lv_obj_t *record_time_label;
    lv_obj_t *sd_card_size_label;
    lv_obj_t *sd_card_size_label2;
    lv_obj_t *spectrum_canvas;
    lv_obj_t *volume_slider;
} objects_t;

extern objects_t Recorder_objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void Recorder_create_screen_main();
void Recorder_tick_screen_main();

void Recorder_create_screens();
void Recorder_tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/