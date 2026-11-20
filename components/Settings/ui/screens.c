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
#define USE_ICONS_SUBMENU_LABELS

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
        // lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
		lv_obj_set_flex_grow(label, 1);
	}

	if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
		lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
		lv_obj_swap(img, label);
	}

	return obj;
}

#define H_SEPARATOR_WIDTH_PCT   75
#define H_SEPARATOR_LINE_W      1
static lv_obj_t *create_h_separator(lv_obj_t *parent ) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);                               // no bg, border, padding
    lv_obj_set_size(row, LV_PCT(100), H_SEPARATOR_LINE_W);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *obj = lv_line_create(row);
    lv_obj_set_size(obj, LV_PCT(H_SEPARATOR_WIDTH_PCT), H_SEPARATOR_LINE_W);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 0);

    // End point in % of the line object's width: follows the parent width
    static lv_point_precise_t Settings_line_points[] = {
        { 0, 0 },
        { LV_PCT(100), 0 }
    };
    lv_line_set_points(obj, Settings_line_points, 2);
    lv_obj_set_style_line_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(obj, H_SEPARATOR_LINE_W, LV_PART_MAIN | LV_STATE_DEFAULT);
    return obj;
}

// Menu row holding one clickable button. event_cb gets LV_EVENT_CLICKED, the
// button's user data is user_id (e.g. a buttons_t value), so one callback can
// serve several buttons. Returns the row, like create_slider() / create_switch().
static lv_obj_t *create_button( lv_obj_t *parent, const char *txt, lv_event_cb_t event_cb, int32_t user_id) {
	lv_obj_t *obj = lv_menu_cont_create(parent);

	lv_obj_t *button = lv_btn_create(obj);
	lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_set_user_data(button, (void *)(intptr_t)user_id);
	{
		lv_obj_t *label = lv_label_create(button);
		// x must stay 0: LVGL leaves a centred child with x != 0 out of its
		// parent's LV_SIZE_CONTENT width, and the button shrank to its padding.
		lv_label_set_text(label, txt);
		lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_style_align(label, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
		// font: inherited, the section text size (menu font, 18)
	}

	return obj;
}




void Settings_create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    Settings_objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 410, 502);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    // Fonts: LV_PART_MAIN only. Parts are values, not flags (LV_PART_MAIN = 0): "LV_PART_MAIN | LV_PART_ITEMS" is
    // LV_PART_ITEMS, which labels never inherit from, so those fonts never reached the menu text.
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *menu = lv_menu_create(parent_obj);
            lv_obj_set_pos(menu, 0, 40);
            lv_obj_set_size(menu, LV_PCT(100), 462);
            lv_obj_set_style_pad_left(menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(menu, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(menu, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(menu, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);   // section text
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
            lv_obj_t *sub_dev_page = lv_menu_page_create(menu, NULL);


            //  Root Settings page
            {
                Settings_root_page = lv_menu_page_create(menu, "Settings");
                SET_SUBMENU_PADDING( Settings_root_page )
                section = lv_menu_section_create(Settings_root_page);
                #ifdef USE_ICONS_SUBMENU_LABELS
                    ADD_SUBMENU("Display", sub_display_page, (const char*)LV_SYMBOL_SETTINGS)
                    ADD_SUBMENU("Audio", sub_audio_page, (const char*)LV_SYMBOL_VOLUME_MAX)
                    ADD_SUBMENU("WiFi", sub_wifi_page, (const char*)LV_SYMBOL_WIFI)
                    ADD_SUBMENU("Bluetooth", sub_bluetooth_page,(const char*)LV_SYMBOL_BLUETOOTH)
                    ADD_SUBMENU("ESP-Now", sub_espnow_page, (const char*)LV_SYMBOL_SETTINGS)
                    ADD_SUBMENU("Upgrade", sub_upgrade_page, (const char*)LV_SYMBOL_UPLOAD)
                    ADD_SUBMENU("Battery", sub_battery_page, (const char*)LV_SYMBOL_BATTERY_FULL)
                    ADD_SUBMENU("SD Card", sub_sdcard_page, (const char*)LV_SYMBOL_SD_CARD)
                    ADD_SUBMENU("About", sub_about_page, (const char*)LV_SYMBOL_HOME)
                    ADD_SUBMENU("Developer", sub_dev_page, (const char*)LV_SYMBOL_USB)
                #else
                    ADD_SUBMENU("Display", sub_display_page, NULL)
                    ADD_SUBMENU("Audio", sub_audio_page, NULL)
                    ADD_SUBMENU("WiFi", sub_wifi_page, NULL)
                    ADD_SUBMENU("Bluetooth", sub_bluetooth_page, NULL)
                    ADD_SUBMENU("ESP-Now", sub_espnow_page, NULL)
                    ADD_SUBMENU("Upgrade", sub_upgrade_page, NULL)
                    ADD_SUBMENU("Battery", sub_battery_page, NULL)
                    ADD_SUBMENU("SD Card", sub_sdcard_page, NULL)
                    ADD_SUBMENU("About", sub_about_page, NULL)
                    ADD_SUBMENU("Developer", sub_dev_page, NULL)
                #endif
                lv_menu_set_sidebar_page(menu, Settings_root_page);
                // Sidebar: categories and "Settings" title 16 (the header is not a child of the page)
                lv_obj_set_style_text_font(Settings_root_page, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_text_font(lv_menu_get_sidebar_header(menu), &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                // Sidebar width (LVGL default 30% = 123 px): "Developer" at 16 px + icon + paddings = 155 px
                lv_obj_set_width(lv_obj_get_parent(Settings_root_page), LV_PCT(32));
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
                create_slider(section, LV_SYMBOL_AUDIO, "Volume", 0, 100, 30);
            }


            //  WiFi Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_wifi_page )
                SET_SUBMENU_SEPARATOR( sub_wifi_page )
                section = lv_menu_section_create( sub_wifi_page );
                create_switch(section, LV_SYMBOL_WIFI, "Enable WiFi", get_wifi_value());
                create_switch(section, LV_SYMBOL_WIFI, "Disable WiFi on boot", get_wifi_disable_on_boot_value());
                create_text(section, LV_SYMBOL_BULLET, get_wifi_ip_address(), LV_MENU_ITEM_BUILDER_VARIANT_1);
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
                create_button(section, "Check for updates", action_button_pressed, BUTTON_CHECK_UPDATES);
            }

            //  Battery Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_battery_page )
                SET_SUBMENU_SEPARATOR( sub_battery_page )
                section = lv_menu_section_create( sub_battery_page );
                create_text(section, LV_SYMBOL_BATTERY_FULL, "Battery", LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_switch(section, LV_SYMBOL_IMAGE, "Display percentage", get_battery_perc_text_value());
                
                create_h_separator(section);

                create_text(section, LV_SYMBOL_BULLET, get_battery_charge_status_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, LV_SYMBOL_BULLET, get_battery_usb_status_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, LV_SYMBOL_BULLET, get_battery_voltage_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, LV_SYMBOL_BULLET, get_battery_ac_voltage_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, LV_SYMBOL_BULLET, get_battery_estimated_life_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_text(section, LV_SYMBOL_BULLET, get_battery_estimated_sleep_life_label(), LV_MENU_ITEM_BUILDER_VARIANT_1);
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
                // create_button(section, "Test", action_button_pressed, BUTTON_TEST);
            }
            //  Developer Sub-Menu
            {
                SET_SUBMENU_PADDING( sub_dev_page )
                SET_SUBMENU_SEPARATOR( sub_dev_page )
                section = lv_menu_section_create( sub_dev_page );
                create_text(section, LV_SYMBOL_USB, "Developer Options", LV_MENU_ITEM_BUILDER_VARIANT_1);
                create_switch(section, LV_SYMBOL_WARNING, "Enable Performance counter", get_developer_performance_value());
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
