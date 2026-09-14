#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>




#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif



void Podometer_ui_init();
void Podometer_ui_tick();

void Podometer_loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H