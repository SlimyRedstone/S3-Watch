#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_record_button_pressed(lv_event_t * e);
extern void action_volume_slider_changed(lv_event_t * e);
extern void action_file_list_selected(lv_event_t * e);
extern void action_play_button_pressed(lv_event_t * e);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/