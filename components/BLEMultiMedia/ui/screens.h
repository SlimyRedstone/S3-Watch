#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *connection_button;
    lv_obj_t *connection_button_label;
    lv_obj_t *connection_container;
    lv_obj_t *connection_dropmenu;
    lv_obj_t *connection_label;
    lv_obj_t *media_buttons;
} objects_t;

extern objects_t BLEMultiMedia_objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void BLEMultiMedia_create_screen_main();
void BLEMultiMedia_tick_screen_main();

void BLEMultiMedia_create_screens();
void BLEMultiMedia_tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/