#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

#define SET_SUBMENU_PADDING(sub) lv_obj_set_style_pad_hor( sub, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
#define SET_SUBMENU_SEPARATOR(sub) lv_menu_separator_create(sub);
#define ADD_SUBMENU(title, submenu, icon) {\
    cont = create_text(section, icon, title, LV_MENU_ITEM_BUILDER_VARIANT_1);\
    lv_menu_set_load_page_event(menu, cont, submenu);\
}

objects_t Settings_objects;
lv_obj_t *Settings_tick_value_change_obj;
lv_obj_t *Settings_root_page;

/* static void Settings_back_event_handler(lv_event_t *e) {
	lv_obj_t *obj	 = lv_event_get_target(e);
	lv_obj_t *menu = lv_event_get_user_data(e);

	if (lv_menu_back_button_is_root(menu, obj)) {
		lv_obj_t *mbox1 = lv_msgbox_create(NULL);
		lv_msgbox_add_title(mbox1, "Hello");
		lv_msgbox_add_text(mbox1, "Root back btn click.");
		lv_msgbox_add_close_button(mbox1);
	}
} */


static lv_obj_t *create_slider( lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max, int32_t val) {
	lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

	lv_obj_t *slider = lv_slider_create(obj);
	lv_obj_set_flex_grow(slider, 1);
	lv_slider_set_range(slider, min, max);
	lv_slider_set_value(slider, val, LV_ANIM_OFF);
    lv_obj_add_event(slider, action_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

	if (icon == NULL) {
		lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
	}

	return obj;
}

static lv_obj_t *create_switch( lv_obj_t *parent, const char *icon, const char *txt, bool chk) {
	lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

	lv_obj_t *sw = lv_switch_create(obj);
	lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : 0);
    lv_obj_add_event(sw, action_button_pressed, LV_EVENT_CLICKED, NULL);

	return obj;
}

static lv_obj_t *create_text( lv_obj_t *parent, const char *icon, const char *txt, lv_menu_builder_variant_t builder_variant) {
	lv_obj_t *obj = lv_menu_cont_create(parent);

	lv_obj_t *img = NULL;
	lv_obj_t *label = NULL;

	if (icon) {
		img = lv_image_create(obj);
		lv_image_set_src(img, icon);
	}

	if (txt) {
		label = lv_label_create(obj);
		lv_label_set_text(label, txt);
		lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
		lv_obj_set_flex_grow(label, 1);
	}

	if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
		lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
		lv_obj_swap(img, label);
	}

	return obj;
}




void Settings_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    Settings_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *menu = lv_menu_create(parent_obj);
            lv_obj_set_pos(menu, 0, 40);
            lv_obj_set_size(menu, LV_PCT(100), 462);
            lv_obj_set_style_pad_left(menu, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(menu, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(menu, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(menu, &lv_font_montserrat_32, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_DEFAULT);
            lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_DISABLED);
            lv_menu_set_mode_header(menu, LV_MENU_HEADER_TOP_FIXED);
            // lv_obj_add_event_cb(menu, Settings_back_event_handler, LV_EVENT_CLICKED, menu);

            lv_obj_t *cont;
            lv_obj_t *section;

            lv_obj_t *sub_display_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_audio_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_wifi_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_bluetooth_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_espnow_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_upgrade_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_battery_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_sdcard_page = lv_menu_page_create(menu, NULL);
            lv_obj_t *sub_about_page = lv_menu_page_create(menu, NULL);


            //  Root Settings page
            {
                Settings_root_page = lv_menu_page_create(menu, "Settings");
                SET_SUBMENU_PADDING( Settings_root_page )
                section = lv_menu_section_create(Settings_root_page);
                
                ADD_SUBMENU("Display", sub_display_page, (const char*)LV_SYMBOL_SETTINGS)
                ADD_SUBMENU("Audio", sub_audio_page, (const char*)LV_SYMBOL_VOLUME_MAX)
                ADD_SUBMENU("WiFi", sub_wifi_page, (const char*)LV_SYMBOL_WIFI)
                ADD_SUBMENU("Bluetooth", sub_bluetooth_page,(const char*)LV_SYMBOL_BLUETOOTH)
                ADD_SUBMENU("ESP-Now", sub_espnow_page, (const char*)LV_SYMBOL_SETTINGS)
                ADD_SUBMENU("Upgrade", sub_upgrade_page, (const char*)LV_SYMBOL_UPLOAD)
                ADD_SUBMENU("Battery", sub_battery_page, (const char*)LV_SYMBOL_BATTERY_FULL)
                ADD_SUBMENU("SD Card", sub_sdcard_page, (const char*)LV_SYMBOL_SD_CARD)
                ADD_SUBMENU("About", sub_about_page, (const char*)LV_SYMBOL_HOME)

                lv_menu_set_sidebar_page(menu, Settings_root_page);
                lv_obj_send_event( 
                    lv_obj_get_child( 
                        lv_obj_get_child( 
                            lv_menu_get_cur_sidebar_page(menu),
                            0),
                        0), 
                LV_EVENT_CLICKED, NULL);
            }

            //  Display Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_display_page )
                SET_SUBMENU_SEPARATOR( sub_display_page )
                section = lv_menu_section_create( sub_display_page );
                create_slider(section, LV_SYMBOL_SETTINGS, "Brightness", 1, 100, 85);
                create_switch(section, LV_SYMBOL_EYE_CLOSE, "Dim screen on light sleep", get_dim_screen_value());
                create_switch(section, LV_SYMBOL_POWER, "User button wake-up", get_user_wake_up_button_value());
                create_switch(section, LV_SYMBOL_POWER, "Disable sleep when charging", get_sleep_charge_button_value());
                create_slider(section, LV_SYMBOL_SETTINGS, "Brightness (Light Sleep)", 1, 255, 80);
                create_slider(section, LV_SYMBOL_SETTINGS, "Time before light sleep", 5, 30, 15);
                create_slider(section, LV_SYMBOL_SETTINGS, "Time before deep sleep", 10, 60, 30);
            }


            //  Audio Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_audio_page )
                SET_SUBMENU_SEPARATOR( sub_audio_page )
                section = lv_menu_section_create( sub_audio_page );
                create_switch(section, LV_SYMBOL_BELL, "Vibrations", get_vibrations_value());
            }


            //  WiFi Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_wifi_page )
                SET_SUBMENU_SEPARATOR( sub_wifi_page )
                section = lv_menu_section_create( sub_wifi_page );
                create_switch(section, LV_SYMBOL_WIFI, "Enable WiFi", get_wifi_value());
            }

            //  Bluetooth Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_bluetooth_page )
                SET_SUBMENU_SEPARATOR( sub_bluetooth_page )
                section = lv_menu_section_create( sub_bluetooth_page );
                create_switch(section, LV_SYMBOL_BLUETOOTH, "Enable Bluetooth", get_bluetooth_value());
            }

            //  ESP-Now Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_espnow_page )
                SET_SUBMENU_SEPARATOR( sub_espnow_page )
                section = lv_menu_section_create( sub_espnow_page );
                create_switch(section, LV_SYMBOL_WIFI, "Enable ESP-Now", get_espnow_value());
            }

            //  Upgrade Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_upgrade_page )
                SET_SUBMENU_SEPARATOR( sub_upgrade_page )
                section = lv_menu_section_create( sub_upgrade_page );
                create_text(section, LV_SYMBOL_FILE, "Upgrades", LV_MENU_ITEM_BUILDER_VARIANT_1);
                {
                    lv_obj_t *button = lv_btn_create(section);
                    lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_add_event_cb(button, action_button_pressed, LV_EVENT_CLICKED, NULL);
                    lv_obj_set_user_data(button, (void *)BUTTON_CHECK_UPDATES);
                    {
                        lv_obj_t *obj = lv_label_create(button);
                        lv_obj_set_pos(obj, -1, 0);
                        lv_label_set_text(obj, "Check for updates");
                        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                        lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    }
                }
            }

            //  Battery Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_battery_page )
                SET_SUBMENU_SEPARATOR( sub_battery_page )
                section = lv_menu_section_create( sub_battery_page );
                create_text(section, LV_SYMBOL_BATTERY_FULL, "Battery", LV_MENU_ITEM_BUILDER_VARIANT_1);
            }

            //  SD Card Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_sdcard_page )
                SET_SUBMENU_SEPARATOR( sub_sdcard_page )
                section = lv_menu_section_create( sub_sdcard_page );
                create_text(section, LV_SYMBOL_SD_CARD, "SD Card", LV_MENU_ITEM_BUILDER_VARIANT_1);
            }

            //  About Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_about_page )
                SET_SUBMENU_SEPARATOR( sub_about_page )
                section = lv_menu_section_create( sub_about_page );
                create_text(section, NULL, "About", LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, NULL, get_version_value("Current: ", NULL), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, NULL, "Created by SlimyRedstone", LV_MENU_ITEM_BUILDER_VARIANT_1);
                {
                    lv_obj_t *button = lv_btn_create(section);
                    lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_add_event_cb(button, action_button_pressed, LV_EVENT_CLICKED, NULL);
                    lv_obj_set_user_data(button, (void *)BUTTON_TEST);
                    {
                        // record_button_label
                        lv_obj_t *obj = lv_label_create(button);
                        lv_obj_set_pos(obj, -1, 0);
                        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                        lv_label_set_text(obj, "Test");
                        lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    }
                }
            }
        }
    }
}

void Settings_tick_screen_main() {
}


void Settings_create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    Settings_create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t Settings_tick_screen_funcs[] = {
    Settings_tick_screen_main,
};

void Settings_tick_screen(int screen_index) {
    Settings_tick_screen_funcs[screen_index]();
}
