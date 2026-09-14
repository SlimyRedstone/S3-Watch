#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"
#include <string.h>







static int16_t Flashlight_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&Flashlight_objects)[index];
}

void Flashlight_loadScreen(enum ScreensEnum screenId) {
    Flashlight_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(Flashlight_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 100, 0, false);
}

void Flashlight_ui_init() {
    Flashlight_create_screens();
    Flashlight_loadScreen(SCREEN_ID_MAIN);
}

void Flashlight_ui_tick() {
    Flashlight_tick_screen(Flashlight_currentScreen);
}
