#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>




#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif



void BLEMultiMedia_ui_init();
void BLEMultiMedia_ui_tick();

void BLEMultiMedia_loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H