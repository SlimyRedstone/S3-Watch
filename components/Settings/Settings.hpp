/*
 * Settings app.
 *
 * Three jobs:
 *   1. Persistent configuration (AppConfig), loaded from / saved to the SD
 *      card as JSON. Other apps read Settings::cfg directly.
 *   2. Background service: feed the Brookesia status-bar battery icon from
 *      the AXP2101 PMU, drive the vibration motor on UI touches, keep the
 *      Bluetooth status-bar icon in sync.
 *   3. Foreground UI: the EEZ-generated menu in ui/ (never edited by hand).
 *      The C getters/setters it calls live in Settings.cpp and are plain
 *      views over AppConfig; the slider/switch handlers apply the value to
 *      the hardware and schedule a save.
 *
 * JSON layout (/sdcard/.settings.json):
 *   { "settings": {
 *       "display":   { "brightness", "dim_screen", "user_button_wake_up",
 *                      "disable_sleep_on_charge", "light_sleep_brightness",
 *                      "light_sleep_delay_s", "deep_sleep_delay_s" },
 *       "audio":     { "vibrations", "vibration_duration", "volume" },
 *       "bluetooth": { "enabled" },
 *       "wifi":      { "enabled" },
 *       "espnow":    { "enabled" },
 *       "battery":   { "show_percentage" },
 *       "upgrade":   { "enabled", "src_url" },
 *       "developer": { "performance_counter" } } }
 */
#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

// Firmware update source, used when settings.upgrade.src_url is missing.
#define SETTINGS_DEFAULT_UPDATE_URL "https://api.slimyredstone.net/code/s3-watch/version"

namespace esp_brookesia::apps {

class Settings : public systems::phone::App {
public:
    static constexpr uint8_t  PMU_I2C_ADDR     = 0x34;
    static constexpr uint32_t STATUS_PERIOD_MS = 1500; // status bar update
    static constexpr uint32_t SAVE_DEBOUNCE_MS = 1000;     // slider drag -> one SD write

    // Vibration motor: GPIO18, active high, through a transistor (schematic
    // "Motor" block; same pin the Waveshare factory firmware uses).
    static constexpr gpio_num_t MOTOR_GPIO       = GPIO_NUM_18;
    static constexpr uint32_t   MOTOR_MAX_ON_MS  = 1000;   // hard cap, whatever the config says

    // ---- Persistent app config (read by other apps; saved to SD) ----
    // Defaults equal the initial values of the generated UI widgets.
    struct AppConfig {
        struct Display {
            int  brightness               = 85;    // 1..100 %  (slider "Brightness")
            bool dim_screen               = false; // light sleep keeps the panel on at light_sleep_brightness
            bool user_button_wake_up      = false; // GPIO0 wakes from light/deep sleep
            bool disable_sleep_on_charge  = false; // no light/deep sleep while VBUS is present
            int  light_sleep_brightness   = 80;    // 1..255 (slider scale; panel takes %, see lightSleepBrightnessPercent())
            int  light_sleep_delay_s      = 15;    // 5..30 s of inactivity
            int  deep_sleep_delay_s       = 30;    // 10..60 s of inactivity (must exceed light_sleep_delay_s)
        } display;
        struct Audio {
            bool vibrations               = false; // haptic tick on UI elements
            int  vibration_duration_ms    = 100;   // length of one tick
            int  volume                   = 30;    // 0..100 speaker volume
        } audio;
        struct Bluetooth { bool enabled = false; } bluetooth;
        struct Wifi      { bool enabled = false; } wifi;
        struct Espnow    { bool enabled = false; } espnow;
        struct Battery   { bool show_percentage = false; } battery;     // "Display percentage" switch
        struct Upgrade {
            bool enabled = false;                                       // UI only, no switch in the menu yet
            char src_url[192] = SETTINGS_DEFAULT_UPDATE_URL;            // version document, see fetchLatestVersion()
        } upgrade;
        struct Developer { bool performance_counter = false; } developer;
    };
    static AppConfig cfg;                              // live values
    static constexpr const char *CFG_PATH = "/sdcard/.settings.json";

    // Mount the SD card and load cfg once. Idempotent, safe from any app's
    // init(): the registry order of app inits is not defined, so an app that
    // needs a setting at init time (BLEMouse) calls this first.
    static bool ensureLoaded();
    static bool loadFromSd();                          // overwrites cfg on success
    static bool saveToSd();
    static void saveLater();                           // debounced saveToSd() (LVGL task only)

    // ---- Apply helpers (hardware side of each setting) ----
    // Panel brightness in percent. Takes the LVGL lock itself (the brightness
    // command shares the QSPI handle with the LVGL flush), so it is callable
    // from any task. Clamped to 0..100.
    static void setPanelBrightness(int percent);
    static void applyBrightness();                     // = setPanelBrightness(cfg.display.brightness)
    static int  lightSleepBrightnessPercent();         // cfg.display.light_sleep_brightness (1..255) -> 1..100

    // Speaker volume -> codec, if the codec has been brought up (Recorder
    // does that lazily). No-op otherwise; the codec init reads cfg itself.
    static void applyVolume();
    static void setVolume(int volume);                 // cfg + apply + saveLater()

    // One vibration tick. ms == 0 -> cfg.audio.vibration_duration_ms. Does
    // nothing while cfg.audio.vibrations is false. Non-blocking (esp_timer
    // turns the motor off). Callable from any task.
    static void vibrate(uint32_t ms = 0);

    // Bluetooth on/off is owned by the BLE app (BLEMouse). It registers a
    // handler at init; the Settings switch calls it. Settings cannot depend
    // on BLEMouse at link time (BLEMouse already depends on Settings).
    // The handler returns false if the requested state could not be reached
    // (e.g. not enough internal RAM to start the controller).
    using BluetoothHandler = bool (*)(bool enable);
    static void setBluetoothHandler(BluetoothHandler handler);
    static bool applyBluetooth();                      // handler(cfg.bluetooth.enabled)
    static volatile bool bluetooth_running;            // set by the BLE app while the stack is up

    // LVGL performance monitor (FPS / CPU / render+flush time, bottom right)
    // shown or hidden per cfg.developer.performance_counter. LVGL creates it
    // at display creation (CONFIG_LV_USE_PERF_MONITOR); hidden, its refresh
    // timer is paused too. Needs the LVGL lock (LVGL task or LvLockGuard).
    static void applyPerformanceCounter();

    // ---- Status bar (top-right icon area + clock) ----
    // Wi-Fi icon only while Wi-Fi or ESP-NOW is enabled: connected -> 3 bars,
    // else the "Wi-Fi off" glyph. Called on the switches and by the status timer.
    static void refreshWifiIcon();
    // Battery percentage text next to the battery icon (cfg.battery.show_percentage).
    static void applyBatteryPercentage();
    // Moves the clock left once more than STATUS_ICONS_BEFORE_SHIFT icons are
    // shown (by the width of the extra ones), and whenever the icons would
    // reach it. Runs on every layout change of the icon or clock area.
    static void layoutStatusBar();
    static constexpr int     STATUS_ICONS_BEFORE_SHIFT = 3;
    static constexpr uint8_t STATUS_CLOCK_AREA         = 1;   // main/dark/status_bar_data.hpp
    static constexpr uint8_t STATUS_ICON_AREA          = 2;

    // ---- Firmware version / update (SettingsUpgrade.cpp) ----
    struct FirmwareVersion {
        uint16_t major_num;
        uint16_t minor_num;
        uint16_t patch_num;
    };
    static constexpr FirmwareVersion FW_VERSION = {0, 5, 0};    // installed firmware: bump on every release
    static FirmwareVersion getVersion() { return FW_VERSION; }
    static const char *getVersionText();                        // "v0.5.0"

    // Latest release, as published at cfg.upgrade.src_url:
    //   {"last":{"t":"v1.2.0","n":[1,2,0]}, "url":"https://.../v1.2.0.bin"}
    struct UpdateInfo {
        FirmwareVersion version;                                // from "n" (or parsed from "t")
        char tag[32];                                           // "t", e.g. "v1.2.0"
        char url[256];                                          // firmware image
    };
    // Blocking HTTP(S) GET + parse. Do not call from the LVGL task. On failure
    // returns false with a user-readable reason in err.
    static bool fetchLatestVersion(UpdateInfo &out, char *err, size_t err_len);
    // "Check for updates" button: Wi-Fi check, then version check and download
    // to the SD card (/sdcard/.temp/) on a worker thread, with message boxes.
    static void checkForUpdates();
    // Station connected with an IP. False while Wi-Fi was never started.
    static bool isWifiConnected();
    // SD card mounted at boot (settings file, firmware downloads).
    static bool isSdMounted();

    // VBUS present (USB plugged). False if the PMU is not reachable.
    static bool isCharging();

    // Wake-lock. Any app may set this true to prevent the watch from
    // entering light/deep sleep while it is in use (e.g. flashlight).
    // Polled by ClockApp's sleep monitor.
    static volatile bool sleep_locked;

    // Bluetooth status-bar icon (shown next to the SD-card icon while a BLE
    // central is connected). Called by BLEMouse from its connection callback.
    // Caller must hold the LVGL lock. Safe to call before the status bar
    // exists: the state is remembered and applied by the status timer.
    static void setBluetoothConnected(bool connected);

    // esp_timer callback that stops the motor (public: registered by a
    // file-scope helper in Settings.cpp).
    static void onMotorOff(void *arg);

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
    static void onSaveTimer(lv_timer_t *t);
    static void onIndevPressed(lv_event_t *e);
    static void onStatusBarLayout(lv_event_t *e);
    static systems::phone::StatusBar *statusBar();
    static void syncSlidersFromCfg(lv_obj_t *root);

    static Settings *_instance;

    // Bluetooth icon state (see setBluetoothConnected()).
    static bool _bt_connected;
    static bool _bt_icon_shown;
    static void syncBluetoothIcon();
    static BluetoothHandler _bt_handler;

    // Background status-bar update timer (always alive after init()).
    lv_timer_t *_status_timer = nullptr;
    static lv_timer_t *_save_timer;   // debounced save, one-shot
};

} // namespace esp_brookesia::apps
