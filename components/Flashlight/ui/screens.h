#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *flashlight_container;
    lv_obj_t *flashlight_matrix;
} Flashlight_objects_t;

extern Flashlight_objects_t Flashlight_objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void Flashlight_create_screen_main();
void Flashlight_tick_screen_main();

void Flashlight_create_screens();
void Flashlight_tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/