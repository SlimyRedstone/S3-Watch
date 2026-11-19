#pragma once

/*
@brief EEZ Studio actions used by ui/screens.c: this project's EEZ export has no actions.h
@note Force-included into ui/screens.c by CMakeLists.txt (ui/ stays as exported), defined in Sonometer.cpp
*/
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void action_fft_options_slider_changed(lv_event_t *e);

#ifdef __cplusplus
}
#endif
