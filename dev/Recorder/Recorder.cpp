#include "Recorder.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Recorder"
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
    #include "actions.h"

    LV_IMG_DECLARE(recorder_icon);
    

    char record_time_text[64] = "Duration: 00:00:00";
    char record_button_text[16] = "Record";
    char play_button_text[16] = "Play";
    char playback_time_text[64] = "Duration: 00:00:00";
    char sd_card_size_text[64] = "SD Card Size: 0Gb/0Gb";
    int32_t recording_led_brightness = 0;

    enum RECORDER_STATUS {
        STATUS_IDLE = 0,
        STATUS_RECORDING_STARTED,
        STATUS_RECORDING_STOPPED,
        STATUS_PLAYING_STARTED,
        STATUS_PLAYING_STOPPED,
    };

    RECORDER_STATUS record_status = STATUS_RECORDING_STOPPED;
    RECORDER_STATUS playback_status = STATUS_PLAYING_STOPPED;

    void action_play_button_pressed(lv_event_t * e) {
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_CLICKED) {
            ESP_LOGI(ESP_UTILS_LOG_TAG,"Play button clicked");
            switch (playback_status) {
                case STATUS_PLAYING_STOPPED:
                    playback_status = STATUS_PLAYING_STARTED;
                    set_var_play_button_text("Stop");
                    break;
                case STATUS_PLAYING_STARTED:
                    playback_status = STATUS_PLAYING_STOPPED;
                    set_var_play_button_text("Start");
                    break;
            
                default:
                    playback_status = STATUS_IDLE;
                    set_var_play_button_text("Unknown");
                    break;
            }
        }
    }
    void action_record_button_pressed(lv_event_t * e) {
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_CLICKED) {
            ESP_LOGI(ESP_UTILS_LOG_TAG,"Record button clicked");
            switch (record_status) {
                case STATUS_RECORDING_STOPPED:
                    record_status = STATUS_RECORDING_STARTED;
                    set_var_record_button_text("Stop");
                    break;
                case STATUS_RECORDING_STARTED:
                    record_status = STATUS_RECORDING_STOPPED;
                    set_var_record_button_text("Start");
                    break;
            
                default:
                    record_status = STATUS_IDLE;
                    set_var_record_button_text("Unknown");
                    break;
            }
            set_var_recording_led_brightness(record_status == STATUS_RECORDING_STARTED ? 255 : 0);
        }
    }
    void action_volume_slider_changed(lv_event_t * e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        int32_t volume_precent = lv_slider_get_value(slider);
        ESP_LOGI(ESP_UTILS_LOG_TAG,"Slider changed: %ld", volume_precent);
    }
    void action_file_list_selected(lv_event_t * e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        if(code == LV_EVENT_CLICKED) {
            ESP_LOGI(ESP_UTILS_LOG_TAG,"Selected: %s", lv_list_get_button_text(Recorder_objects.record_files_list, obj));
        }
    }
    
    
    int32_t get_var_recording_led_brightness() {
        return recording_led_brightness;
    }
    void set_var_recording_led_brightness(int32_t value) {
        recording_led_brightness = value;
        lv_led_set_brightness(Recorder_objects.record_led, recording_led_brightness);
    }
    const char *get_var_sd_card_size_text() {
        return (const char *)sd_card_size_text;
    }
    void set_var_sd_card_size_text(const char *value) {
        strncpy(sd_card_size_text, value, sizeof(sd_card_size_text));
        lv_label_set_text(Recorder_objects.sd_card_size_label, sd_card_size_text);
        lv_label_set_text(Recorder_objects.sd_card_size_label2 , record_time_text);
    }
    const char *get_var_record_time_text() {
        return (const char *)record_time_text;
    }
    void set_var_record_time_text(const char *value) {
        strncpy(record_time_text, value, sizeof(record_time_text));
        lv_label_set_text(Recorder_objects.record_time_label, record_time_text);
    }
    const char *get_var_record_button_text() {
        return (const char *)record_button_text;
    }
    void set_var_record_button_text(const char *value) {
        strncpy(record_button_text, value, sizeof(record_button_text));
        lv_label_set_text(Recorder_objects.record_button_label, record_button_text);
    }
    const char *get_var_play_button_text() {
        return (const char *)play_button_text;
    }
    void set_var_play_button_text(const char *value) {
        strncpy(play_button_text, value, sizeof(play_button_text));
        lv_label_set_text(Recorder_objects.play_button_label, play_button_text);
    }
    const char *get_var_playback_time_text() {
        return (const char *)playback_time_text;
    }
    void set_var_playback_time_text(const char *value) {
        strncpy(playback_time_text, value, sizeof(playback_time_text));
        lv_label_set_text(Recorder_objects.playback_time_label, playback_time_text);
    }
}

namespace esp_brookesia::apps {

    static constexpr char APP_NAME[] = "Recorder";

    Recorder *Recorder::_instance = nullptr;

    Recorder *Recorder::requestInstance(bool use_status_bar, bool use_navigation_bar) {
        if (_instance == nullptr) _instance = new Recorder(use_status_bar, use_navigation_bar);
        return _instance;
    }

    Recorder::Recorder(bool use_status_bar, bool use_navigation_bar)
        : App(APP_NAME,
            &recorder_icon,
            /*use_default_screen=*/true,
            use_status_bar,
            use_navigation_bar) {}

    Recorder::~Recorder() { _instance = nullptr; }

    bool Recorder::run() {
        Recorder_ui_init();   // EEZ Studio entry. Builds widgets on the active screen.
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Recorder app started");
        set_var_recording_led_brightness(0);
        set_var_record_button_text("Start");
        set_var_play_button_text("Play");
        set_var_playback_time_text("Duration: UNK");
        set_var_record_time_text("Duration: UNK");
        set_var_sd_card_size_text("SD Card Size: UNK/UNK");
        return true;
    }

    bool Recorder::back() {
        notifyCoreClosed();
        return true;
    }

    bool Recorder::close() {
        return true;
    }

    ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Recorder, APP_NAME, []()
    {
        return std::shared_ptr<Recorder>(Recorder::requestInstance(), [](Recorder *p) {});
    })

} // namespace esp_brookesia::apps
