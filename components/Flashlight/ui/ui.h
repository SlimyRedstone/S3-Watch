#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>




    #if !defined(EEZ_FOR_LVGL)
    #include "screens.h"
    #endif

    #ifdef __cplusplus
    extern "C" {
    #endif

        

        void Flashlight_ui_init();
        void Flashlight_ui_tick();
        
        #if !defined(EEZ_FOR_LVGL)
        void Flashlight_loadScreen(enum ScreensEnum screenId);
        #endif

    #ifdef __cplusplus
    }
    #endif

#endif // EEZ_LVGL_UI_GUI_H