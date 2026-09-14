/*
 * ClockApp - Always On Display style clock with touch + button wake.
 *
 * Power tricks:
 *   - DIMMED state: backlight off (or dim), tick slowed to 1/min.
 *   - ACTIVE state: backlight on, tick at 1 Hz.
 *   - Pixel shift every minute = OLED/AMOLED burn-in protection.
 *
 * Wake sources (polled at 50ms in _wake_poll, runs even when app is paused):
 *   - Touch indev press edge   -> wake / re-foreground.
 *   - User button (GPIO0/BOOT) -> always wake / re-foreground / toggle AOD.
 *
 * Behavior matrix when wake event fires:
 *   FG = ClockApp is the active foreground app. BG = paused (other app or launcher).
 *
 *     EVENT          FG+ACTIVE        FG+DIMMED        BG+launcher       BG+other app
 *     button press   toggle->DIMMED   wake->ACTIVE     re-foreground     re-foreground
 *     touch press    nothing          wake->ACTIVE     re-foreground     ignored (other
 *                                                                         app handles it)
 *
 * Idle detection:
 *   - lv_disp_get_inactive_time(NULL) >= AOD_IDLE_MS -> enterDimmed().
 *   - Touch resets LVGL inactive time automatically.
 *   - Button wake also resets via lv_disp_trig_activity().
 */
#include "ClockApp.hpp"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include "bsp/esp-bsp.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/rtc_io.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// XPowersLib: select AXP2101 chip and pull in the templated header.
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

// Pull in the Settings app's persistent config so we can read the
// user-configured inactivity delay at runtime.
#include "Settings.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "ClockApp"
#include "esp_lib_utils.h"


extern "C" {
    // #include "ui.h"
    // #include "vars.h"
    // #include "styles.h"
    // #include "structs.h"
    // #include "images.h"
    // #include "fonts.h"
    // #include "screens.h"
    LV_IMG_DECLARE(clock_icon);
}

// ---------------------------------------------------------------------------
// File-scope PMU singleton + I2C glue. Defined here at top so all member
// functions in this translation unit can see them. Plain `static` (internal
// linkage) keeps these private to this .cpp.
// ---------------------------------------------------------------------------
static XPowersPMU              s_pmu;
static bool                    s_pmu_inited = false;
static i2c_master_dev_handle_t s_pmu_dev    = nullptr;

// Read callback: write 1-byte register address, then read `len` bytes.
static int pmu_reg_read(uint8_t /*devAddr*/, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (s_pmu_dev == nullptr) {
        return -1;
    }
    esp_err_t err = i2c_master_transmit_receive(s_pmu_dev, &regAddr, 1, data, len, 100);
    return (err == ESP_OK) ? 0 : -1;
}

// Write callback: send register address followed by `len` bytes.
static int pmu_reg_write(uint8_t /*devAddr*/, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (s_pmu_dev == nullptr) {
        return -1;
    }
    uint8_t pkt[1 + 32];
    if (len > sizeof(pkt) - 1) {
        return -1;
    }
    pkt[0] = regAddr;
    if (len > 0 && data != nullptr) {
        memcpy(&pkt[1], data, len);
    }
    esp_err_t err = i2c_master_transmit(s_pmu_dev, pkt, len + 1, 100);
    return (err == ESP_OK) ? 0 : -1;
}

namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "Clock";

ClockApp *ClockApp::_instance = nullptr;

ClockApp *ClockApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new ClockApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

ClockApp::ClockApp(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &clock_icon,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar)
{
    ESP_LOGI("ClockApp", "ClockApp created");
}

ClockApp::~ClockApp()
{
    _instance = nullptr;
}

bool ClockApp::run()
{
    ESP_UTILS_LOGD("Run");

    _screen = lv_scr_act();

    lv_obj_set_style_bg_color(_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Big time label
    _lbl_time = lv_label_create(_screen);
    lv_label_set_text(_lbl_time, "--:--");
    lv_obj_set_style_text_color(_lbl_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lbl_time, &jetbrains_96_1bpp, 0);
    lv_obj_align(_lbl_time, LV_ALIGN_CENTER, 0, -20);

    // Date label
    _lbl_date = lv_label_create(_screen);
    lv_label_set_text(_lbl_date, "----");
    lv_obj_set_style_text_color(_lbl_date, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(_lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(_lbl_date, LV_ALIGN_CENTER, 0, 30);

    // Battery label (below date)
    _lbl_bat = lv_label_create(_screen);
    lv_label_set_text(_lbl_bat, "-- %");
    lv_obj_set_style_text_color(_lbl_bat, lv_color_hex(0x606060), 0);
    lv_obj_set_style_text_font(_lbl_bat, &lv_font_montserrat_24, 0);
    lv_obj_align(_lbl_bat, LV_ALIGN_CENTER, 0, 60);

    // No screen touch event handler needed: we poll the indev directly in
    // the wake timer so we can react even when this app is in the background.

    // Listen for swipe gestures on the screen so we can pop up the
    // "Set Clock" panel on swipe-down.
    lv_obj_add_event_cb(_screen, &ClockApp::onScreenGesture, LV_EVENT_GESTURE, this);

    // Pull current time from the on-board PCF85063A RTC.
    syncTimeFromRtc();

    // Initialize the AXP2101 PMIC for battery monitoring.
    initBatteryMonitor();

    // Initial draw
    refreshTime();

    // 1s tick: clock + idle check.
    _tick = lv_timer_create(&ClockApp::onTickTimer, 1000, this);

    // 50ms wake button poll. Stays ACTIVE-state independent.
    configureButton();
    _wake_poll = lv_timer_create(&ClockApp::onWakeTimer, 50, this);

    _state = State::ACTIVE;

    return true;
}

bool ClockApp::init()
{
    ESP_UTILS_LOGI("init -> spawn always-on sleep monitor");
    // Spawn sleep monitor at install time so it runs regardless of which
    // app is foreground (including the launcher with no app open).
    _stop_sleep_task = false;
    if (_sleep_task == nullptr) {
        xTaskCreate(&ClockApp::sleepMonitorTask, "ClockSleep", 4096, this, 5, &_sleep_task);
    }
    return true;
}

bool ClockApp::deinit()
{
    ESP_UTILS_LOGI("deinit -> stop sleep monitor");
    _stop_sleep_task = true;
    return true;
}

bool ClockApp::back()
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool ClockApp::pause()
{
    ESP_UTILS_LOGD("Pause (system) -> background; keep wake poll running");
    // App moves to background. Make sure backlight is on for whatever the
    // foreground app draws. We KEEP _wake_poll running so we can re-foreground
    // ourselves on touch/button.
    enterActive();
    return true;
}

bool ClockApp::resume()
{
    ESP_UTILS_LOGD("Resume (system) -> ACTIVE");
    enterActive();
    refreshTime();
    return true;
}

bool ClockApp::close()
{
    ESP_UTILS_LOGD("Close");

    // NOTE: do NOT stop the sleep monitor here. It must keep running while
    // the user is in the launcher / another app so the watch still sleeps
    // after inactivity. The task is killed only at deinit (uninstall).

    // Tear down PMU monitoring.
    deinitBatteryMonitor();

    // Make sure backlight is on so next screen is visible.
    setBacklight(true);
    _screen          = nullptr;
    _lbl_time        = nullptr;
    _lbl_date        = nullptr;
    _lbl_bat         = nullptr;
    _tick            = nullptr;
    _wake_poll       = nullptr;
    _settings_panel  = nullptr;
    for (int i = 0; i < FIELD_COUNT; i++) _settings_value_lbl[i] = nullptr;
    return true;
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void ClockApp::enterActive()
{
    if (_state == State::ACTIVE) {
        return;
    }
    ESP_UTILS_LOGI("AOD exit -> ACTIVE");
    _state = State::ACTIVE;

    setBacklight(true);
    if (_tick != nullptr) {
        lv_timer_set_period(_tick, 1000);
    }
    refreshTime();
}

void ClockApp::enterDimmed()
{
    if (_state == State::DIMMED) {
        return;
    }
    ESP_UTILS_LOGI("AOD enter -> DIMMED");
    _state = State::DIMMED;

    if (_tick != nullptr) {
        lv_timer_set_period(_tick, 60 * 1000);
    }
    // Fully off. Swap for bsp_display_brightness_set(10) if BSP exposes dim.
    setBacklight(false);
}

void ClockApp::wakeFromAnySource()
{
    // Reset LVGL idle counter so we don't bounce straight back to DIMMED.
    lv_disp_trig_activity(NULL);
    enterActive();
}

void ClockApp::toggleAod()
{
    if (_state == State::ACTIVE) {
        // Force dim now. Reset idle counter so re-entry logic stays clean.
        lv_disp_trig_activity(NULL);
        enterDimmed();
    } else {
        wakeFromAnySource();
    }
}

// ---------------------------------------------------------------------------
// Light / deep sleep
// ---------------------------------------------------------------------------

void ClockApp::enterLightSleep()
{
    ESP_UTILS_LOGI("Enter LIGHT SLEEP");
    _state = State::LIGHT_SLEEP;

    // Make sure display is off before sleeping.
    setBacklight(false);

    // GPIO wakeup: button (GPIO0) and touch INT (GPIO38). Both active LOW.
    gpio_wakeup_enable(BTN_WAKE_GPIO,  GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(TOUCH_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Timer wakeup so we can escalate to deep sleep at DEEP_SLEEP_IDLE_MS.
    uint32_t already_idle = lv_disp_get_inactive_time(NULL);
    uint32_t remain_ms = (DEEP_SLEEP_IDLE_MS > already_idle) ? (DEEP_SLEEP_IDLE_MS - already_idle) : 1000;
    esp_sleep_enable_timer_wakeup((uint64_t)remain_ms * 1000ULL);

    // BLOCKS the calling task. Whole chip sleeps until a wake source fires.
        // enterDeepSleep();
    esp_light_sleep_start();

    // ---------- Wake ----------
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_UTILS_LOGI("Light sleep wake cause=%d", (int)cause);

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        // No user input for full DEEP_SLEEP_IDLE_MS -> escalate.
        ESP_UTILS_LOGI("Enter DEEP SLEEP");
        enterDeepSleep();
        // not reached
    }
    ESP_UTILS_LOGI("Exit LIGHT SLEEP");

    // GPIO wake -> bring system back to ACTIVE.
    setBacklight(true);

    // We are on the sleep-monitor task here, not LVGL's. Both of these mutate
    // LVGL state -- lv_async_call() in particular edits the timer list that
    // lv_timer_handler() is walking -- so they have to be done under the lock.
    if (bsp_display_lock(1000)) {
        // Reset the idle counter so we don't bounce straight back to dim/sleep.
        lv_disp_trig_activity(NULL);
        // Schedule UI restore on the LVGL task.
        lv_async_call(&ClockApp::onWakeFromLightSleepAsync, this);
        bsp_display_unlock();
    } else {
        ESP_UTILS_LOGW("Light sleep exit: LVGL lock timeout, UI not restored");
    }
}

ClockApp::DeepSleepHandler ClockApp::_deep_sleep_handler = nullptr;

void ClockApp::setBacklight(bool on)
{
    // esp_lcd's SPI panel IO is NOT thread safe. Backlight control is command
    // 0x51 sent over the SAME QSPI handle the LVGL flush uses for pixels, and
    // that handle keeps a shared transaction pool plus a num_trans_inflight
    // counter with no locking of its own.
    //
    // Call this from any task other than LVGL's while a flush is in flight and
    // the pool gets corrupted. First symptom is
    //   lcd_panel.io.spi: panel_io_spi_tx_color(395): spi transmit (queue) color failed
    // and from then on the handle is wedged: further commands are accepted by
    // the driver and never reach the panel. That is why the log cheerfully
    // reports "Backlight off" while the screen stays lit and frozen.
    //
    // The sleep monitor runs on its own task, so every one of its backlight
    // calls was racing the flush. lvgl_port's mutex is recursive, so taking it
    // here is also safe from the LVGL task itself.
    // Callable before bsp_display_start_with_config(), e.g. from the early
    // ULP-wake path that never brings the display up at all. In that state
    // lvgl_port_lock() would trip its own assert, while the BSP brightness
    // call below is a safe no-op because it checks panel_handle itself.
    const bool lvgl_up = (lv_display_get_default() != nullptr);

    bool locked = false;
    if (lvgl_up) {
        locked = bsp_display_lock(1000);
        if (!locked) {
            ESP_UTILS_LOGW("Backlight: LVGL lock timeout, proceeding unlocked");
        }
    }

    if (on) {
        bsp_display_backlight_on();
    } else {
        bsp_display_backlight_off();
    }

    if (locked) {
        bsp_display_unlock();
    }
}

void ClockApp::setDeepSleepHandler(DeepSleepHandler handler)
{
    _deep_sleep_handler = handler;
}

// Which rails were on before we dropped them. Kept in RTC memory so it
// survives deep sleep; a real power cycle zeroes it, and in that case the PMU
// has reset to its own defaults anyway so there is nothing to restore.
RTC_DATA_ATTR static uint32_t s_rail_snapshot = 0;
static constexpr uint32_t RAIL_SNAP_VALID = 1u << 31;

bool ClockApp::ensurePmuStatic()
{
    // Static-only bring-up, so rail restore can run from app_main long before
    // any ClockApp instance exists.
    if (s_pmu_inited) return true;

    if (s_pmu_dev == nullptr) {
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (bus == nullptr) return false;
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address  = PMU_I2C_ADDR;
        cfg.scl_speed_hz    = 400000;
        if (i2c_master_bus_add_device(bus, &cfg, &s_pmu_dev) != ESP_OK) {
            s_pmu_dev = nullptr;
            return false;
        }
    }
    if (!s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_reg_read, pmu_reg_write)) {
        return false;
    }
    s_pmu_inited = true;
    return true;
}

void ClockApp::powerUpAfterUlpSleep()
{
    // AXP2101 registers survive an ESP32-S3 deep sleep, so every rail dropped
    // on the way down is still off. Put back exactly what was on before,
    // before the display or touch are brought up on those rails.
    if (!(s_rail_snapshot & RAIL_SNAP_VALID)) return;
    if (!ensurePmuStatic()) {
        ESP_UTILS_LOGE("PMU unreachable, cannot restore rails after sleep");
        return;
    }

    const uint32_t s = s_rail_snapshot;
    if (s & RAIL_DC2)     s_pmu.enableDC2();
    if (s & RAIL_DC3)     s_pmu.enableDC3();
    if (s & RAIL_DC4)     s_pmu.enableDC4();
    if (s & RAIL_DC5)     s_pmu.enableDC5();
    if (s & RAIL_ALDO1)   s_pmu.enableALDO1();
    if (s & RAIL_ALDO2)   s_pmu.enableALDO2();
    if (s & RAIL_ALDO3)   s_pmu.enableALDO3();
    if (s & RAIL_ALDO4)   s_pmu.enableALDO4();
    if (s & RAIL_BLDO1)   s_pmu.enableBLDO1();
    if (s & RAIL_BLDO2)   s_pmu.enableBLDO2();
    if (s & RAIL_CPUSLDO) s_pmu.enableCPUSLDO();
    if (s & RAIL_DLDO1)   s_pmu.enableDLDO1();
    if (s & RAIL_DLDO2)   s_pmu.enableDLDO2();

    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();

    s_rail_snapshot = 0;

    // Rails need a moment to come up before the panel is talked to.
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_UTILS_LOGI("Rails restored after ULP sleep (0x%03X)",
                   (unsigned)(s & 0x3FF));
}

void ClockApp::displayEnterSleep()
{
    // Same shared-handle rule as setBacklight(): these are panel commands on
    // the QSPI handle the LVGL flush also uses, so they must be serialised
    // against it. Callable before the display exists -- the BSP no-ops on a
    // null handle, and lvgl_port_lock() would assert, so skip the lock then.
    const bool lvgl_up = (lv_display_get_default() != nullptr);

    bool locked = false;
    if (lvgl_up) {
        locked = bsp_display_lock(1000);
        if (!locked) {
            ESP_UTILS_LOGW("Panel sleep: LVGL lock timeout, proceeding unlocked");
        }
    }

    bsp_display_enter_sleep();

    if (locked) {
        bsp_display_unlock();
    }
}

void ClockApp::powerDownForUlpSleep(uint32_t drop_mask)
{
    // Panel off for real, not just a black picture: DISPOFF + SLPIN. Brightness
    // 0 alone leaves the SH8601 driving the matrix and its analogue front end,
    // which is a large slice of the standby current. Nothing has to undo it --
    // waking is a full reboot and bsp_display_new() hardware-resets the panel.
    displayEnterSleep();

#ifdef BSP_POWER_AMP_IO
    gpio_set_direction(BSP_POWER_AMP_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_POWER_AMP_IO, 0);
#endif

    if (drop_mask == RAIL_NONE) {
        // Default. Standby current stays high, but the watch always wakes.
        s_rail_snapshot = 0;
        ESP_UTILS_LOGI("Sleep with all rails up (drop_mask=0)");
        return;
    }

    if (!s_pmu_inited) {
        ESP_UTILS_LOGW("PMU not up, cannot drop rails for sleep");
        s_rail_snapshot = 0;
        return;
    }

    // Record the pre-sleep state so the wake path restores exactly what was on.
    uint32_t snap = RAIL_SNAP_VALID;
    if (s_pmu.isEnableDC2())     snap |= RAIL_DC2;
    if (s_pmu.isEnableDC3())     snap |= RAIL_DC3;
    if (s_pmu.isEnableDC4())     snap |= RAIL_DC4;
    if (s_pmu.isEnableDC5())     snap |= RAIL_DC5;
    if (s_pmu.isEnableALDO1())   snap |= RAIL_ALDO1;
    if (s_pmu.isEnableALDO2())   snap |= RAIL_ALDO2;
    if (s_pmu.isEnableALDO3())   snap |= RAIL_ALDO3;
    if (s_pmu.isEnableALDO4())   snap |= RAIL_ALDO4;
    if (s_pmu.isEnableBLDO1())   snap |= RAIL_BLDO1;
    if (s_pmu.isEnableBLDO2())   snap |= RAIL_BLDO2;
    if (s_pmu.isEnableCPUSLDO()) snap |= RAIL_CPUSLDO;
    if (s_pmu.isEnableDLDO1())   snap |= RAIL_DLDO1;
    if (s_pmu.isEnableDLDO2())   snap |= RAIL_DLDO2;
    s_rail_snapshot = snap;

    // Drop only what was explicitly asked for. DC1 is never in the mask.
    if (drop_mask & RAIL_DC2)     s_pmu.disableDC2();
    if (drop_mask & RAIL_DC3)     s_pmu.disableDC3();
    if (drop_mask & RAIL_DC4)     s_pmu.disableDC4();
    if (drop_mask & RAIL_DC5)     s_pmu.disableDC5();
    if (drop_mask & RAIL_ALDO1)   s_pmu.disableALDO1();
    if (drop_mask & RAIL_ALDO2)   s_pmu.disableALDO2();
    if (drop_mask & RAIL_ALDO3)   s_pmu.disableALDO3();
    if (drop_mask & RAIL_ALDO4)   s_pmu.disableALDO4();
    if (drop_mask & RAIL_BLDO1)   s_pmu.disableBLDO1();
    if (drop_mask & RAIL_BLDO2)   s_pmu.disableBLDO2();
    if (drop_mask & RAIL_CPUSLDO) s_pmu.disableCPUSLDO();
    if (drop_mask & RAIL_DLDO1)   s_pmu.disableDLDO1();
    if (drop_mask & RAIL_DLDO2)   s_pmu.disableDLDO2();

    // Deliberately NO gpio_hold_en() on the I2C lines here. prepareDeepSleepPower()
    // holds them, which would freeze the pads and stop the ULP bit-banging.
    ESP_UTILS_LOGI("Rails down for ULP sleep (drop=0x%04X, was=0x%04X)",
                   (unsigned)drop_mask, (unsigned)(snap & 0x1FFF));
}

void ClockApp::enterDeepSleep()
{
    ESP_UTILS_LOGI("Enter DEEP SLEEP");

    setBacklight(false);

    // If something needs the RTC domain to stay alive across sleep (the
    // Podometer needs its ULP to keep counting), it owns the sleep. The PMU
    // shutdown below would cut the rail the ULP runs on.
    if (_deep_sleep_handler != nullptr) {
        ESP_UTILS_LOGI("Delegating to registered deep-sleep handler");
        _deep_sleep_handler();
        // Handler is not expected to return. If it does, fall through to the
        // power-off path rather than sitting in a half-slept state.
        ESP_UTILS_LOGW("Deep-sleep handler returned, falling back to PMU off");
    }

    // Preferred path: full PMU shutdown. AXP2101 cuts every rail (incl. DC1),
    // ESP32-S3 powers off completely. PEKEY (sleep button) press wakes the
    // PMU which re-powers the chip = fresh boot.


    // Fallback: PMU not initialised. Use ESP deep sleep + BOOT button wake.
    // prepareDeepSleepPower();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_deinit(BTN_WAKE_GPIO);
    rtc_gpio_init(BTN_WAKE_GPIO);
    rtc_gpio_set_direction(BTN_WAKE_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_set_direction_in_sleep(BTN_WAKE_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(BTN_WAKE_GPIO);
    rtc_gpio_pullup_en(BTN_WAKE_GPIO);
    rtc_gpio_hold_en(BTN_WAKE_GPIO);
    uint64_t io_mask = 1ULL << BTN_WAKE_GPIO;
    esp_sleep_enable_ext1_wakeup(io_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    gpio_deep_sleep_hold_en();

    if (s_pmu_inited) {
        ESP_UTILS_LOGI("PMU shutdown -> PEKEY press will reboot the watch");
        s_pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
        s_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
        s_pmu.clearIrqStatus();
        s_pmu.shutdown();
        // Power dies almost immediately. Spin so we don't return.
        for (;;) { vTaskDelay(pdMS_TO_TICKS(100)); }
    }
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// Bring every on-board peripheral to its lowest practical power state before
// deep sleep. Run from the same task that calls esp_deep_sleep_start().
//
// What we touch:
//   - Display    : backlight OFF (BSP). Panel power is killed by AXP rails.
//   - Audio amp  : BSP_POWER_AMP_IO driven low so the speaker amp sits idle.
//   - AXP2101    : disable every non-essential power rail (DCx, ALDOx, BLDOx,
//                   CPUSLDO, DLDOx). Battery / system / temp measurements
//                   disabled to drop a few extra uA. We deliberately do NOT
//                   touch DC1 (ESP32-S3 main 3.3 V rail) — cutting it would
//                   brown the chip out before it can park itself.
//   - GPIO holds : freeze RTC-capable output GPIOs at their current level so
//                   peripheral control lines don't float during deep sleep.
//                   The wake button (GPIO0) is exempt so it can fire wake.
//
// Notes:
//   - PCF85063A RTC has its own coin-cell backup; survives any rail change.
//   - Touch INT line (GPIO38) is not RTC-capable, so we cannot hold it.
// ---------------------------------------------------------------------------
void ClockApp::prepareDeepSleepPower()
{
    ESP_UTILS_LOGI("Powering down peripherals for deep sleep");

    // ---- Display backlight off ----
    setBacklight(false);

    // ---- Audio amp enable: drive low so the class-D amp goes idle ----
#ifdef BSP_POWER_AMP_IO
    gpio_set_direction(BSP_POWER_AMP_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    if (BSP_POWER_AMP_IO >= 0 && BSP_POWER_AMP_IO <= 21) {
        gpio_hold_en(BSP_POWER_AMP_IO);
    }
#endif

    // ---- AXP2101 rails: disable every rail not required while sleeping ----
    if (s_pmu_inited) {
        // Buck converters (keep DC1 = main 3.3 V powering the ESP32 alive)
        s_pmu.disableDC2();
        s_pmu.disableDC3();
        s_pmu.disableDC4();
        s_pmu.disableDC5();

        // ALDO 1..4 (camera / sensor / OLED logic supplies on the ref design)
        s_pmu.disableALDO1();
        s_pmu.disableALDO2();
        s_pmu.disableALDO3();
        s_pmu.disableALDO4();

        // BLDO 1..2 (OLED / mic supplies on the ref design)
        s_pmu.disableBLDO1();
        s_pmu.disableBLDO2();

        // CPUSLDO + DLDOs
        s_pmu.disableCPUSLDO();
        s_pmu.disableDLDO1();
        s_pmu.disableDLDO2();

        // Stop all measurements (saves a few uA)
        s_pmu.disableBattVoltageMeasure();
        s_pmu.disableSystemVoltageMeasure();
        s_pmu.disableTemperatureMeasure();
    }

    // ---- Hold RTC-capable GPIOs at their current level ----
    static const gpio_num_t hold_pins[] = {
        BSP_LCD_RST,        // panel reset
        BSP_LCD_TOUCH_RST,  // touch IC reset
        BSP_LCD_CS,         // QSPI CS
        BSP_I2C_SCL,        // I2C lines (pulled high by default)
        BSP_I2C_SDA,
    };
    for (auto pin : hold_pins) {
        if (pin == BTN_WAKE_GPIO) continue;
        if (pin < 0 || pin > 21) continue; // not RTC-capable on S3
        gpio_hold_en(pin);
        gpio_set_level(pin, 0);
    }
}

void ClockApp::onWakeFromLightSleepAsync(void *p)
{
    auto *self = static_cast<ClockApp *>(p);
    if (self == nullptr) {
        return;
    }

    // Wake event itself was the user input. Resync wake_poll edge detectors to
    // the current pin states so the still-held button / finger doesn't look
    // like a fresh press and immediately re-toggle AOD.
    self->_btn_last_level  = gpio_get_level(BTN_WAKE_GPIO);
    self->_btn_prev_stable = self->_btn_last_level;
    self->_btn_stable_cnt  = 0;

    auto *ctx = self->getSystemContext();
    if (ctx != nullptr) {
        lv_indev_t *touch = ctx->getTouchDevice();
        if (touch != nullptr) {
            self->_touch_was_pressed = (touch->state == LV_INDEV_STATE_PRESSED);
        }
    }

    setBacklight(true);
    if (self->isForeground()) {
        self->enterActive();
    } else {
        // Background: just sync internal state, let wake_poll handle inputs.
        self->_state = State::ACTIVE;
        if (self->_tick != nullptr) {
            lv_timer_set_period(self->_tick, 1000);
        }
    }
}

void ClockApp::sleepMonitorTask(void *arg)
{
    auto *self = static_cast<ClockApp *>(arg);
    if (self == nullptr) {
        vTaskDelete(NULL);
        return;
    }

    while (!self->_stop_sleep_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (self->_stop_sleep_task) {
            break;
        }

        // Don't trigger sleep if already sleeping (shouldn't happen, but safe).
        if (self->_state == State::LIGHT_SLEEP) {
            continue;
        }

        // This runs on our own task, so reading LVGL state needs the lock:
        // lv_disp_get_inactive_time() walks the display list while the LVGL
        // task may be rewriting it mid-animation.
        uint32_t idle = 0;
        if (bsp_display_lock(100)) {
            idle = lv_disp_get_inactive_time(NULL);
            bsp_display_unlock();
        } else {
            continue;   // LVGL busy; check again next tick
        }

        // Threshold comes from the Settings app's persistent config
        // (saved on SD as /sdcard/.settings.json, value in ms).
        uint32_t threshold_ms = Settings::cfg.inactivity_delay_ms;
        if (threshold_ms == 0) threshold_ms = LIGHT_SLEEP_IDLE_MS; // safety
        // Honour any active wake-lock (e.g. Flashlight in foreground).
        if (Settings::sleep_locked) continue;
        if (idle >= threshold_ms) {
            // Blocks here until chip wakes. On return, state has been
            // restored (unless chip went to deep sleep, in which case we
            // never get here).
            // self->enterLightSleep();
            self->enterDeepSleep();
        }
    }

    self->_sleep_task = nullptr;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Timer + event callbacks
// ---------------------------------------------------------------------------

void ClockApp::onTickTimer(lv_timer_t *t)
{
    auto *self = static_cast<ClockApp *>(lv_timer_get_user_data(t));
    if (self == nullptr) {
        return;
    }

    self->refreshTime();

    // Only run AOD idle logic when we are the foreground app.
    // If we're in background, another app owns the display, don't dim.
    if (!self->isForeground()) {
        return;
    }

    if (self->_state == State::ACTIVE) {
        uint32_t idle = lv_disp_get_inactive_time(NULL);
        if (idle >= AOD_IDLE_MS && !Settings::sleep_locked) {
            self->enterDimmed();
        }
    }
}

void ClockApp::onWakeTimer(lv_timer_t *t)
{
    auto *self = static_cast<ClockApp *>(lv_timer_get_user_data(t));
    if (self == nullptr) {
        return;
    }

    // ---- 1. Button poll with debounce ----
    int level = gpio_get_level(BTN_WAKE_GPIO);
    if (level == self->_btn_last_level) {
        if (self->_btn_stable_cnt < 4) {
            self->_btn_stable_cnt++;
        }
    } else {
        self->_btn_stable_cnt = 0;
        self->_btn_last_level = level;
    }
    int stable_level = (self->_btn_stable_cnt >= 2) ? self->_btn_last_level : self->_btn_prev_stable;
    bool button_press = (self->_btn_prev_stable == 1 && stable_level == 0);
    self->_btn_prev_stable = stable_level;

    // ---- 2. Touch indev poll (rising edge) ----
    bool touch_press = false;
    auto *ctx = self->getSystemContext();
    if (ctx != nullptr) {
        lv_indev_t *touch = ctx->getTouchDevice();
        if (touch != nullptr) {
            bool now_pressed = (touch->state == LV_INDEV_STATE_PRESSED);
            if (now_pressed && !self->_touch_was_pressed) {
                touch_press = true;
            }
            self->_touch_was_pressed = now_pressed;
        }
    }

    // ---- 3. AXP2101 power key (PEKEY): software-timed long press ----
    // Poll PMU IRQ status every ~200 ms (every 4th 50 ms tick).
    if (s_pmu_inited) {
        if (++self->_pmu_poll_div >= 4) {
            self->_pmu_poll_div = 0;
            s_pmu.getIrqStatus();
            if (s_pmu.isPekeyNegativeIrq()) {
                self->_pkey_press_us = esp_timer_get_time();
            }
            if (s_pmu.isPekeyPositiveIrq()) {
                self->_pkey_press_us = 0;
            }
            s_pmu.clearIrqStatus();
        }
        if (self->_pkey_press_us != 0 &&
            (esp_timer_get_time() - self->_pkey_press_us) >= POWERKEY_LONG_PRESS_US && !Settings::sleep_locked) {
            ESP_UTILS_LOGI("Power key long press -> deep sleep");
            self->_pkey_press_us = 0;
            self->enterDeepSleep();   // does not return
        }
    }

    if (!button_press && !touch_press) {
        return;
    }

    bool fg = self->isForeground();

    if (button_press) {
        ESP_UTILS_LOGI("Button press (fg=%d)", (int)fg);
        if (fg) {
            self->toggleAod();
        } else {
            // Button is dedicated wake key for ClockApp - always re-foreground.
            self->bringToForeground();
        }
    }

    if (touch_press) {
        if (fg) {
            // In foreground: only react when DIMMED so we wake the screen.
            // ACTIVE foreground touches are handled by LVGL natively.
            if (self->_state == State::DIMMED) {
                ESP_UTILS_LOGI("Touch wake (fg dimmed)");
                self->wakeFromAnySource();
            }
        } else {
            // Background: only steal focus when no other app is foreground
            // ("no app is actually listening" = launcher visible).
            if (self->isLauncherVisible()) {
                ESP_UTILS_LOGI("Touch from launcher -> re-foreground ClockApp");
                self->bringToForeground();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ClockApp::configureButton()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << BTN_WAKE_GPIO;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    _btn_last_level  = gpio_get_level(BTN_WAKE_GPIO);
    _btn_prev_stable = _btn_last_level;
}

bool ClockApp::isForeground() const
{
    auto *ctx = getSystemContext();
    if (ctx == nullptr) {
        return false;
    }
    return ctx->getManager().getActiveApp() == this;
}

bool ClockApp::isLauncherVisible() const
{
    auto *ctx = getSystemContext();
    if (ctx == nullptr) {
        return false;
    }
    // No app foreground = launcher / home is showing = no app is "listening".
    return ctx->getManager().getActiveApp() == nullptr;
}

void ClockApp::bringToForeground()
{
    auto *ctx = getSystemContext();
    if (ctx == nullptr) {
        return;
    }
    int id = getId();
    if (id < systems::base::App::APP_ID_MIN) {
        return;
    }
    systems::base::Context::AppEventData ev = {
        .id   = id,
        .type = systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    if (!ctx->sendAppEvent(&ev)) {
        ESP_UTILS_LOGW("Re-foreground send event failed");
    }
}

void ClockApp::refreshTime()
{
    if (_lbl_time == nullptr || _lbl_date == nullptr) {
        return;
    }

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    char buf_time[16];
    char buf_date[32];
    snprintf(buf_time, sizeof(buf_time), "%02d:%02d", ti.tm_hour, ti.tm_min);
    strftime(buf_date, sizeof(buf_date), "%a %d %b", &ti);

    lv_label_set_text(_lbl_time, buf_time);
    lv_label_set_text(_lbl_date, buf_date);

    // Battery percent
    if (_lbl_bat != nullptr) {
        int pct = readBatteryPercent();
        char buf_bat[16];
        if (pct < 0) {
            snprintf(buf_bat, sizeof(buf_bat), "-- %%");
        } else {
            snprintf(buf_bat, sizeof(buf_bat), "%d %%", pct);
        }
        lv_label_set_text(_lbl_bat, buf_bat);
    }

    if (ti.tm_sec == 0) {
        shiftPixels();
    }
}

void ClockApp::shiftPixels()
{
    static const int dx[4] = { 0, 4, 4, 0 };
    static const int dy[4] = { 0, 0, 4, 4 };
    _shift_step = (_shift_step + 1) & 0x03;
    _shift_x = dx[_shift_step];
    _shift_y = dy[_shift_step];

    if (_lbl_time) {
        lv_obj_align(_lbl_time, LV_ALIGN_CENTER, _shift_x, -20 + _shift_y);
    }
    if (_lbl_date) {
        lv_obj_align(_lbl_date, LV_ALIGN_CENTER, _shift_x, 30 + _shift_y);
    }
    if (_lbl_bat) {
        lv_obj_align(_lbl_bat, LV_ALIGN_CENTER, _shift_x, 60 + _shift_y);
    }
}

// ---------------------------------------------------------------------------
// PCF85063A I2C RTC (Waveshare on-board, address 0x51)
//
// Register map (we read/write 0x04..0x0A as a block):
//   0x04 Seconds  : bit7 = OS (oscillator stop -> RTC invalid), bits 6:0 BCD seconds
//   0x05 Minutes  : BCD minutes
//   0x06 Hours    : BCD hours (24h mode, default)
//   0x07 Days     : BCD day-of-month
//   0x08 Weekdays : 0..6
//   0x09 Months   : BCD month (1..12)
//   0x0A Years    : BCD year (0..99 => 2000..2099)
// ---------------------------------------------------------------------------

static inline uint8_t bcd_to_bin(uint8_t v) { return (v & 0x0F) + ((v >> 4) & 0x0F) * 10; }
static inline uint8_t bin_to_bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtc_open_dev(i2c_master_dev_handle_t &dev, uint8_t addr)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == nullptr) {
        return false;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address  = addr;
    cfg.scl_speed_hz    = 100000;
    return i2c_master_bus_add_device(bus, &cfg, &dev) == ESP_OK;
}

bool ClockApp::readRtcTime(struct tm &out)
{
    i2c_master_dev_handle_t dev = nullptr;
    if (!rtc_open_dev(dev, RTC_I2C_ADDR)) {
        ESP_UTILS_LOGW("RTC: open device failed");
        return false;
    }

    uint8_t reg = 0x04;
    uint8_t buf[7] = {};
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), 100);
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) {
        ESP_UTILS_LOGW("RTC: I2C read err=%d", (int)err);
        return false;
    }

    // OS bit (oscillator stop) = power was lost, time is invalid.
    if (buf[0] & 0x80) {
        ESP_UTILS_LOGW("RTC: oscillator-stop flag set, time invalid");
        return false;
    }

    out = {};
    out.tm_sec  = bcd_to_bin(buf[0] & 0x7F);
    out.tm_min  = bcd_to_bin(buf[1] & 0x7F);
    out.tm_hour = bcd_to_bin(buf[2] & 0x3F);
    out.tm_mday = bcd_to_bin(buf[3] & 0x3F);
    out.tm_wday = buf[4] & 0x07;
    out.tm_mon  = bcd_to_bin(buf[5] & 0x1F) - 1;     // tm_mon 0..11
    out.tm_year = bcd_to_bin(buf[6]) + 100;          // tm_year since 1900; PCF85063 2000+
    out.tm_isdst = -1;

    return true;
}

bool ClockApp::writeRtcTime(const struct tm &in)
{
    i2c_master_dev_handle_t dev = nullptr;
    if (!rtc_open_dev(dev, RTC_I2C_ADDR)) {
        return false;
    }

    // Block write: register + 7 bytes
    uint8_t pkt[1 + 7];
    pkt[0] = 0x04;
    pkt[1] = bin_to_bcd((uint8_t)in.tm_sec)  & 0x7F; // clears OS bit
    pkt[2] = bin_to_bcd((uint8_t)in.tm_min)  & 0x7F;
    pkt[3] = bin_to_bcd((uint8_t)in.tm_hour) & 0x3F;
    pkt[4] = bin_to_bcd((uint8_t)in.tm_mday) & 0x3F;
    pkt[5] = (uint8_t)(in.tm_wday & 0x07);
    pkt[6] = bin_to_bcd((uint8_t)(in.tm_mon + 1)) & 0x1F;
    int year2k = in.tm_year - 100;
    if (year2k < 0)  year2k = 0;
    if (year2k > 99) year2k = 99;
    pkt[7] = bin_to_bcd((uint8_t)year2k);

    esp_err_t err = i2c_master_transmit(dev, pkt, sizeof(pkt), 100);
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) {
        ESP_UTILS_LOGW("RTC: I2C write err=%d", (int)err);
        return false;
    }
    return true;
}

bool ClockApp::syncTimeFromRtc()
{
    struct tm rtc_time;
    if (!readRtcTime(rtc_time)) {
        // RTC dead or never set. Seed it with the firmware compile time so
        // the clock at least shows a sane value, then write back to RTC.
        ESP_UTILS_LOGW("RTC: invalid -> seeding from build time");
        struct tm seed = {};
        char mon_str[4] = {};
        int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
        // __DATE__ format: "Mmm dd yyyy"
        sscanf(__DATE__, "%3s %d %d", mon_str, &day, &year);
        sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
        const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *p = strstr(mons, mon_str);
        int mon = (p != nullptr) ? (int)((p - mons) / 3) : 0;
        seed.tm_year = year - 1900;
        seed.tm_mon  = mon;
        seed.tm_mday = day;
        seed.tm_hour = hh;
        seed.tm_min  = mm;
        seed.tm_sec  = ss;
        seed.tm_isdst = -1;
        rtc_time = seed;
        writeRtcTime(seed); // best effort
    }

    time_t t = mktime(&rtc_time);
    if (t < 0) {
        return false;
    }
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    ESP_UTILS_LOGI("RTC sync OK: %04d-%02d-%02d %02d:%02d:%02d",
                   rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
                   rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
    return true;
}

// ---------------------------------------------------------------------------
// Battery monitoring via AXP2101 PMIC (XPowersLib)
//
// AXP2101 lives at I2C 0x34. PMU singleton + register r/w callbacks are
// defined at the top of this file so other helpers (e.g. prepareDeepSleepPower)
// can reach them.
// ---------------------------------------------------------------------------

bool ClockApp::initBatteryMonitor()
{
    if (_pmu_ready) {
        return true;
    }

    // Add AXP2101 as a device on the BSP shared I2C bus.0
    if (s_pmu_dev == nullptr) {
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (bus == nullptr) {
            ESP_UTILS_LOGW("PMU: I2C bus handle null");
            return false;
        }
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address  = PMU_I2C_ADDR;
        cfg.scl_speed_hz    = 400000;
        if (i2c_master_bus_add_device(bus, &cfg, &s_pmu_dev) != ESP_OK) {
            ESP_UTILS_LOGW("PMU: add device failed");
            s_pmu_dev = nullptr;
            return false;
        }
    }

    if (!s_pmu_inited) {
        if (!s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_reg_read, pmu_reg_write)) {
            ESP_UTILS_LOGW("PMU: begin failed");
            return false;
        }
        // Minimal init: just enable the measurements we read. Don't touch
        // power rails so we don't disturb the rest of the board.
        s_pmu.enableBattVoltageMeasure();
        s_pmu.enableSystemVoltageMeasure();

        // Enable power-key press / release IRQs so we can software-time the
        // long press for deep-sleep trigger.
        s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
                        XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
        s_pmu.clearIrqStatus();
        s_pmu_inited = true;

        ESP_UTILS_LOGI("PMU ready: vbat=%dmV, batt=%d%%",
                       (int)s_pmu.getBattVoltage(),
                       (int)s_pmu.getBatteryPercent());
    }

    _pmu_ready = true;
    return true;
}

void ClockApp::deinitBatteryMonitor()
{
    // Keep the static PMU around for other consumers, but mark this instance
    // as unbound. Removing the device handle is optional; we leave it
    // allocated since the PMU is a singleton tied to the lifetime of the app.
    _pmu_ready = false;
}

int ClockApp::readBatteryPercent()
{
    if (!_pmu_ready || !s_pmu_inited) {
        return -1;
    }
    if (!s_pmu.isBatteryConnect()) {
        // Running on USB / no battery: report 100% so user sees something
        // sensible (or change to -1 to show "-- %" when no pack is present).
        return -1;
    }
    int pct = (int)s_pmu.getBatteryPercent();
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}


// ---------------------------------------------------------------------------
// "Set Clock" pull-down panel
// ---------------------------------------------------------------------------

namespace {
struct FieldDef { const char *name; int min; int max; };
static const FieldDef kFields[7] = {
    { "HH", 0, 23 },   // hour
    { "mm", 0, 59 },   // minute
    { "ss", 0, 59 },   // second
    { "DD", 0,  6 },   // weekday (0=Sun..6=Sat)
    { "dd", 1, 31 },   // day of month
    { "MM", 1, 12 },   // month
    { "YY", 0, 99 },   // year (2000..2099)
};
static const char *kWday[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

// Format helper: weekday gets named, others numeric.
static void format_field_value(int idx, int v, char *buf, size_t bufsz)
{
    if (idx == 3) snprintf(buf, bufsz, "%s", kWday[v % 7]);
    else          snprintf(buf, bufsz, "%02d", v);
}

// User data passed to +/- button events: high 4 bits = field index,
// low 4 bits = direction (0=minus, 1=plus). Encoded as void* via uintptr.
static inline void *encode_btn(int field, int dir) {
    return reinterpret_cast<void *>((uintptr_t)((field << 1) | (dir & 1)));
}
static inline int decode_field(void *p) { return (int)((uintptr_t)p >> 1) & 0x07; }
static inline int decode_dir(void *p)   { return (int)((uintptr_t)p) & 0x01; }
} // anonymous namespace

void ClockApp::onSettingsBtn(lv_event_t *e)
{
    auto *self = static_cast<ClockApp *>(lv_event_get_user_data(e));
    void *raw  = lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    int field  = decode_field(raw);
    int dir    = decode_dir(raw);
    int &v     = self->_settings_values[field];
    int lo     = kFields[field].min;
    int hi     = kFields[field].max;
    int span   = hi - lo + 1;
    v = lo + ((v - lo + (dir ? 1 : (span - 1))) % span);

    char buf[8];
    format_field_value(field, v, buf, sizeof(buf));
    lv_label_set_text(self->_settings_value_lbl[field], buf);
}

void ClockApp::onSettingsSet(lv_event_t *e)
{
    auto *self = static_cast<ClockApp *>(lv_event_get_user_data(e));
    self->applySettingsPanel();
}

void ClockApp::onScreenGesture(lv_event_t *e)
{
    auto *self = static_cast<ClockApp *>(lv_event_get_user_data(e));
    if (self == nullptr) return;
    lv_indev_t *ind = lv_indev_active();
    if (ind == nullptr) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(ind);
    if (dir == LV_DIR_BOTTOM) {
        self->showSettingsPanel();
    } else if (dir == LV_DIR_TOP) {
        self->hideSettingsPanel();
    }
}

void ClockApp::buildSettingsPanel()
{
    if (_settings_panel != nullptr || _screen == nullptr) return;

    _settings_panel = lv_obj_create(_screen);
    lv_obj_set_size(_settings_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(_settings_panel, 0, 0);
    lv_obj_set_style_bg_color(_settings_panel, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(_settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_settings_panel, 0, 0);
    lv_obj_set_style_pad_all(_settings_panel, 6, 0);
    lv_obj_set_flex_flow(_settings_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_settings_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(_settings_panel, LV_OBJ_FLAG_HIDDEN);

    // Title
    lv_obj_t *title = lv_label_create(_settings_panel);
    lv_label_set_text(title, "Set Clock");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    // Hint
    lv_obj_t *hint = lv_label_create(_settings_panel);
    lv_label_set_text(hint, "Swipe up to close");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x707070), 0);

    // 7 rows: label | value | minus | plus
    for (int i = 0; i < FIELD_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(_settings_panel);
        lv_obj_set_size(row, lv_pct(95), 44);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, kFields[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_width(lbl, 50);

        _settings_value_lbl[i] = lv_label_create(row);
        lv_label_set_text(_settings_value_lbl[i], "--");
        lv_obj_set_style_text_color(_settings_value_lbl[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(_settings_value_lbl[i], &lv_font_montserrat_18, 0);
        lv_obj_set_width(_settings_value_lbl[i], 70);

        lv_obj_t *btn_minus = lv_btn_create(row);
        lv_obj_set_size(btn_minus, 36, 36);
        lv_obj_set_user_data(btn_minus, encode_btn(i, 0));
        lv_obj_t *m = lv_label_create(btn_minus);
        lv_label_set_text(m, "-");
        lv_obj_center(m);
        lv_obj_add_event_cb(btn_minus, &ClockApp::onSettingsBtn, LV_EVENT_CLICKED, this);

        lv_obj_t *btn_plus = lv_btn_create(row);
        lv_obj_set_size(btn_plus, 36, 36);
        lv_obj_set_user_data(btn_plus, encode_btn(i, 1));
        lv_obj_t *p = lv_label_create(btn_plus);
        lv_label_set_text(p, "+");
        lv_obj_center(p);
        lv_obj_add_event_cb(btn_plus, &ClockApp::onSettingsBtn, LV_EVENT_CLICKED, this);
    }

    // Set button
    lv_obj_t *btn_set = lv_btn_create(_settings_panel);
    lv_obj_set_size(btn_set, 120, 44);
    lv_obj_t *sl = lv_label_create(btn_set);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);
    lv_obj_add_event_cb(btn_set, &ClockApp::onSettingsSet, LV_EVENT_CLICKED, this);
}

void ClockApp::showSettingsPanel()
{
    if (_settings_panel == nullptr) buildSettingsPanel();
    if (_settings_panel == nullptr) return;

    // Seed values from current system time.
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    _settings_values[0] = ti.tm_hour;
    _settings_values[1] = ti.tm_min;
    _settings_values[2] = ti.tm_sec;
    _settings_values[3] = ti.tm_wday;
    _settings_values[4] = ti.tm_mday;
    _settings_values[5] = ti.tm_mon + 1;
    _settings_values[6] = ti.tm_year - 100;
    char buf[8];
    for (int i = 0; i < FIELD_COUNT; i++) {
        format_field_value(i, _settings_values[i], buf, sizeof(buf));
        lv_label_set_text(_settings_value_lbl[i], buf);
    }

    lv_obj_clear_flag(_settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_settings_panel);
}

void ClockApp::hideSettingsPanel()
{
    if (_settings_panel == nullptr) return;
    lv_obj_add_flag(_settings_panel, LV_OBJ_FLAG_HIDDEN);
}

void ClockApp::applySettingsPanel()
{
    struct tm ti = {};
    ti.tm_hour  = _settings_values[0];
    ti.tm_min   = _settings_values[1];
    ti.tm_sec   = _settings_values[2];
    ti.tm_wday  = _settings_values[3];
    ti.tm_mday  = _settings_values[4];
    ti.tm_mon   = _settings_values[5] - 1;
    ti.tm_year  = _settings_values[6] + 100;
    ti.tm_isdst = -1;

    // Push to the on-board RTC chip so it survives reboot / deep sleep.
    if (writeRtcTime(ti)) {
        ESP_UTILS_LOGI("RTC updated from panel");
    }

    // Push to system time so refreshTime() picks it up immediately.
    time_t t = mktime(&ti);
    if (t > 0) {
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }

    refreshTime();
    hideSettingsPanel();
}

// Self-register with the app registry.
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, ClockApp, APP_NAME, []()
{
    return std::shared_ptr<ClockApp>(ClockApp::requestInstance(), [](ClockApp *p) {});
})

} // namespace esp_brookesia::apps
