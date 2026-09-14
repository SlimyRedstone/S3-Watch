#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"







#include <string.h>

static int16_t BLEMultiMedia_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&BLEMultiMedia_objects)[index];
}

void BLEMultiMedia_loadScreen(enum ScreensEnum screenId) {
    BLEMultiMedia_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(BLEMultiMedia_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void BLEMultiMedia_ui_init() {
    BLEMultiMedia_create_screens();
    BLEMultiMedia_loadScreen(SCREEN_ID_MAIN);
}

void BLEMultiMedia_ui_tick() {
    BLEMultiMedia_tick_screen(BLEMultiMedia_currentScreen);
}

