#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif


extern void action_button_pressed(lv_event_t * e);
extern void action_slider_changed(lv_event_t * e);

extern bool get_dim_screen_value(void); 
extern void set_dim_screen_value(bool value); 

extern bool get_user_wake_up_button_value(void); 
extern void set_user_wake_up_button_value(bool value); 

extern bool get_vibrations_value(void); 
extern void set_vibrations_value(bool value); 

extern bool get_wifi_value(void); 
extern void set_wifi_value(bool value); 

extern bool get_bluetooth_value(void); 
extern void set_bluetooth_value(bool value); 

extern bool get_espnow_value(void); 
extern void set_espnow_value(bool value); 

extern bool get_upgrade_value(void); 
extern void set_upgrade_value(bool value); 

extern const char *get_version_value(const char* prefix, const char *suffix); 
extern void set_version_value(const char * value); 

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/
