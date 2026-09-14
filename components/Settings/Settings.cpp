/*
 * Settings app - wires the on-board AXP2101 PMU to the Brookesia status bar
 * battery icon and provides a tiny info page.
 */
#include "Settings.hpp"
#include <cstdio>
#include <cstring>
#include "bsp/esp-bsp.h"
#include "cJSON.h"

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


extern "C" {
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"
    LV_IMG_DECLARE(settings_icon);
    LV_IMG_DECLARE(sd_card_icon);


    bool dim_screen, user_wake_up, sleep_charge, vibrations_enabled, 
        wifi_enabled, bluetooth_enabled, upgrade_enabled, espnow_enabled = false;

    char version_text[64] = {0};

    bool get_dim_screen_value(void) {
        return dim_screen;
    }
    void set_dim_screen_value(bool value) {
        dim_screen = value;
        cfg.dim_screen = value;
    }

    bool get_user_wake_up_button_value(void) {
        return user_wake_up;
    }
    void set_user_wake_up_button_value(bool value) {
        if (value) {
            Settings::sleep_locked = s_pmu.isVbusIn();
        } else {
            Settings::sleep_locked = false;
        }
        user_wake_up = value;
        cfg.user_wake_up = value;
    }

    bool get_sleep_charge_button_value(void) {
        return sleep_charge;
    }
    void set_sleep_charge_button_value(bool value) {
        sleep_charge = value;
        cfg.sleep_charge = value;
    }

    bool get_vibrations_value(void) {
        return vibrations_enabled;
    }
    void set_vibrations_value(bool value) {
        vibrations_enabled = value;
        cfg.vibrations_enabled = value;
    }

    bool get_wifi_value(void) {
        return wifi_enabled;
    }
    void set_wifi_value(bool value) {
        wifi_enabled = value;
        cfg.wifi_enabled = value;
    }

    bool get_bluetooth_value(void) {
        return bluetooth_enabled;
    }
    void set_bluetooth_value(bool value) {
        bluetooth_enabled = value;
        cfg.bluetooth_enabled = value;
    }

    bool get_upgrade_value(void) {
        return upgrade_enabled;
    }
    void set_upgrade_value(bool value) {
        upgrade_enabled = value;
        cfg.upgrade_enabled = value;
    }

    bool get_espnow_value(void) {
        return espnow_enabled;
    }
    void set_espnow_value(bool value) {
        espnow_enabled = value;
        cfg.espnow_enabled = value;
    }

    const char *get_version_value(const char* prefix, const char *suffix) {
        char txt[128] = {0};
        if (prefix == NULL) prefix = "";
        if (suffix == NULL) suffix = "";

        if (strlen(prefix) + strlen(suffix) > (sizeof(txt)/2)) return __TIME__;
        char version_txt[16] = {0};
        
        snprintf(version_txt, sizeof(version_txt), "%s", __TIME__);
        snprintf(txt, sizeof(txt), "%s%s%s", prefix, version_txt, suffix);
        return txt;
    }
    void set_version_value(const char * value) {
        // (void)value;
        strncpy(version_text, value, sizeof(value));
    }


    void action_button_pressed(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_CLICKED) {
            /* lv_obj_t *button = (lv_obj_t *)lv_event_get_target(e);
            
            // buttons_t type = *(buttons_t *)lv_obj_get_user_data(button);
            switch (type) {
                case BUTTON_CHECK_UPDATES:
                    ESP_LOGI(ESP_UTILS_LOG_TAG, "Check updates button pressed");
                    break;
                default:
                case BUTTON_TEST:
                    ESP_LOGI(ESP_UTILS_LOG_TAG, "Test button pressed");
                    break;
                    
            } */
        }
    }
    
    void action_slider_changed(lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        int v   = (int)lv_slider_get_value(slider);
    }
}
static constexpr int SD_ICON_ID = 1001;


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
    // s_pmu.setWatchdogConfig()
    return true;
}

namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "Settings";

Settings *Settings::_instance = nullptr;
Settings::AppConfig Settings::cfg{};   // default-initialized
volatile bool Settings::sleep_locked = false;
static bool s_sd_mounted = false;

// ---------------------------------------------------------------------------
// SD-backed config load/save.
//   { "settings": { "inactivity_delay": <ms> } }
// ---------------------------------------------------------------------------

bool Settings::loadFromSd()
{
    FILE *f = fopen(CFG_PATH, "r");
    if (f == nullptr) {
        return false;
    }
    char buf[256] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;

    cJSON *j = cJSON_Parse(buf);
    if (j == nullptr) return false;

    cJSON *root = cJSON_GetObjectItem(j, "settings");
    if (cJSON_IsObject(root)) {
        cJSON *d = cJSON_GetObjectItem(root, "inactivity_delay");
        if (cJSON_IsNumber(d) && d->valueint > 0) {
            cfg.inactivity_delay_ms = (uint32_t)d->valueint;
        }

        cJSON *aa = cJSON_GetObjectItem(root, "enable_user_button_wake_up");
        cfg.enable_user_button_wake_up = false;
        if (cJSON_IsBool(aa)) {
            if (cJSON_IsTrue(aa)) cfg.enable_user_button_wake_up = true;
        }
        cJSON_Delete(aa);

        cJSON *ab = cJSON_GetObjectItem(root, "disable_sleep_on_charge");
        cfg.disable_sleep_on_charge = false;
        if (cJSON_IsBool(ab)) {
            if (cJSON_IsTrue(ab)) cfg.disable_sleep_on_charge = true;
        }
        cJSON_Delete(ab);

        cJSON *ac = cJSON_GetObjectItem(root, "enable_vibrations");
        cfg.enable_vibrations = false;
        if (cJSON_IsBool(ac)) {
            if (cJSON_IsTrue(ac)) cfg.enable_vibrations = true;
        }
        cJSON_Delete(ac);

        cJSON *ad = cJSON_GetObjectItem(root, "enable_wifi");
        cfg.enable_wifi = false;
        if (cJSON_IsBool(ad)) {
            if (cJSON_IsTrue(ad)) cfg.enable_wifi = true;
        }
        cJSON_Delete(ad);

        cJSON *ae = cJSON_GetObjectItem(root, "enable_bluetooth");
        cfg.enable_bluetooth = false;
        if (cJSON_IsBool(ae)) {
            if (cJSON_IsTrue(ae)) cfg.enable_bluetooth = true;
        }
        cJSON_Delete(ae);

        cJSON *af = cJSON_GetObjectItem(root, "enable_espnow");
        cfg.enable_espnow = false;
        if (cJSON_IsBool(af)) {
            if (cJSON_IsTrue(af)) cfg.enable_espnow = true;
        }
        cJSON_Delete(af);
    }
    cJSON_Delete(j);
    ESP_UTILS_LOGI("Settings loaded: inactivity_delay=%u ms",
                   (unsigned)cfg.inactivity_delay_ms);
    return true;
}

bool Settings::saveToSd()
{
    if (!s_sd_mounted) return false;   // skip silently when no SD

    cJSON *j = cJSON_CreateObject();
    if (j == nullptr) return false;
    cJSON *root = cJSON_AddObjectToObject(j, "settings");
    if (root == nullptr) { cJSON_Delete(j); return false; }
    cJSON_AddNumberToObject(root, "inactivity_delay", cfg.inactivity_delay_ms);
    cJSON_AddBoolToObject(root, "enable_user_button_wake_up", cfg.enable_user_button_wake_up);
    cJSON_AddBoolToObject(root, "disable_sleep_on_charge", cfg.disable_sleep_on_charge);
    cJSON_AddBoolToObject(root, "dim_screen", cfg.dim_screen);
    cJSON_AddBoolToObject(root, "enable_wifi", cfg.enable_wifi);
    cJSON_AddBoolToObject(root, "enable_bluetooth", cfg.enable_bluetooth);
    cJSON_AddBoolToObject(root, "enable_espnow", cfg.enable_espnow);
    cJSON_AddBoolToObject(root, "enable_vibrations", cfg.enable_vibrations);

    char *out = cJSON_Print(j);  // pretty-print to match the shape user wants
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
    ESP_UTILS_LOGI("Settings saved: inactivity_delay=%u ms",
                   (unsigned)cfg.inactivity_delay_ms);
    return true;
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

    // Try to mount the SD card. Best-effort: failures are silently ignored
    // and we fall back to defaults / no persistence.
    if (bsp_sdcard_mount() == ESP_OK) {
        s_sd_mounted = true;
        if (!loadFromSd()) {
            saveToSd();
        }
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
    } else {
        ESP_UTILS_LOGW("SD mount failed -> using default settings (no persist)");
    }

    // Long-lived LV timer that pushes battery state to the status bar.
    if (_status_timer == nullptr) {
        _status_timer = lv_timer_create(&Settings::onStatusTimer, STATUS_PERIOD_MS, this);
    }
    onStatusTimer(_status_timer);

    Settings::sleep_locked = s_pmu.isVbusIn();

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

bool Settings::run()
{
    ESP_UTILS_LOGD("Run");
    Settings_ui_init();
    /* 
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_title = lv_label_create(scr);
    lv_label_set_text(_lbl_title, "Settings");
    lv_obj_set_style_text_color(_lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_align(_lbl_title, LV_ALIGN_TOP_MID, 0, 30);

    _lbl_pct = lv_label_create(scr);
    lv_obj_set_style_text_color(_lbl_pct, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lbl_pct, &lv_font_montserrat_18, 0);
    lv_obj_align(_lbl_pct, LV_ALIGN_CENTER, 0, -30);

    _lbl_volt = lv_label_create(scr);
    lv_obj_set_style_text_color(_lbl_volt, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_font(_lbl_volt, &lv_font_montserrat_18, 0);
    lv_obj_align(_lbl_volt, LV_ALIGN_CENTER, 0, 0);

    _lbl_charge = lv_label_create(scr);
    lv_obj_set_style_text_color(_lbl_charge, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_font(_lbl_charge, &lv_font_montserrat_18, 0);
    lv_obj_align(_lbl_charge, LV_ALIGN_CENTER, 0, 30);

    // ---- Inactivity delay row: label + minus / plus buttons ----
    _lbl_delay = lv_label_create(scr);
    lv_obj_set_style_text_color(_lbl_delay, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lbl_delay, &lv_font_montserrat_18, 0);
    lv_obj_align(_lbl_delay, LV_ALIGN_CENTER, 0, 80);

    lv_obj_t *btn_minus = lv_btn_create(scr);
    lv_obj_set_size(btn_minus, 40, 40);
    lv_obj_align(btn_minus, LV_ALIGN_CENTER, -90, 130);
    lv_obj_t *lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_center(lbl_minus);
    lv_obj_add_event_cb(btn_minus, &Settings::onDelayMinusClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *btn_plus = lv_btn_create(scr);
    lv_obj_set_size(btn_plus, 40, 40);
    lv_obj_align(btn_plus, LV_ALIGN_CENTER, 90, 130);
    lv_obj_t *lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_center(lbl_plus);
    lv_obj_add_event_cb(btn_plus, &Settings::onDelayPlusClicked, LV_EVENT_CLICKED, this);

    refreshUi();

    // 1s UI timer - only ticks while app foreground (auto-cleaned on close).
    _ui_timer = lv_timer_create(&Settings::onUiTimer, 1000, this);
     */
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
    if (self == nullptr || !s_pmu_inited) return;

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

void Settings::onUiTimer(lv_timer_t *t)
{
    auto *self = static_cast<Settings *>(lv_timer_get_user_data(t));
    if (self == nullptr) return;
    self->refreshUi();
}

void Settings::refreshUi()
{
    if (!s_pmu_inited) return;
    if (_lbl_pct == nullptr) return;

    char buf[48];

    if (s_pmu.isBatteryConnect()) {
        snprintf(buf, sizeof(buf), "Battery: %d %%", (int)s_pmu.getBatteryPercent());
    } else {
        snprintf(buf, sizeof(buf), "Battery: not connected");
    }
    lv_label_set_text(_lbl_pct, buf);

    snprintf(buf, sizeof(buf), "Voltage: %d mV", (int)s_pmu.getBattVoltage());
    lv_label_set_text(_lbl_volt, buf);

    const char *state = "discharging";
    if (s_pmu.isCharging())          state = "charging";
    else if (s_pmu.isVbusIn())       state = "USB connected";
    else if (s_pmu.isStandby())      state = "standby";
    snprintf(buf, sizeof(buf), "State: %s", state);
    lv_label_set_text(_lbl_charge, buf);

    if (_lbl_delay != nullptr) {
        // Show as seconds (1 decimal) for readability; the underlying value
        // and the JSON file are stored in milliseconds.
        snprintf(buf, sizeof(buf), "Inactivity delay: %.1f s",
                 cfg.inactivity_delay_ms / 1000.0f);
        lv_label_set_text(_lbl_delay, buf);
    }
}

// Step size for + / - buttons (ms). Min/max bound the slider.
static constexpr uint32_t DELAY_STEP_MS = 5000;
static constexpr uint32_t DELAY_MIN_MS  = 5000;
static constexpr uint32_t DELAY_MAX_MS  = 600000;

void Settings::onDelayMinusClicked(lv_event_t *e)
{
    auto *self = static_cast<Settings *>(lv_event_get_user_data(e));
    if (self == nullptr) return;
    if (cfg.inactivity_delay_ms > DELAY_MIN_MS) {
        cfg.inactivity_delay_ms -= DELAY_STEP_MS;
        saveToSd();
        self->refreshUi();
    }
}

void Settings::onDelayPlusClicked(lv_event_t *e)
{
    auto *self = static_cast<Settings *>(lv_event_get_user_data(e));
    if (self == nullptr) return;
    if (cfg.inactivity_delay_ms < DELAY_MAX_MS) {
        cfg.inactivity_delay_ms += DELAY_STEP_MS;
        saveToSd();
        self->refreshUi();
    }
}

// Self-register with the app registry. Phone picks it up via
// initAppFromRegistry() / installAppFromRegistry() in main.cpp.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Settings, APP_NAME, []()
{
    return std::shared_ptr<Settings>(Settings::requestInstance(), [](Settings *p) {});
})

} // namespace esp_brookesia::apps
