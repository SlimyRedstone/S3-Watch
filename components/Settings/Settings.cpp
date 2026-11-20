/*
 * Settings app - persistent config on SD, AXP2101 -> status-bar battery icon,
 * vibration motor, and the glue between the EEZ-generated menu (ui/) and the
 * hardware. See Settings.hpp for the JSON layout.
 */
#include "Settings.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "esp_timer.h"
#if LV_USE_PERF_MONITOR
#include "src/display/lv_display_private.h"            // perf_label / perf timer: LVGL 9.2 has no public toggle
#include "src/others/sysmon/lv_sysmon_private.h"
#endif

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Settings"
#include "esp_lib_utils.h"

// XPowersLib: select AXP2101 chip and pull in templated header.
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"


// ---------------------------------------------------------------------------
// File-scope PMU singleton + I2C device. Independent from ClockApp's PMU
// instance: each app gets its own dev handle on the shared bus, both can
// read the AXP2101 at 0x34 in parallel without conflict.
// ---------------------------------------------------------------------------
static XPowersPMU              s_pmu;
static bool                    s_pmu_inited = false;
static i2c_master_dev_handle_t s_pmu_dev    = nullptr;

// The C bridge below lives outside the namespace; pull the class in.
using esp_brookesia::apps::Settings;

// Text of the label that sits in the same menu row as `widget`. The generated
// create_slider()/create_switch() build: menu cont { [icon image], label, widget }.
static const char *row_label_text(lv_obj_t *widget)
{
    lv_obj_t *row = widget ? lv_obj_get_parent(widget) : nullptr;
    if (row == nullptr) return "";
    uint32_t n = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(row, (int32_t)i);
        if (lv_obj_check_type(c, &lv_label_class)) {
            const char *t = lv_label_get_text(c);
            return t ? t : "";
        }
    }
    return "";
}

// Row titles exactly as the generated ui/screens.c writes them. They are the
// only handle the generated code gives us: every slider shares one callback
// with no user data, and the switch callback never calls the setters.
static constexpr const char *ROW_BRIGHTNESS      = "Brightness";
static constexpr const char *ROW_DIM             = "Dim screen on light sleep";
static constexpr const char *ROW_BTN_WAKE        = "User button wake-up";
static constexpr const char *ROW_NO_SLEEP_CHARGE = "Disable sleep when charging";
static constexpr const char *ROW_LS_BRIGHTNESS   = "Brightness (Light Sleep)";
static constexpr const char *ROW_LS_DELAY        = "Time before light sleep";
static constexpr const char *ROW_DS_DELAY        = "Time before deep sleep";
static constexpr const char *ROW_VIBRATIONS      = "Vibrations";
static constexpr const char *ROW_VOLUME          = "Volume";
static constexpr const char *ROW_WIFI            = "Enable WiFi";
static constexpr const char *ROW_BLUETOOTH       = "Enable Bluetooth";
static constexpr const char *ROW_ESPNOW          = "Enable ESP-Now";
static constexpr const char *ROW_PERF_COUNTER    = "Enable Performance counter";
static constexpr const char *ROW_BATTERY_PERCENT = "Display percentage";

extern "C" {
    #include "settings_ui_bridge.h"   // prototypes missing from generated actions.h
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"
    LV_IMG_DECLARE(settings_icon);
    LV_IMG_DECLARE(sd_card_icon);
    LV_IMG_DECLARE(bluetooth_icon_2);   // 16x16, same size as the SD icon

    // EEZ-generated screens.c calls these C getters/setters. They are plain
    // views over the persistent Settings::cfg (declared inside
    // namespace esp_brookesia::apps, hence the using-declaration above).
    char version_text[64] = {0};

    bool get_dim_screen_value(void)            { return Settings::cfg.display.dim_screen; }
    void set_dim_screen_value(bool value)      { Settings::cfg.display.dim_screen = value; }

    bool get_user_wake_up_button_value(void)   { return Settings::cfg.display.user_button_wake_up; }
    void set_user_wake_up_button_value(bool v) { Settings::cfg.display.user_button_wake_up = v; }

    bool get_sleep_charge_button_value(void)   { return Settings::cfg.display.disable_sleep_on_charge; }
    void set_sleep_charge_button_value(bool v) { Settings::cfg.display.disable_sleep_on_charge = v; }

    bool get_vibrations_value(void)            { return Settings::cfg.audio.vibrations; }
    void set_vibrations_value(bool value)      { Settings::cfg.audio.vibrations = value; }

    bool get_wifi_value(void)                  { return Settings::cfg.wifi.enabled; }
    void set_wifi_value(bool value)            { Settings::cfg.wifi.enabled = value; Settings::refreshWifiIcon(); }

    bool get_bluetooth_value(void)             { return Settings::cfg.bluetooth.enabled; }
    void set_bluetooth_value(bool value)       {
        Settings::cfg.bluetooth.enabled = value;
        if (!Settings::applyBluetooth()) Settings::cfg.bluetooth.enabled = false;   // could not start: stay off
    }

    bool get_upgrade_value(void)               { return Settings::cfg.upgrade.enabled; }
    void set_upgrade_value(bool value)         { Settings::cfg.upgrade.enabled = value; }

    bool get_espnow_value(void)                { return Settings::cfg.espnow.enabled; }
    void set_espnow_value(bool value)          { Settings::cfg.espnow.enabled = value; Settings::refreshWifiIcon(); }

    bool get_battery_perc_text_value(void)     { return Settings::cfg.battery.show_percentage; }
    void set_battery_perc_text_value(bool v)   {
        Settings::cfg.battery.show_percentage = v;
        Settings::applyBatteryPercentage();
    }

    bool get_developer_performance_value(void)                { return Settings::cfg.developer.performance_counter; }
    void set_developer_performance_value(bool value)          {
        Settings::cfg.developer.performance_counter = value;
        Settings::applyPerformanceCounter();
    }



    const char *get_version_value(const char* prefix, const char *suffix) {
        // static: the generated UI keeps the returned pointer (label text),
        // a stack buffer here was a dangling pointer (-Wreturn-local-addr).
        static char txt[128] = {0};
        if (prefix == NULL) prefix = "";
        if (suffix == NULL) suffix = "";

        if (strlen(prefix) + strlen(suffix) > (sizeof(txt)/2)) return Settings::getVersionText();
        snprintf(txt, sizeof(txt), "%s%s%s", prefix, Settings::getVersionText(), suffix);
        return txt;
    }
    void set_version_value(const char * value) {
        // sizeof(value) was the pointer size (4), not the buffer size.
        strncpy(version_text, value, sizeof(version_text) - 1);
        version_text[sizeof(version_text) - 1] = 0;
    }

    // Every switch (LV_EVENT_CLICKED, fired after lv_switch has toggled its
    // CHECKED state) and the two plain buttons land here.
    void action_button_pressed(lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
        if (target == NULL) return;

        if (lv_obj_check_type(target, &lv_switch_class)) {
            const bool  on   = lv_obj_has_state(target, LV_STATE_CHECKED);
            const char *name = row_label_text(target);
            if      (strcmp(name, ROW_DIM) == 0)             set_dim_screen_value(on);
            else if (strcmp(name, ROW_BTN_WAKE) == 0)        set_user_wake_up_button_value(on);
            else if (strcmp(name, ROW_NO_SLEEP_CHARGE) == 0) set_sleep_charge_button_value(on);
            else if (strcmp(name, ROW_VIBRATIONS) == 0)      { set_vibrations_value(on); if (on) Settings::vibrate(); }
            else if (strcmp(name, ROW_WIFI) == 0)            set_wifi_value(on);
            else if (strcmp(name, ROW_BLUETOOTH) == 0) {
                set_bluetooth_value(on);
                if (on && !Settings::cfg.bluetooth.enabled) {
                    lv_obj_remove_state(target, LV_STATE_CHECKED);   // start refused: show it
                }
            }
            else if (strcmp(name, ROW_ESPNOW) == 0)          set_espnow_value(on);
            else if (strcmp(name, ROW_PERF_COUNTER) == 0)    set_developer_performance_value(on);
            else if (strcmp(name, ROW_BATTERY_PERCENT) == 0) set_battery_perc_text_value(on);
            else { ESP_UTILS_LOGW("switch '%s' not mapped", name); return; }
            ESP_UTILS_LOGI("switch '%s' -> %d", name, (int)on);
            Settings::saveLater();
            return;
        }

        // Plain buttons carry a buttons_t id in their user data.
        buttons_t type = (buttons_t)(uintptr_t)lv_obj_get_user_data(target);
        switch (type) {
            case BUTTON_CHECK_UPDATES:
                ESP_UTILS_LOGI("Check updates button pressed");
                Settings::checkForUpdates();
                break;
            case BUTTON_TEST:
                ESP_UTILS_LOGI("Test button pressed");
                Settings::vibrate();
                break;
            default:
                break;
        }
    }

    // Every slider (LV_EVENT_VALUE_CHANGED, continuous while dragging).
    void action_slider_changed(lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        if (slider == NULL) return;
        const int   v    = (int)lv_slider_get_value(slider);
        const char *name = row_label_text(slider);

        if (strcmp(name, ROW_BRIGHTNESS) == 0) {
            Settings::cfg.display.brightness = v;
            Settings::applyBrightness();                 // live
        } else if (strcmp(name, ROW_LS_BRIGHTNESS) == 0) {
            Settings::cfg.display.light_sleep_brightness = v;
        } else if (strcmp(name, ROW_LS_DELAY) == 0) {
            Settings::cfg.display.light_sleep_delay_s = v;
        } else if (strcmp(name, ROW_DS_DELAY) == 0) {
            Settings::cfg.display.deep_sleep_delay_s = v;
        } else if (strcmp(name, ROW_VOLUME) == 0) {
            Settings::cfg.audio.volume = v;
            Settings::applyVolume();                     // live
        } else {
            ESP_UTILS_LOGW("slider '%s' not mapped", name);
            return;
        }
        Settings::saveLater();
    }
}
static constexpr int SD_ICON_ID = 1001;
static constexpr int BT_ICON_ID = 1002;


static int pmu_reg_read(uint8_t /*addr*/, uint8_t reg, uint8_t *data, uint8_t len)
{
    if (s_pmu_dev == nullptr) return -1;
    return i2c_master_transmit_receive(s_pmu_dev, &reg, 1, data, len, 100) == ESP_OK ? 0 : -1;
}

static int pmu_reg_write(uint8_t /*addr*/, uint8_t reg, uint8_t *data, uint8_t len)
{
    if (s_pmu_dev == nullptr) return -1;
    uint8_t pkt[1 + 32];
    if (len > sizeof(pkt) - 1) return -1;
    pkt[0] = reg;
    if (len > 0 && data != nullptr) memcpy(&pkt[1], data, len);
    return i2c_master_transmit(s_pmu_dev, pkt, len + 1, 100) == ESP_OK ? 0 : -1;
}

static bool pmu_open()
{
    if (s_pmu_inited) return true;

    if (s_pmu_dev == nullptr) {
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (bus == nullptr) return false;
        i2c_device_config_t i2c_cfg = {};
        i2c_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        i2c_cfg.device_address  = 0x34;  // AXP2101
        i2c_cfg.scl_speed_hz    = 400000;
        if (i2c_master_bus_add_device(bus, &i2c_cfg, &s_pmu_dev) != ESP_OK) {
            s_pmu_dev = nullptr;
            return false;
        }
    }

    if (!s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_reg_read, pmu_reg_write)) {
        return false;
    }
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu_inited = true;
    s_pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    s_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
    return true;
}

// ---------------------------------------------------------------------------
// cJSON helpers: tolerant reads with defaults and clamping.
// ---------------------------------------------------------------------------
static bool json_bool(cJSON *obj, const char *key, bool def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

static int json_int(cJSON *obj, const char *key, int def, int lo, int hi)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    int r = cJSON_IsNumber(v) ? v->valueint : def;
    if (r < lo) r = lo;
    if (r > hi) r = hi;
    return r;
}

namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "Settings";

Settings *Settings::_instance = nullptr;
Settings::AppConfig Settings::cfg{};   // default-initialized
volatile bool Settings::sleep_locked = false;
volatile bool Settings::bluetooth_running = false;
bool Settings::_bt_connected  = false;
bool Settings::_bt_icon_shown = false;
Settings::BluetoothHandler Settings::_bt_handler = nullptr;
lv_timer_t *Settings::_save_timer = nullptr;
static bool s_sd_mounted    = false;
static bool s_load_attempted = false;
static bool s_save_after_load = false;   // the file lacks a key with a default worth writing (upgrade.src_url)
static constexpr size_t CFG_FILE_MAX = 4096;

// ---------------------------------------------------------------------------
// SD-backed config load/save.
// ---------------------------------------------------------------------------

bool Settings::ensureLoaded()
{
    if (s_load_attempted) return s_sd_mounted;
    s_load_attempted = true;

    // Best-effort: failures mean defaults / no persistence.
    if (bsp_sdcard_mount() == ESP_OK) {
        s_sd_mounted = true;
        if (!loadFromSd() || s_save_after_load) {
            saveToSd();
        }
    } else {
        ESP_UTILS_LOGW("SD mount failed -> using default settings (no persist)");
    }
    return s_sd_mounted;
}

bool Settings::loadFromSd()
{
    FILE *f = fopen(CFG_PATH, "r");
    if (f == nullptr) {
        return false;
    }
    // Heap, not stack: this runs on the main task (3.5 KB stack) and the file
    // is past 1 KB since the upgrade URL was added.
    char *buf = (char *)calloc(1, CFG_FILE_MAX);
    if (buf == nullptr) {
        fclose(f);
        return false;
    }
    size_t n = fread(buf, 1, CFG_FILE_MAX - 1, f);
    fclose(f);
    if (n == CFG_FILE_MAX - 1) ESP_UTILS_LOGW("%s is larger than %u B, truncated", CFG_PATH, (unsigned)CFG_FILE_MAX);
    cJSON *j = (n > 0) ? cJSON_Parse(buf) : nullptr;
    free(buf);
    if (j == nullptr) return false;

    // Items returned by cJSON_GetObjectItem() are owned by `j`; only
    // cJSON_Delete(j) at the end. Missing sections/keys keep the defaults.
    cJSON *root = cJSON_GetObjectItem(j, "settings");
    if (cJSON_IsObject(root)) {
        AppConfig d{};   // defaults
        cJSON *disp = cJSON_GetObjectItem(root, "display");
        if (cJSON_IsObject(disp)) {
            cfg.display.brightness              = json_int (disp, "brightness",              d.display.brightness, 1, 100);
            cfg.display.dim_screen              = json_bool(disp, "dim_screen",              d.display.dim_screen);
            cfg.display.user_button_wake_up     = json_bool(disp, "user_button_wake_up",     d.display.user_button_wake_up);
            cfg.display.disable_sleep_on_charge = json_bool(disp, "disable_sleep_on_charge", d.display.disable_sleep_on_charge);
            cfg.display.light_sleep_brightness  = json_int (disp, "light_sleep_brightness",  d.display.light_sleep_brightness, 1, 255);
            cfg.display.light_sleep_delay_s     = json_int (disp, "light_sleep_delay_s",     d.display.light_sleep_delay_s, 5, 30);
            cfg.display.deep_sleep_delay_s      = json_int (disp, "deep_sleep_delay_s",      d.display.deep_sleep_delay_s, 10, 60);
        }
        cJSON *audio = cJSON_GetObjectItem(root, "audio");
        if (cJSON_IsObject(audio)) {
            cfg.audio.vibrations            = json_bool(audio, "vibrations",         d.audio.vibrations);
            cfg.audio.vibration_duration_ms = json_int (audio, "vibration_duration", d.audio.vibration_duration_ms, 1, (int)MOTOR_MAX_ON_MS);
            cfg.audio.volume                = json_int (audio, "volume",             d.audio.volume, 0, 100);
        }
        cJSON *bt = cJSON_GetObjectItem(root, "bluetooth");
        if (cJSON_IsObject(bt))   cfg.bluetooth.enabled = json_bool(bt, "enabled", d.bluetooth.enabled);
        cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
        if (cJSON_IsObject(wifi)) cfg.wifi.enabled = json_bool(wifi, "enabled", d.wifi.enabled);
        cJSON *now = cJSON_GetObjectItem(root, "espnow");
        if (cJSON_IsObject(now))  cfg.espnow.enabled = json_bool(now, "enabled", d.espnow.enabled);
        cJSON *bat = cJSON_GetObjectItem(root, "battery");
        if (cJSON_IsObject(bat))  cfg.battery.show_percentage =
                                      json_bool(bat, "show_percentage", d.battery.show_percentage);
        cJSON *up = cJSON_GetObjectItem(root, "upgrade");
        if (cJSON_IsObject(up))   cfg.upgrade.enabled = json_bool(up, "enabled", d.upgrade.enabled);
        cJSON *src = cJSON_GetObjectItem(up, "src_url");
        if (cJSON_IsString(src) && src->valuestring[0] != '\0' &&
            strlen(src->valuestring) < sizeof(cfg.upgrade.src_url)) {
            strcpy(cfg.upgrade.src_url, src->valuestring);
        } else {
            strcpy(cfg.upgrade.src_url, d.upgrade.src_url);
            s_save_after_load = true;                  // write the default so the key shows up in the file
        }
        cJSON *dev = cJSON_GetObjectItem(root, "developer");
        if (cJSON_IsObject(dev))  cfg.developer.performance_counter =
                                      json_bool(dev, "performance_counter", d.developer.performance_counter);
    }
    cJSON_Delete(j);
    ESP_UTILS_LOGI("Settings loaded: brightness=%d dim=%d btn_wake=%d no_sleep_chg=%d ls_bri=%d ls=%ds ds=%ds "
                   "vib=%d/%dms vol=%d bt=%d wifi=%d espnow=%d",
                   cfg.display.brightness, (int)cfg.display.dim_screen, (int)cfg.display.user_button_wake_up,
                   (int)cfg.display.disable_sleep_on_charge, cfg.display.light_sleep_brightness,
                   cfg.display.light_sleep_delay_s, cfg.display.deep_sleep_delay_s,
                   (int)cfg.audio.vibrations, cfg.audio.vibration_duration_ms, cfg.audio.volume,
                   (int)cfg.bluetooth.enabled, (int)cfg.wifi.enabled, (int)cfg.espnow.enabled);
    return true;
}

bool Settings::saveToSd()
{
    if (!s_sd_mounted) return false;   // skip silently when no SD

    cJSON *j = cJSON_CreateObject();
    if (j == nullptr) return false;
    cJSON *root = cJSON_AddObjectToObject(j, "settings");
    if (root == nullptr) { cJSON_Delete(j); return false; }

    cJSON *disp = cJSON_AddObjectToObject(root, "display");
    cJSON_AddNumberToObject(disp, "brightness",              cfg.display.brightness);
    cJSON_AddBoolToObject  (disp, "dim_screen",              cfg.display.dim_screen);
    cJSON_AddBoolToObject  (disp, "user_button_wake_up",     cfg.display.user_button_wake_up);
    cJSON_AddBoolToObject  (disp, "disable_sleep_on_charge", cfg.display.disable_sleep_on_charge);
    cJSON_AddNumberToObject(disp, "light_sleep_brightness",  cfg.display.light_sleep_brightness);
    cJSON_AddNumberToObject(disp, "light_sleep_delay_s",     cfg.display.light_sleep_delay_s);
    cJSON_AddNumberToObject(disp, "deep_sleep_delay_s",      cfg.display.deep_sleep_delay_s);

    cJSON *audio = cJSON_AddObjectToObject(root, "audio");
    cJSON_AddBoolToObject  (audio, "vibrations",         cfg.audio.vibrations);
    cJSON_AddNumberToObject(audio, "vibration_duration", cfg.audio.vibration_duration_ms);
    cJSON_AddNumberToObject(audio, "volume",             cfg.audio.volume);

    cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "bluetooth"), "enabled", cfg.bluetooth.enabled);
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "wifi"),      "enabled", cfg.wifi.enabled);
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "espnow"),    "enabled", cfg.espnow.enabled);
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "battery"),   "show_percentage", cfg.battery.show_percentage);
    cJSON *up = cJSON_AddObjectToObject(root, "upgrade");
    cJSON_AddBoolToObject(up,   "enabled", cfg.upgrade.enabled);
    cJSON_AddStringToObject(up, "src_url", cfg.upgrade.src_url);
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "developer"), "performance_counter",
                          cfg.developer.performance_counter);

    char *out = cJSON_Print(j);  // pretty-printed
    cJSON_Delete(j);
    if (out == nullptr) return false;

    FILE *f = fopen(CFG_PATH, "w");
    if (f == nullptr) {
        cJSON_free(out);
        ESP_UTILS_LOGW("Settings save failed: fopen failed");
        return false;
    }
    fputs(out, f);
    fclose(f);
    cJSON_free(out);
    ESP_UTILS_LOGI("Settings saved");
    return true;
}

// Sliders fire VALUE_CHANGED for every pixel of a drag. Collapse a burst
// into one SD write, SAVE_DEBOUNCE_MS after the last change.
void Settings::saveLater()
{
    if (_save_timer == nullptr) {
        _save_timer = lv_timer_create(&Settings::onSaveTimer, SAVE_DEBOUNCE_MS, nullptr);
        if (_save_timer == nullptr) { saveToSd(); return; }
    }
    lv_timer_reset(_save_timer);
    lv_timer_resume(_save_timer);
}

void Settings::onSaveTimer(lv_timer_t *t)
{
    lv_timer_pause(t);
    saveToSd();
}

// ---------------------------------------------------------------------------
// Apply helpers
// ---------------------------------------------------------------------------

void Settings::setPanelBrightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    // The brightness command (0x51) goes over the same QSPI handle as the
    // LVGL flush and that handle has no locking of its own: serialise with
    // the LVGL task. The port mutex is recursive, so this is also fine from
    // the LVGL task itself. Before the display exists there is no lock to
    // take and the BSP call is a no-op (it checks its panel handle).
    const bool lvgl_up = (lv_display_get_default() != nullptr);
    bool locked = false;
    if (lvgl_up) {
        locked = bsp_display_lock(1000);
        if (!locked) ESP_UTILS_LOGW("Brightness: LVGL lock timeout, proceeding unlocked");
    }
    bsp_display_brightness_set(percent);
    if (locked) bsp_display_unlock();
}

void Settings::applyBrightness()
{
    setPanelBrightness(cfg.display.brightness);
}

int Settings::lightSleepBrightnessPercent()
{
    int v = cfg.display.light_sleep_brightness;        // slider range 1..255
    int pct = (v * 100 + 127) / 255;
    if (pct < 1)   pct = 1;
    if (pct > 100) pct = 100;
    return pct;
}

void Settings::applyVolume()
{
    if (!bsp_extra_codec_is_init()) return;             // Recorder brings the codec up lazily
    bsp_extra_codec_volume_set(cfg.audio.volume, nullptr);
}

void Settings::setVolume(int volume)
{
    if (volume < 0)   volume = 0;
    if (volume > 100) volume = 100;
    cfg.audio.volume = volume;
    applyVolume();
    saveLater();
}

void Settings::setBluetoothHandler(BluetoothHandler handler)
{
    _bt_handler = handler;
}

bool Settings::applyBluetooth()
{
    if (_bt_handler == nullptr) {
        ESP_UTILS_LOGW("Bluetooth: no handler registered (BLE app not installed)");
        return false;
    }
    return _bt_handler(cfg.bluetooth.enabled);
}

void Settings::applyPerformanceCounter()
{
#if LV_USE_PERF_MONITOR
    // lv_sysmon_show_performance() creates a new label + timer on every call
    // (LVGL 9.2.2), so it must not be called again: toggle the existing one.
    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr || disp->perf_label == nullptr) return;

    // LVGL creates the label on the display's system layer when the display
    // is created. Brookesia's Display::begin() then replaces disp->sys_layer
    // with its own system screen (status bar, gestures), so the label was left
    // on a layer that is never drawn again. Move it onto the current one.
    lv_obj_t *sys = lv_display_get_layer_sys(disp);
    if (sys != nullptr && lv_obj_get_parent(disp->perf_label) != sys) {
        lv_obj_set_parent(disp->perf_label, sys);
        lv_obj_add_flag(disp->perf_label, LV_OBJ_FLAG_FLOATING);           // out of any layout / scrolling
        lv_obj_align(disp->perf_label, LV_USE_PERF_MONITOR_POS, 0, 0);
    }

    const bool on = cfg.developer.performance_counter;
    if (on) {
        lv_obj_remove_flag(disp->perf_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(disp->perf_label);                           // above the status bar & co.
    } else {
        lv_obj_add_flag(disp->perf_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (disp->perf_sysmon_backend.timer != nullptr) {
        if (on) lv_timer_resume(disp->perf_sysmon_backend.timer);
        else    lv_timer_pause(disp->perf_sysmon_backend.timer);
    }
#else
    if (cfg.developer.performance_counter) {
        ESP_UTILS_LOGW("Performance counter: CONFIG_LV_USE_PERF_MONITOR is off in menuconfig");
    }
#endif
}

bool Settings::isSdMounted()
{
    return s_sd_mounted;
}

bool Settings::isCharging()
{
    if (!s_pmu_inited) return false;
    return s_pmu.isVbusIn();
}

// ---------------------------------------------------------------------------
// Vibration motor (GPIO18, active high). One esp_timer turns it off; a new
// tick while one is running just extends it.
// ---------------------------------------------------------------------------
static esp_timer_handle_t s_motor_timer = nullptr;
static bool               s_motor_ready = false;

static bool motor_prepare()
{
    if (s_motor_ready) return true;

    gpio_set_level(Settings::MOTOR_GPIO, 0);           // latch low before enabling the driver
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << Settings::MOTOR_GPIO;
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&io) != ESP_OK) {
        ESP_UTILS_LOGE("Motor GPIO%d config failed", (int)Settings::MOTOR_GPIO);
        return false;
    }
    gpio_set_level(Settings::MOTOR_GPIO, 0);

    esp_timer_create_args_t ta = {};
    ta.callback = &Settings::onMotorOff;
    ta.arg      = nullptr;
    ta.name     = "motor_off";
    if (esp_timer_create(&ta, &s_motor_timer) != ESP_OK) {
        ESP_UTILS_LOGE("Motor timer create failed");
        return false;
    }
    s_motor_ready = true;
    return true;
}

void Settings::onMotorOff(void * /*arg*/)
{
    gpio_set_level(MOTOR_GPIO, 0);
}

void Settings::vibrate(uint32_t ms)
{
    if (!cfg.audio.vibrations) return;
    if (ms == 0) ms = (uint32_t)cfg.audio.vibration_duration_ms;
    if (ms == 0) return;
    if (ms > MOTOR_MAX_ON_MS) ms = MOTOR_MAX_ON_MS;
    if (!motor_prepare()) return;

    esp_timer_stop(s_motor_timer);                     // ESP_ERR_INVALID_STATE if idle: fine
    gpio_set_level(MOTOR_GPIO, 1);
    if (esp_timer_start_once(s_motor_timer, (uint64_t)ms * 1000ULL) != ESP_OK) {
        gpio_set_level(MOTOR_GPIO, 0);                 // never leave the motor running
    }
}

// Haptic tick on press of an interactive element. Registered on the pointer
// indev, so it covers every app and the launcher without touching them.
// The pressed object arrives as the event parameter (lv_indev.c send_event()).
// Plain containers are clickable by default in LVGL, so only real widgets and
// images/labels that were made clickable on purpose (launcher icons) count.
void Settings::onIndevPressed(lv_event_t *e)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_param(e));
    if (obj == nullptr) obj = lv_indev_get_active_obj();
    if (obj == nullptr || lv_obj_get_parent(obj) == nullptr) return;   // nothing / a screen

    static const lv_obj_class_t *const kWidgets[] = {
        &lv_button_class, &lv_switch_class, &lv_slider_class, &lv_checkbox_class,
        &lv_dropdown_class, &lv_roller_class, &lv_textarea_class, &lv_imagebutton_class,
        &lv_buttonmatrix_class, &lv_arc_class, &lv_spinbox_class, &lv_keyboard_class,
        &lv_menu_cont_class,
    };
    bool hit = false;
    for (auto *cls : kWidgets) {
        if (lv_obj_check_type(obj, cls)) { hit = true; break; }
    }
    if (!hit && (lv_obj_check_type(obj, &lv_image_class) || lv_obj_check_type(obj, &lv_label_class))) {
        hit = lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    if (hit) vibrate();
}

Settings *Settings::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Settings(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Settings::Settings(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &settings_icon,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar)
{
    ESP_LOGI("Settings", "Settings created");
}

Settings::~Settings()
{
    _instance = nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Settings::init()
{
    ESP_UTILS_LOGI("init -> open PMU, mount SD, load config, start status feed");

    pmu_open();

    if (ensureLoaded()) {
        auto *phone = getSystem();
        if (phone) {
            auto *bar = phone->getDisplay().getStatusBar();
            if (bar) {
                systems::phone::StatusBarIcon::Data icon = {};
                icon.size  = gui::StyleSize::SQUARE(16);
                icon.icon.image_num = 1;
                icon.icon.images[0] = gui::StyleImage::IMAGE(&sd_card_icon);
                bar->addIcon(icon, /*area_index=*/2, SD_ICON_ID);
            }
        }
    }

    // Performance counter visibility from the saved config (hidden by default).
    applyPerformanceCounter();

    // Status bar: Wi-Fi icon visibility, battery percentage, clock position.
    refreshWifiIcon();
    applyBatteryPercentage();
    if (auto *bar = statusBar()) {
        for (uint8_t area : {STATUS_ICON_AREA, STATUS_CLOCK_AREA}) {
            lv_obj_t *obj = bar->getAreaObject(area);
            if (obj != nullptr) lv_obj_add_event_cb(obj, &Settings::onStatusBarLayout, LV_EVENT_LAYOUT_CHANGED, nullptr);
        }
    }

    // Long-lived LV timer that pushes battery state to the status bar.
    if (_status_timer == nullptr) {
        _status_timer = lv_timer_create(&Settings::onStatusTimer, STATUS_PERIOD_MS, this);
    }
    onStatusTimer(_status_timer);

    // Haptic feedback hook on every pointer input device.
    for (lv_indev_t *ind = lv_indev_get_next(nullptr); ind != nullptr; ind = lv_indev_get_next(ind)) {
        if (lv_indev_get_type(ind) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(ind, &Settings::onIndevPressed, LV_EVENT_PRESSED, nullptr);
        }
    }

    return true;
}

bool Settings::deinit()
{
    ESP_UTILS_LOGI("deinit");
    if (_status_timer != nullptr) {
        lv_timer_del(_status_timer);
        _status_timer = nullptr;
    }
    return true;
}

// The generated create_slider() hard-codes the initial slider values. Walk
// the screen once after creation and overwrite them with the saved config.
// Switches already read their state through the get_*_value() getters.
void Settings::syncSlidersFromCfg(lv_obj_t *root)
{
    if (root == nullptr) return;
    if (lv_obj_check_type(root, &lv_slider_class)) {
        const char *name = row_label_text(root);
        int v = -1;
        if      (strcmp(name, ROW_BRIGHTNESS) == 0)    v = cfg.display.brightness;
        else if (strcmp(name, ROW_LS_BRIGHTNESS) == 0) v = cfg.display.light_sleep_brightness;
        else if (strcmp(name, ROW_LS_DELAY) == 0)      v = cfg.display.light_sleep_delay_s;
        else if (strcmp(name, ROW_DS_DELAY) == 0)      v = cfg.display.deep_sleep_delay_s;
        else if (strcmp(name, ROW_VOLUME) == 0)        v = cfg.audio.volume;
        if (v >= 0) lv_slider_set_value(root, v, LV_ANIM_OFF);
        return;
    }
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        syncSlidersFromCfg(lv_obj_get_child(root, (int32_t)i));
    }
}

bool Settings::run()
{
    ESP_UTILS_LOGD("Run");
    Settings_ui_init();
    syncSlidersFromCfg(Settings_objects.main);
    return true;
}

bool Settings::back()
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

// ---------------------------------------------------------------------------
// Timer callbacks
// ---------------------------------------------------------------------------

void Settings::onStatusTimer(lv_timer_t *t)
{
    auto *self = static_cast<Settings *>(lv_timer_get_user_data(t));
    if (self == nullptr) return;

    // Catches a BLE state change that arrived before the status bar existed.
    syncBluetoothIcon();
    refreshWifiIcon();

    if (!s_pmu_inited) return;

    int  pct        = (int)s_pmu.getBatteryPercent();
    bool charging   = s_pmu.isCharging();
    bool battery_ok = s_pmu.isBatteryConnect();

    // Push to status bar. Need the phone-system Display (has getStatusBar()),
    // which we get via getSystem() -> Phone*. base::Context::getDisplay()
    // would only return the base Display which doesn't expose the bar.
    auto *phone = self->getSystem();
    if (phone == nullptr) return;
    auto *status_bar = phone->getDisplay().getStatusBar();
    if (status_bar == nullptr) return;

    int show_pct = battery_ok ? pct : 100;
    if (show_pct < 0)   show_pct = 0;
    if (show_pct > 100) show_pct = 100;
    status_bar->setBatteryPercent(charging, show_pct);
}

// ---------------------------------------------------------------------------
// Status bar: Wi-Fi icon, battery percentage, clock position
// ---------------------------------------------------------------------------

static int s_wifi_icon_state = -2;                      // last state pushed, -1 = hidden, -2 = never set
static constexpr int32_t CLOCK_GAP_PX        = 8;       // minimum space between the clock and the icons
static constexpr int32_t CLOCK_LEFT_LIMIT_PX = 40;      // the clock never goes further left (round corner)

systems::phone::StatusBar *Settings::statusBar()
{
    if (_instance == nullptr) return nullptr;
    auto *phone = _instance->getSystem();
    if (phone == nullptr) return nullptr;
    return phone->getDisplay().getStatusBar();
}

void Settings::refreshWifiIcon()
{
    auto *bar = statusBar();
    if (bar == nullptr) return;
    using WifiState = systems::phone::StatusBar::WifiState;
    int state = -1;                                     // hidden: neither Wi-Fi nor ESP-NOW enabled
    if (cfg.wifi.enabled || cfg.espnow.enabled) {
        state = static_cast<int>(isWifiConnected() ? WifiState::SIGNAL_3 : WifiState::DISCONNECTED);
    }
    if (state == s_wifi_icon_state) return;
    if (bar->setWifiIconState(state)) s_wifi_icon_state = state;
}

// Enabled Montserrat font whose line height is closest to half of ref's.
static const lv_font_t *half_size_font(const lv_font_t *ref)
{
    static const lv_font_t *const fonts[] = {
#if LV_FONT_MONTSERRAT_8
        &lv_font_montserrat_8,
#endif
#if LV_FONT_MONTSERRAT_10
        &lv_font_montserrat_10,
#endif
#if LV_FONT_MONTSERRAT_12
        &lv_font_montserrat_12,
#endif
#if LV_FONT_MONTSERRAT_14
        &lv_font_montserrat_14,
#endif
#if LV_FONT_MONTSERRAT_16
        &lv_font_montserrat_16,
#endif
#if LV_FONT_MONTSERRAT_18
        &lv_font_montserrat_18,
#endif
#if LV_FONT_MONTSERRAT_20
        &lv_font_montserrat_20,
#endif
#if LV_FONT_MONTSERRAT_22
        &lv_font_montserrat_22,
#endif
#if LV_FONT_MONTSERRAT_24
        &lv_font_montserrat_24,
#endif
        LV_FONT_DEFAULT,
    };
    const int32_t target = ref->line_height / 2;
    const lv_font_t *best = ref;
    int32_t best_diff = INT32_MAX;
    for (const lv_font_t *f : fonts) {
        const int32_t diff = std::abs(f->line_height - target);
        if (diff < best_diff) {
            best_diff = diff;
            best = f;
        }
    }
    return best;
}

// Icon area, left to right as Brookesia builds it: [Wi-Fi][%][battery], then
// the SD and Bluetooth icons Settings adds. Put the battery first and its
// percentage right after it: [battery][%][Wi-Fi][SD][BT]. The label is the
// only text in the area and Brookesia creates the battery icon right after it
// (StatusBar::beginBattery()), which is how the icon is found. Once is enough:
// icons added later are appended on the right.
static bool s_icons_arranged = false;

void Settings::applyBatteryPercentage()
{
    auto *bar = statusBar();
    if (bar == nullptr) return;
    if (cfg.battery.show_percentage) bar->showBatteryPercent();
    else                             bar->hideBatteryPercent();

    lv_obj_t *icons = bar->getAreaObject(STATUS_ICON_AREA);
    if (icons == nullptr) return;
    lv_obj_t *label = nullptr;
    int32_t label_idx = -1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(icons); i++) {
        lv_obj_t *child = lv_obj_get_child(icons, (int32_t)i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            label = child;
            label_idx = (int32_t)i;
            break;
        }
    }
    if (label == nullptr) {
        ESP_UTILS_LOGW("Status bar has no battery label (stylesheet enable_battery_label = 0?)");
        return;
    }

    // Percentage text at half the clock's size (the clock uses the bar's font).
    lv_obj_t *clock_area = bar->getAreaObject(STATUS_CLOCK_AREA);
    lv_obj_t *clock      = (clock_area != nullptr) ? lv_obj_get_child(clock_area, 0) : nullptr;
    lv_obj_t *clock_text = (clock != nullptr) ? lv_obj_get_child(clock, 0) : nullptr;
    if (clock_text != nullptr) {
        lv_obj_set_style_text_font(label, half_size_font(lv_obj_get_style_text_font(clock_text, LV_PART_MAIN)),
                                   LV_PART_MAIN);
    }

    if (!s_icons_arranged) {
        lv_obj_t *battery = lv_obj_get_child(icons, label_idx + 1);
        if (battery != nullptr) {
            lv_obj_move_to_index(battery, 0);
            lv_obj_move_to_index(label, 1);
            s_icons_arranged = true;
        }
    }
}

void Settings::onStatusBarLayout(lv_event_t * /*e*/)
{
    layoutStatusBar();
}

// The icon area is right-aligned (flex END, last child rightmost) and is wide
// enough for 5 icons, so it overlaps the clock area. The clock area is moved
// with translate_x, which does not touch the icon layout. Only written when
// the value changes: setting it re-runs the clock area's layout, whose
// LV_EVENT_LAYOUT_CHANGED calls this again, and must then find nothing to do.
void Settings::layoutStatusBar()
{
    auto *bar = statusBar();
    if (bar == nullptr) return;
    lv_obj_t *icons      = bar->getAreaObject(STATUS_ICON_AREA);
    lv_obj_t *clock_area = bar->getAreaObject(STATUS_CLOCK_AREA);
    if (icons == nullptr || clock_area == nullptr) return;
    lv_obj_t *clock = lv_obj_get_child(clock_area, 0);
    if (clock == nullptr || lv_obj_has_flag(clock, LV_OBJ_FLAG_HIDDEN)) return;

    // Visible items, right to left: the group's width, and the width taken by
    // the icons beyond the first STATUS_ICONS_BEFORE_SHIFT (the % label is not an icon).
    const int32_t pad = lv_obj_get_style_pad_column(icons, LV_PART_MAIN);
    int32_t group = 0;
    int32_t extra = 0;
    int     items = 0;
    int     icon_count = 0;
    for (int32_t i = (int32_t)lv_obj_get_child_count(icons) - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(icons, i);
        if (child == nullptr || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        const int32_t w = lv_obj_get_width(child) + ((items > 0) ? pad : 0);
        group += w;
        items++;
        if (!lv_obj_check_type(child, &lv_label_class) && ++icon_count > STATUS_ICONS_BEFORE_SHIFT) extra += w;
    }

    lv_area_t icons_area;
    lv_obj_get_coords(icons, &icons_area);
    const int32_t icons_left = icons_area.x2 - lv_obj_get_style_pad_right(icons, LV_PART_MAIN) - group + 1;

    // Clock position without the current shift.
    const int32_t tx_now = lv_obj_get_style_translate_x(clock_area, LV_PART_MAIN);
    lv_area_t clock_coords;
    lv_obj_get_coords(clock, &clock_coords);
    const int32_t clock_x1 = clock_coords.x1 - tx_now;
    const int32_t clock_x2 = clock_coords.x2 - tx_now;

    int32_t shift = extra;                                          // > 3 icons: room for each extra one
    if (items > 0) shift = std::max(shift, clock_x2 + CLOCK_GAP_PX - icons_left + 1);   // never touch the icons
    shift = std::max<int32_t>(shift, 0);
    shift = std::min<int32_t>(shift, std::max<int32_t>(clock_x1 - CLOCK_LEFT_LIMIT_PX, 0));
    if (tx_now != -shift) lv_obj_set_style_translate_x(clock_area, -shift, LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// Bluetooth status-bar icon
// ---------------------------------------------------------------------------

void Settings::setBluetoothConnected(bool connected)
{
    _bt_connected = connected;
    syncBluetoothIcon();
}

// Adds / removes the icon so it matches _bt_connected. Idempotent. Needs the
// LVGL lock: removeIcon() destroys the icon's LVGL objects.
void Settings::syncBluetoothIcon()
{
    if (_bt_connected == _bt_icon_shown) return;
    if (_instance == nullptr) return;

    auto *phone = _instance->getSystem();
    if (phone == nullptr) return;
    auto *bar = phone->getDisplay().getStatusBar();
    if (bar == nullptr) return;

    if (_bt_connected) {
        systems::phone::StatusBarIcon::Data icon = {};
        icon.size  = gui::StyleSize::SQUARE(16);
        icon.icon.image_num = 1;
        icon.icon.images[0] = gui::StyleImage::IMAGE(&bluetooth_icon_2);
        // Same area as the SD-card icon so the two sit side by side.
        if (bar->addIcon(icon, /*area_index=*/2, BT_ICON_ID)) {
            _bt_icon_shown = true;
        }
    } else {
        bar->removeIcon(BT_ICON_ID);
        _bt_icon_shown = false;
    }
}

// Self-register with the app registry. Phone picks it up via
// initAppFromRegistry() / installAppFromRegistry() in main.cpp.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Settings, APP_NAME, []()
{
    return std::shared_ptr<Settings>(Settings::requestInstance(), [](Settings *p) {});
})

} // namespace esp_brookesia::apps
