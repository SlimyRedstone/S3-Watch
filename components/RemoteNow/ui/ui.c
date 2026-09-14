#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"

#include <string.h>

static int16_t RemoteNow_currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&RemoteNow_objects)[index];
}

void RemoteNow_loadScreen(enum ScreensEnum screenId) {
    RemoteNow_currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(RemoteNow_currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void RemoteNow_ui_init() {
    RemoteNow_create_screens();
    RemoteNow_loadScreen(SCREEN_ID_MAIN);
}

void RemoteNow_ui_tick() {
    RemoteNow_tick_screen(RemoteNow_currentScreen);
}