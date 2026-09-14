#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"







#include <string.h>

static int16_t Podometer_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&Podometer_objects)[index];
}

void Podometer_loadScreen(enum ScreensEnum screenId) {
    Podometer_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(Podometer_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void Podometer_ui_init() {
    Podometer_create_screens();
    Podometer_loadScreen(SCREEN_ID_MAIN);
}

void Podometer_ui_tick() {
    Podometer_tick_screen(Podometer_currentScreen);
}

