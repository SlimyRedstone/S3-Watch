#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"







#include <string.h>

static int16_t Recorder_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&Recorder_objects)[index];
}

void Recorder_loadScreen(enum ScreensEnum screenId) {
    Recorder_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(Recorder_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void Recorder_ui_init() {
    Recorder_create_screens();
    Recorder_loadScreen(SCREEN_ID_MAIN);
}

void Recorder_ui_tick() {
    Recorder_tick_screen(Recorder_currentScreen);
}

