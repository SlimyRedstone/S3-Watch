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
    FLOW_GLOBAL_VARIABLE_NONE
};

// Native global variables

extern int32_t get_var_meter_value();
extern void set_var_meter_value(int32_t value);
extern const char *get_var_sound_level();
extern void set_var_sound_level(const char *value);
extern const char *get_var_fft_sensitivity_value();
extern void set_var_fft_sensitivity_value(const char *value);
extern bool get_var_fft_peaks_list_disabled();
extern void set_var_fft_peaks_list_disabled(bool value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/