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


typedef struct {
    lv_obj_t *obj;
    char symbol[16];
    
} menu_section;


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/
