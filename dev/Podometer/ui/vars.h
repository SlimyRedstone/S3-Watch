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
    FLOW_GLOBAL_VARIABLE_SENSITIVITY_SLIDER_VALUE = 0,
    FLOW_GLOBAL_VARIABLE_SENSITIVITY_SLIDER_LABEL = 1,
    FLOW_GLOBAL_VARIABLE_STEP_COUNT = 2,
    FLOW_GLOBAL_VARIABLE_STEP_COUNT_PERCENT = 3,
    FLOW_GLOBAL_VARIABLE_STEP_COUNT_STR = 4,
    FLOW_GLOBAL_VARIABLE_HEIGHT_VALUE = 5
};

// Native global variables

extern int32_t get_var_sensitivity_slider_value();
extern void set_var_sensitivity_slider_value(int32_t value);
extern const char *get_var_sensitivity_slider_label();
extern void set_var_sensitivity_slider_label(const char *value);
extern int32_t get_var_step_count();
extern void set_var_step_count(int32_t value);
extern int32_t get_var_step_count_percent();
extern void set_var_step_count_percent(int32_t value);
extern const char *get_var_step_count_str();
extern void set_var_step_count_str(const char *value);
extern float get_var_height_value();
extern void set_var_height_value(float value);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/