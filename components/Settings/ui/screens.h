#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	LV_MENU_ITEM_BUILDER_VARIANT_1,
	LV_MENU_ITEM_BUILDER_VARIANT_2
} lv_menu_builder_variant_t;

typedef enum {
    BUTTON_CHECK_UPDATES = 1,
    BUTTON_TEST=255,
} buttons_t;

typedef struct _objects_t {
    lv_obj_t *main;
} objects_t;

extern objects_t Settings_objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};


static lv_obj_t *create_text( lv_obj_t *parent, const char *icon, const char *txt, lv_menu_builder_variant_t builder_variant);
static lv_obj_t *create_slider( lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max, int32_t val);
static lv_obj_t *create_switch( lv_obj_t *parent, const char *icon, const char *txt, bool chk);

void Settings_create_screen_main();
void Settings_tick_screen_main();

void Settings_create_screens();
void Settings_tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
