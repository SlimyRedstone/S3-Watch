#include "RemoteNow.hpp"

#ifdef ESP_UTILS_LOG_TAG
	#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "RemoteNow"
#include "esp_lib_utils.h"

// EEZ Studio generates C code; pull its entry point in with C linkage.
extern "C" {
    #include "fonts.h"
    #include "images.h"
    #include "screens.h"
    #include "structs.h"
    #include "styles.h"
    #include "ui.h"
    #include "vars.h"

    char arming_state_text[32] = { 0 };
    bool arming_state = false;


    // RemoteNow_objects.arming_button

    void action_arming_button(lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        // lv_obj_t *sw    = (lv_obj_t *)lv_event_get_target(e);
        // arming_state = lv_obj_get_state(sw) == LV_STATE_CHECKED;
        
        // lv_obj_get_state()

        arming_state = !arming_state;
        
        lv_obj_set_state(RemoteNow_objects.matrix, LV_STATE_DISABLED, (!arming_state));
        if (arming_state) {
            set_var_arming_state_text("ARMED");
        } else {
            set_var_arming_state_text("DISARMED");
        }
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Arming status: %s", arming_state_text);


    }
    void action_matrix_button(lv_event_t * e) {
        
        lv_obj_t *matrix = (lv_obj_t *)lv_event_get_target(e);
        uint32_t btn_idx = lv_buttonmatrix_get_selected_button(matrix);

        uint16_t code = 0;
        switch (btn_idx) {
            case 0: code = (arming_state << 15 | 1);   break;
            case 1: code = (arming_state << 15 | 2);  break;
            case 2: code = (arming_state << 15 | 3);  break;
            case 3: code = (arming_state << 15 | 4);  break;
            default:
                return;
        }
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Button %lu pressed", btn_idx);
    }
    const char *get_var_arming_state_text() {
        return arming_state_text;
    }

    void set_var_arming_state_text(const char *value) {
        strncpy(arming_state_text, value, sizeof(arming_state_text) / sizeof(char));
        arming_state_text[sizeof(arming_state_text) / sizeof(char) - 1] = 0;
        if (RemoteNow_objects.arming_button_label)
            lv_label_set_text(RemoteNow_objects.arming_button_label, arming_state_text);
    }
}

LV_IMG_DECLARE(esp_brookesia_image_middle_app_launcher_default_112_112);

namespace esp_brookesia::apps {

	static constexpr char APP_NAME[] = "RemoteNow";

	RemoteNow *RemoteNow::_instance = nullptr;

	RemoteNow *RemoteNow::requestInstance(
			bool use_status_bar, bool use_navigation_bar) {
		if (_instance == nullptr)
			_instance = new RemoteNow(use_status_bar, use_navigation_bar);
		return _instance;
	}

	RemoteNow::RemoteNow(bool use_status_bar, bool use_navigation_bar)
			: App(APP_NAME, &esp_brookesia_image_middle_app_launcher_default_112_112,
						/*use_default_screen=*/true, use_status_bar, use_navigation_bar) {}

	RemoteNow::~RemoteNow() { _instance = nullptr; }

	bool RemoteNow::run() {
		RemoteNow_ui_init();	// EEZ Studio entry. Builds widgets on the active
													// screen.
		return true;
	}

	bool RemoteNow::back() {
		notifyCoreClosed();
		return true;
	}

	bool RemoteNow::close() { return true; }

	ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
			systems::base::App, RemoteNow, APP_NAME, []() {
				return std::shared_ptr<RemoteNow>(
						RemoteNow::requestInstance(), [](RemoteNow *p) {});
			})

}	 // namespace esp_brookesia::apps
