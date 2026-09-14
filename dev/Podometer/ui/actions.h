#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_step_spinbox_increment(lv_event_t * e);
extern void action_step_spinbox_decrement(lv_event_t * e);
extern void action_sensitivity_slider(lv_event_t * e);
extern void action_save_sdcard_option_switch(lv_event_t * e);
extern void action_height_spinbox_increment(lv_event_t * e);
extern void action_height_spinbox_decrement(lv_event_t * e);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/