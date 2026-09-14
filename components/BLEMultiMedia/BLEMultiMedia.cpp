#include "BLEMultiMedia.hpp"
#include "Settings.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BLEMultiMedia"
#include "esp_lib_utils.h"
#include "esp_log.h"

// EEZ Studio generates C code; pull its entry point in with C linkage.
extern "C" {
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"

    LV_IMG_DECLARE(music_icon);

    // Provided by BLEMouse component. Pushes a 16-bit HID Consumer Control
    // usage code over the existing BLE HID connection. Send 0 to release.
    void ble_mouse_send_consumer(uint16_t code);

    // HID Consumer Control usage codes (Usage Page 0x0C).
    #define HID_CC_PLAY_PAUSE   0x00CD
    #define HID_CC_NEXT_TRACK   0x00B5
    #define HID_CC_PREV_TRACK   0x00B6
    #define HID_CC_VOLUME_UP    0x00E9
    #define HID_CC_VOLUME_DOWN  0x00EA
    #define HID_CC_MUTE         0x00E2

    char label_connected[64] = "Status:\nUnknown";
    char connection_dropmenu_items[256] = "";
    static uint32_t btn_idx_old = 0xFF;

    const char *get_var_label_connected() {
        return (const char*)label_connected;
    }
    void set_var_label_connected(const char *value) {
        snprintf(label_connected, sizeof(label_connected), "Status:\n%s", value);
    }
    const char *get_var_connection_dropmenu_items() {
        return (const char*)connection_dropmenu_items;
    }
    void set_var_connection_dropmenu_items(const char *value) {}

    void action_connect_button(lv_event_t * e) {
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Connect button clicked");
    }


    void action_media_buttons(lv_event_t * e) {
        lv_obj_t *matrix = (lv_obj_t *)lv_event_get_target(e);
        uint32_t btn_idx = lv_buttonmatrix_get_selected_button(matrix);

        uint16_t code = 0;
        switch (btn_idx) {
            case 1: code = HID_CC_VOLUME_UP;   break;
            case 3: code = HID_CC_PREV_TRACK;  break;
            case 4: code = HID_CC_PLAY_PAUSE;  break;
            case 5: code = HID_CC_NEXT_TRACK;  break;
            case 7: code = HID_CC_VOLUME_DOWN; break;
            case 0: case 2: case 6: case 8: default:
                ESP_LOGI(ESP_UTILS_LOG_TAG, "Button %lu pressed", btn_idx);
                btn_idx_old = btn_idx;
                return;
        }

        // Tap-and-release: most BLE hosts treat the code press + 0 release
        // as one media-key tap.
        ble_mouse_send_consumer(code);
        ble_mouse_send_consumer(0);

        ESP_LOGI(ESP_UTILS_LOG_TAG, "Sent consumer code 0x%04X (btn %lu)", code, btn_idx);
        btn_idx_old = btn_idx;
    }
}


namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "BLEMultiMedia";

BLEMultiMedia *BLEMultiMedia::_instance = nullptr;

BLEMultiMedia *BLEMultiMedia::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new BLEMultiMedia(use_status_bar, use_navigation_bar);
    return _instance;
}

BLEMultiMedia::BLEMultiMedia(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &music_icon,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar) {}

BLEMultiMedia::~BLEMultiMedia() { _instance = nullptr; }

bool BLEMultiMedia::run() {
    Settings::sleep_locked = true;
    
    BLEMultiMedia_ui_init();   // EEZ Studio entry. Builds widgets on the active screen.
    return true;
}

bool BLEMultiMedia::back() {
    notifyCoreClosed();
    return true;
}

bool BLEMultiMedia::pause() {
    Settings::sleep_locked = false;    // backgrounded -> release lock
    return true;
}

bool BLEMultiMedia::resume() {
    Settings::sleep_locked = true;     // foreground again -> re-lock
    return true;
}

bool BLEMultiMedia::close() {
    Settings::sleep_locked = false;
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, BLEMultiMedia, APP_NAME, []()
{
    return std::shared_ptr<BLEMultiMedia>(BLEMultiMedia::requestInstance(), [](BLEMultiMedia *p) {});
})

} // namespace esp_brookesia::apps
