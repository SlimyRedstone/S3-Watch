#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"







#include <string.h>

static int16_t Settings_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&Settings_objects)[index];
}

void Settings_loadScreen(enum ScreensEnum screenId) {
    Settings_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(Settings_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void Settings_ui_init() {
    Settings_create_screens();
    Settings_loadScreen(SCREEN_ID_MAIN);
}

void Settings_ui_tick() {
    Settings_tick_screen(Settings_currentScreen);
}
