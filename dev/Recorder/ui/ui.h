#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>




#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif



void Recorder_ui_init();
void Recorder_ui_tick();

void Recorder_loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H