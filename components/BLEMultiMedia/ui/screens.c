#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t BLEMultiMedia_objects;
lv_obj_t *BLEMultiMedia_tick_value_change_obj;

void BLEMultiMedia_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    BLEMultiMedia_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // connection_container
            lv_obj_t *obj = lv_obj_create(parent_obj);
            BLEMultiMedia_objects.connection_container = obj;
            lv_obj_set_pos(obj, 0, 46);
            lv_obj_set_size(obj, 410, 410);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_CLICK_FOCUSABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // connection_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    BLEMultiMedia_objects.connection_label = obj;
                    lv_obj_set_pos(obj, 115, 70);
                    lv_obj_set_size(obj, 181, LV_SIZE_CONTENT);
                    lv_label_set_text(obj, "");
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // connection_dropmenu
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    BLEMultiMedia_objects.connection_dropmenu = obj;
                    lv_obj_set_pos(obj, 45, 205);
                    lv_obj_set_size(obj, 321, LV_SIZE_CONTENT);
                    lv_dropdown_set_options(obj, "");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                }
                {
                    // connection_button
                    lv_obj_t *obj = lv_btn_create(parent_obj);
                    BLEMultiMedia_objects.connection_button = obj;
                    lv_obj_set_pos(obj, 115, 284);
                    lv_obj_set_size(obj, 181, 50);
                    lv_obj_add_event_cb(obj, action_connect_button, LV_EVENT_PRESSED, (void *)0);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // connection_button_label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            BLEMultiMedia_objects.connection_button_label = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Connect");
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
            }
        }
        {
            // MediaButtons
            lv_obj_t *obj = lv_buttonmatrix_create(parent_obj);
            BLEMultiMedia_objects.media_buttons = obj;
            lv_obj_set_pos(obj, 0, 46);
            lv_obj_set_size(obj, 410, 410);
            static const char *map[12] = {
                "Btn",
                "Volume\nUp",
                "Btn",
                "\n",
                "Previous",
                "Play\nPause",
                "Next",
                "\n",
                "Btn",
                "Volume\nDown",
                "Btn",
                NULL,
            };
            static lv_buttonmatrix_ctrl_t BLEMultiMedia_ctrl_map[9] = {
                1 | LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_DISABLED,
                1,
                1 | LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_DISABLED,
                1,
                1,
                1,
                1 | LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_DISABLED,
                1,
                1 | LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_DISABLED,
            };
            lv_buttonmatrix_set_map(obj, map);
            lv_buttonmatrix_set_ctrl_map(obj, BLEMultiMedia_ctrl_map);
            lv_obj_add_event_cb(obj, action_media_buttons, LV_EVENT_VALUE_CHANGED, (void *)0);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff244c60), LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff7c2626), LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_tiled(obj, false, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 16, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_blend_mode(obj, LV_BLEND_MODE_ADDITIVE, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(obj, 5, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_color(obj, lv_color_hex(0xffffffff), LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_x(obj, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_y(obj, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_opa(obj, 120, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_spread(obj, 0, LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(obj, 10, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_ofs_x(obj, 2, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_ofs_y(obj, 2, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_spread(obj, 1, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_color(obj, lv_color_hex(0xffffffff), LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_shadow_opa(obj, 180, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

void BLEMultiMedia_tick_screen_main() {
    {
        const char *new_val = get_var_label_connected();
        const char *cur_val = lv_label_get_text(BLEMultiMedia_objects.connection_label);
        if (strcmp(new_val, cur_val) != 0) {
            BLEMultiMedia_tick_value_change_obj = BLEMultiMedia_objects.connection_label;
            lv_label_set_text(BLEMultiMedia_objects.connection_label, new_val);
            BLEMultiMedia_tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_connection_dropmenu_items();
        const char *cur_val = lv_dropdown_get_options(BLEMultiMedia_objects.connection_dropmenu);
        if (strcmp(new_val, cur_val) != 0) {
            BLEMultiMedia_tick_value_change_obj = BLEMultiMedia_objects.connection_dropmenu;
            lv_dropdown_set_options(BLEMultiMedia_objects.connection_dropmenu, new_val);
            BLEMultiMedia_tick_value_change_obj = NULL;
        }
    }
}


void BLEMultiMedia_create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    BLEMultiMedia_create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t BLEMultiMedia_tick_screen_funcs[] = {
    BLEMultiMedia_tick_screen_main,
};

void BLEMultiMedia_tick_screen(int screen_index) {
    BLEMultiMedia_tick_screen_funcs[screen_index]();
}
