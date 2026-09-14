#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t RemoteNow_objects;

//
// Event handlers
//

lv_obj_t *RemoteNow_tick_value_change_obj;

//
// Screens
//

void RemoteNow_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    RemoteNow_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            lv_obj_set_pos(obj, 55, 49);
            lv_obj_set_size(obj, 300, 404);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // matrix
                    lv_obj_t *obj = lv_buttonmatrix_create(parent_obj);
                    RemoteNow_objects.matrix = obj;
                    lv_obj_set_pos(obj, LV_PCT(5), LV_PCT(5));
                    lv_obj_set_size(obj, LV_PCT(90), LV_PCT(70));
                    static const char *map[7] = {
                        "Key #0",
                        "Key #1",
                        "Key #2",
                        "\n",
                        "\n",
                        "Key #4",
                        NULL,
                    };
                    static lv_buttonmatrix_ctrl_t RemoteNow_ctrl_map[4] = {
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT,
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT,
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT,
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT,
                    };
                    lv_buttonmatrix_set_map(obj, map);
                    lv_buttonmatrix_set_ctrl_map(obj, RemoteNow_ctrl_map);
                    lv_obj_add_event_cb(obj, action_matrix_button, LV_EVENT_VALUE_CHANGED, (void *)0);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // ArmingButton
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    RemoteNow_objects.arming_button = obj;
                    lv_obj_set_pos(obj, 31, 339);
                    lv_obj_set_size(obj, 238, 50);
                    lv_obj_add_event_cb(obj, action_arming_button, LV_EVENT_CLICKED, (void *)0);
                    lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0x21f339), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // ArmingButtonLabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            RemoteNow_objects.arming_button_label = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "");
                        }
                    }
                }
            }
        }
    }
    
    RemoteNow_tick_screen_main();
}

void RemoteNow_tick_screen_main() {
    {
        const char *new_val = get_var_arming_state_text();
        const char *cur_val = lv_label_get_text(RemoteNow_objects.arming_button_label);
        if (strcmp(new_val, cur_val) != 0) {
            RemoteNow_tick_value_change_obj = RemoteNow_objects.arming_button_label;
            lv_label_set_text(RemoteNow_objects.arming_button_label, new_val);
            RemoteNow_tick_value_change_obj = NULL;
        }
    }
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t RemoteNow_tick_screen_funcs[] = {
    RemoteNow_tick_screen_main,
};
void RemoteNow_tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        RemoteNow_tick_screen_funcs[screen_index]();
    }
}
void RemoteNow_tick_screen_by_id(enum ScreensEnum screenId) {
    RemoteNow_tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t RemoteNow_fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t RemoteNow_active_theme_index = 0;

//
//
//

void RemoteNow_create_screens() {

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    RemoteNow_create_screen_main();
}
