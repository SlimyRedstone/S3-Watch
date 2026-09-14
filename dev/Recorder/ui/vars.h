#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations



// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_RECORDING_LED_BRIGHTNESS = 0,
    FLOW_GLOBAL_VARIABLE_SD_CARD_SIZE_TEXT = 1,
    FLOW_GLOBAL_VARIABLE_RECORD_TIME_TEXT = 2,
    FLOW_GLOBAL_VARIABLE_RECORD_BUTTON_TEXT = 3,
    FLOW_GLOBAL_VARIABLE_PLAY_BUTTON_TEXT = 4,
    FLOW_GLOBAL_VARIABLE_PLAYBACK_TIME_TEXT = 5
};

// Native global variables

extern int32_t get_var_recording_led_brightness();
extern void set_var_recording_led_brightness(int32_t value);
extern const char *get_var_sd_card_size_text();
extern void set_var_sd_card_size_text(const char *value);
extern const char *get_var_record_time_text();
extern void set_var_record_time_text(const char *value);
extern const char *get_var_record_button_text();
extern void set_var_record_button_text(const char *value);
extern const char *get_var_play_button_text();
extern void set_var_play_button_text(const char *value);
extern const char *get_var_playback_time_text();
extern void set_var_playback_time_text(const char *value);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/