/*
 * Settings app.
 *
 * Two jobs:
 *   1. Background service: feed the Brookesia status-bar battery icon with
 *      real values read from the AXP2101 PMU. Started in init() so it runs
 *      regardless of which app is foreground.
 *   2. Foreground UI: small page showing current battery %, voltage and
 *      charging state.
 */
#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/i2c_master.h"

namespace esp_brookesia::apps {

class Settings : public systems::phone::App {
public:
    static constexpr uint8_t  PMU_I2C_ADDR    = 0x34;
    static constexpr uint32_t STATUS_PERIOD_MS = 5 * 1000; // status bar update

    // ---- Persistent app config (read by other apps; saved to SD) ----
    struct AppConfig {
        uint32_t inactivity_delay_ms = 30000;   // light-sleep idle threshold (ms)
        bool dim_screen = false;
        bool enable_user_button_wake_up = false;
        bool disable_sleep_on_charge = false;
        bool enable_vibrations = false;
        bool enable_wifi = false;
        bool enable_bluetooth = false;
        bool enable_espnow = false;
    };
    static AppConfig cfg;                              // live values
    static constexpr const char *CFG_PATH = "/sdcard/.settings.json";
    static bool loadFromSd();                          // overwrites cfg on success
    static bool saveToSd();

    // Wake-lock. Any app may set this true to prevent the watch from
    // entering light/deep sleep while it is in use (e.g. flashlight).
    // Polled by ClockApp's sleep monitor.
    static volatile bool sleep_locked;

    static Settings *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Settings();

    bool run()  override;
    bool back() override;
    bool init() override;
    bool deinit() override;

protected:
    Settings(bool use_status_bar, bool use_navigation_bar);

private:
    static void onStatusTimer(lv_timer_t *t);
    static void onUiTimer(lv_timer_t *t);
    void refreshUi();

    static Settings *_instance;

    // UI labels
    lv_obj_t   *_lbl_title  = nullptr;
    lv_obj_t   *_lbl_pct    = nullptr;
    lv_obj_t   *_lbl_volt   = nullptr;
    lv_obj_t   *_lbl_charge = nullptr;
    lv_obj_t   *_lbl_delay  = nullptr;   // shows "Inactivity delay: NN s"
    lv_timer_t *_ui_timer   = nullptr;   // 1s while app foreground

    // Background status-bar update timer (always alive after init()).
    lv_timer_t *_status_timer = nullptr;

    static void onDelayMinusClicked(lv_event_t *e);
    static void onDelayPlusClicked(lv_event_t *e);
};

} // namespace esp_brookesia::apps
