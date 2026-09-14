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
    FLOW_GLOBAL_VARIABLE_LABEL_CONNECTED = 0,
    FLOW_GLOBAL_VARIABLE_CONNECTION_DROPMENU_ITEMS = 1
};

// Native global variables

extern const char *get_var_label_connected();
extern void set_var_label_connected(const char *value);
extern const char *get_var_connection_dropmenu_items();
extern void set_var_connection_dropmenu_items(const char *value);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/