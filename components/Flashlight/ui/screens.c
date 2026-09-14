#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

Flashlight_objects_t Flashlight_objects;
lv_obj_t *Flashlight_tick_value_change_obj;

void Flashlight_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    Flashlight_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // Flashlight_container
            lv_obj_t *obj = lv_obj_create(parent_obj);
            Flashlight_objects.flashlight_container = obj;
            lv_obj_set_pos(obj, LV_PCT(0), LV_PCT(0));
            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_CLICK_FOCUSABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // flashlight_matrix
                    lv_obj_t *obj = lv_buttonmatrix_create(parent_obj);
                    Flashlight_objects.flashlight_matrix = obj;
                    lv_obj_set_pos(obj, LV_PCT(0), 378);
                    lv_obj_set_size(obj, LV_PCT(100), 90);
                    static const char *map[4] = {
                        "White",
                        "empty",
                        "Red",
                        NULL,
                    };
                    static lv_buttonmatrix_ctrl_t Flashlight_ctrl_map[3] = {
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
                        1 | LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_DISABLED,
                        1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
                    };
                    lv_buttonmatrix_set_map(obj, map);
                    lv_buttonmatrix_set_ctrl_map(obj, Flashlight_ctrl_map);
                    lv_buttonmatrix_set_one_checked(obj, true);
                    lv_obj_add_event_cb(obj, action_flashlight_matrix_handler, LV_EVENT_VALUE_CHANGED, (void *)0);
                    lv_obj_add_state(obj, LV_STATE_FOCUSED);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff33a2f9), LV_PART_ITEMS | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_ITEMS | LV_STATE_DEFAULT);
                }
            }
        }
    }
}

void Flashlight_tick_screen_main() {
}


void Flashlight_create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    Flashlight_create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t Flashlight_tick_screen_funcs[] = {
    Flashlight_tick_screen_main,
};

void Flashlight_tick_screen(int screen_index) {
    Flashlight_tick_screen_funcs[screen_index]();
}
