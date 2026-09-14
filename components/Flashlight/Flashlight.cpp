#include "Flashlight.hpp"
#include "Settings.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Flashlight"
#include "esp_lib_utils.h"

// EEZ Studio generates C code; pull its entry point in with C linkage.
extern "C" {
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"

    LV_IMG_DECLARE(flashlight_icon);

    static int32_t flashlight_status = 0;
    static uint32_t btn_idx_old = 0xFF;
    
    int32_t get_var_flashlight_status() {
        return flashlight_status;
    }
    void set_var_flashlight_status(int32_t value) {
        flashlight_status = value;
    }

    void action_flashlight_matrix_handler(lv_event_t *e) {
        lv_obj_t *matrix = (lv_obj_t *)lv_event_get_target(e);
        uint32_t btn_idx = lv_buttonmatrix_get_selected_button(matrix);

        bool is_on = get_var_flashlight_status() != 0;
        lv_color_t c;
        if (btn_idx_old != btn_idx) {
            if (btn_idx == 0) { // White button
                c = lv_color_hex(0xffffffff);
            } else if (btn_idx == 2) { // Red button
                c = lv_color_hex(0xffff0000);
            }
        } else {
            if (is_on) {
                c = lv_color_hex(0xff000000);
            } else {
                if (btn_idx == 0) { // White button
                    c = lv_color_hex(0xffffffff);
                } else if (btn_idx == 2) { // Red button
                    c = lv_color_hex(0xffff0000);
                }
            }
        }
        btn_idx_old = btn_idx;

        set_var_flashlight_status(is_on ? 0 : 1);
        
        lv_obj_set_style_bg_color(Flashlight_objects.flashlight_container, c, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

}

// LV_IMG_DECLARE(esp_brookesia_image_middle_app_launcher_default_112_112);



namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "Flashlight";

Flashlight *Flashlight::_instance = nullptr;

Flashlight *Flashlight::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new Flashlight(use_status_bar, use_navigation_bar);
    return _instance;
}

Flashlight::Flashlight(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &flashlight_icon,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar) {}

Flashlight::~Flashlight() { _instance = nullptr; }

bool Flashlight::run()
{
    Settings::sleep_locked = true;     // hold wake-lock while flashlight is on
    Flashlight_ui_init();
    return true;
}

bool Flashlight::back()
{
    notifyCoreClosed();
    return true;
}

bool Flashlight::pause()
{
    Settings::sleep_locked = false;    // backgrounded -> release lock
    return true;
}

bool Flashlight::resume()
{
    Settings::sleep_locked = true;     // foreground again -> re-lock
    return true;
}

bool Flashlight::close()
{
    Settings::sleep_locked = false;
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Flashlight, APP_NAME, []()
{
    return std::shared_ptr<Flashlight>(Flashlight::requestInstance(), [](Flashlight *p) {});
})

} // namespace esp_brookesia::apps
