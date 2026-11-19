#include "ui.h"
#include "screens.h"
#include "images.h"
#include "vars.h"

#include <string.h>

static int16_t Sonometer_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&Sonometer_objects)[index];
}

void Sonometer_loadScreen(enum ScreensEnum screenId) {
    Sonometer_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(Sonometer_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void Sonometer_ui_init() {
    Sonometer_create_screens();
    Sonometer_loadScreen(SCREEN_ID_MAIN);
}

void Sonometer_ui_tick() {
    Sonometer_tick_screen(Sonometer_currentScreen);
}