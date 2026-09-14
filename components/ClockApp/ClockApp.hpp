/*
 * ClockApp - Always On Display style clock for ESP-Brookesia phone system.
 *
 * Power state ladder (idle time = lv_disp_get_inactive_time):
 *   t  <  AOD_IDLE_MS         -> ACTIVE      (backlight on, 1 Hz tick)
 *   t >=  AOD_IDLE_MS         -> DIMMED      (backlight off, 1/min tick)
 *   t >=  LIGHT_SLEEP_IDLE_MS -> LIGHT_SLEEP (chip in light sleep, GPIO wake)
 *   t >=  DEEP_SLEEP_IDLE_MS  -> deep sleep  (chip resets on wake)
 *
 * Wake sources:
 *   ACTIVE/DIMMED            -> touch indev or button GPIO (poll timer @ 50ms)
 *   LIGHT_SLEEP              -> GPIO wakeup on BTN_WAKE_GPIO and TOUCH_INT_GPIO
 *   DEEP_SLEEP               -> EXT1 wakeup on BTN_WAKE_GPIO only
 *                                (TOUCH_INT_GPIO=38 is not RTC-capable on S3)
 *
 * Notes on deep sleep:
 *   - Chip fully resets. main.cpp re-runs and ClockApp auto-starts again.
 *   - System time (TZ + offset since epoch) is restored from the on-board
 *     PCF85063A I2C RTC (0x51) at boot via syncTimeFromRtc().
 */
#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Custom LVGL fonts MUST be declared at global C linkage. The font .c file
// defines the symbol as a plain C `const lv_font_t`, but if LV_FONT_DECLARE
// sits inside a C++ namespace it expands to `extern const lv_font_t ...;`
// in that namespace, producing a mangled name the linker can't resolve.
// Wrap with extern "C" and keep it OUTSIDE any namespace.
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(jetbrains_96_1bpp);
LV_FONT_DECLARE(jetbrains_80_1bpp);
LV_FONT_DECLARE(jetbrains_60_1bpp);
LV_FONT_DECLARE(jetbrains_48_1bpp);
#ifdef __cplusplus
}
#endif

namespace esp_brookesia::apps {

class ClockApp : public systems::phone::App {
public:
    // Idle ms thresholds for each power state.
    static constexpr uint32_t AOD_IDLE_MS         = 5 * 1000;
    static constexpr uint32_t LIGHT_SLEEP_IDLE_MS = 15 * 1000;
    static constexpr uint32_t DEEP_SLEEP_IDLE_MS  = 60 * 1000;

    // GPIO used as wake button. GPIO0 = BOOT button on most ESP32-S3 boards.
    // Active LOW (pulled up). RTC-capable -> works for deep sleep wake.
    static constexpr gpio_num_t BTN_WAKE_GPIO = GPIO_NUM_0;

    // Touch controller interrupt line. BSP_LCD_TOUCH_INT on Waveshare AMOLED 2.06.
    // Active LOW. Not RTC-capable on S3 -> works only for light sleep wake.
    static constexpr gpio_num_t TOUCH_INT_GPIO = GPIO_NUM_38;

    // PCF85063A I2C address (battery-backed RTC on the Waveshare board).
    static constexpr uint8_t RTC_I2C_ADDR = 0x51;

    // AXP2101 PMIC I2C address. Battery percent is read via XPowersLib.
    static constexpr uint8_t PMU_I2C_ADDR = 0x34;

    static ClockApp *requestInstance(
        bool use_status_bar = false, bool use_navigation_bar = false);
    ~ClockApp();

    bool init()   override;
    bool deinit() override;
    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;
    bool close()  override;

    // Optional override for how the watch sleeps.
    //
    // The default is a full AXP2101 shutdown: every rail is cut, including
    // the one feeding the ESP32-S3. That is the lowest-power state possible,
    // but it also kills the RTC domain, so the ULP co-processor stops and
    // anything in RTC memory is lost. A handler installed here is called
    // instead and is expected never to return -- the Podometer installs one
    // that uses real ESP32-S3 deep sleep so it can keep counting steps.
    //
    // Set to nullptr to go back to the power-off behaviour.
    using DeepSleepHandler = void (*)();
    static void setDeepSleepHandler(DeepSleepHandler handler);

    // Backlight control that holds the LVGL lock while it talks to the panel.
    // ALWAYS use this instead of bsp_display_backlight_on/off(): the backlight
    // command shares the QSPI panel-IO handle (and its transaction pool) with
    // the LVGL flush, and that handle has no locking of its own.
    static void setBacklight(bool on);

    // Put the panel itself to sleep (DISPOFF + SLPIN), not just blank it.
    // Holds the LVGL lock, since these are commands on the QSPI handle the
    // flush also uses. No wake counterpart: leaving deep sleep is a reboot and
    // the BSP hardware-resets the panel during init.
    static void displayEnterSleep();

    // Drop the named AXP2101 rails before deep sleep, to cut standby current.
    //
    // drop_mask is an explicit allow-list and defaults to zero (drop nothing).
    // That default is deliberate and must stay: cutting a rail is a one-way
    // door. If the rail being cut also feeds the I2C pull-ups or the AXP2101's
    // own interface, the ESP32-S3 can never reach the PMU again to undo it --
    // the watch wakes with a dead screen and no way to recover in software.
    // Only the power key (long press, PMU reset to defaults) gets it back.
    //
    // DC1 is never touched: it feeds the ESP32-S3 and the QMI8658.
    //
    // Also differs from prepareDeepSleepPower() in never holding the I2C pads,
    // since a held pad cannot be bit-banged by the ULP.
    enum RailMask : uint32_t {
        RAIL_NONE    = 0,
        RAIL_DC2     = 1u << 0,
        RAIL_DC3     = 1u << 1,
        RAIL_DC4     = 1u << 2,
        RAIL_DC5     = 1u << 3,
        RAIL_ALDO1   = 1u << 4,
        RAIL_ALDO2   = 1u << 5,
        RAIL_ALDO3   = 1u << 6,
        RAIL_ALDO4   = 1u << 7,
        RAIL_BLDO1   = 1u << 8,
        RAIL_BLDO2   = 1u << 9,
        RAIL_CPUSLDO = 1u << 10,
        RAIL_DLDO1   = 1u << 11,
        RAIL_DLDO2   = 1u << 12,
    };
    static void powerDownForUlpSleep(uint32_t drop_mask);

    // Restore the rails powerDownForUlpSleep() dropped. AXP2101 registers
    // survive an ESP32-S3 deep sleep, so without this the display and touch
    // rails stay off and the screen never comes back. Call early in app_main,
    // after bsp_i2c_init() and before the display is brought up. No-op on a
    // cold boot.
    static void powerUpAfterUlpSleep();

protected:
    ClockApp(bool use_status_bar, bool use_navigation_bar);

private:
    enum class State : uint8_t {
        ACTIVE = 0,
        DIMMED,
        LIGHT_SLEEP,
    };

    static void onTickTimer(lv_timer_t *t);
    static void onWakeTimer(lv_timer_t *t);
    static void sleepMonitorTask(void *arg);
    static void onWakeFromLightSleepAsync(void *p);

    void refreshTime();
    void shiftPixels();
    void enterActive();
    void enterDimmed();
    void enterLightSleep();
    void enterDeepSleep();
    void prepareDeepSleepPower();
    void wakeFromAnySource();
    void toggleAod();
    void configureButton();
    bool isForeground() const;
    bool isLauncherVisible() const;
    void bringToForeground();

    // RTC (PCF85063A on I2C 0x51)
    bool readRtcTime(struct tm &out);
    bool writeRtcTime(const struct tm &in);
    bool syncTimeFromRtc();

    // Pull-down "Set Clock" panel (swipe down from top inside ClockApp).
    void buildSettingsPanel();
    void showSettingsPanel();
    void hideSettingsPanel();
    void applySettingsPanel();
    static void onScreenGesture(lv_event_t *e);
    static void onSettingsBtn(lv_event_t *e);
    static void onSettingsSet(lv_event_t *e);

    // Battery (AXP2101 PMIC via XPowersLib)
    bool initBatteryMonitor();
    void deinitBatteryMonitor();
    int  readBatteryPercent(); // 0..100, or -1 on error

    static ClockApp *_instance;
    static DeepSleepHandler _deep_sleep_handler;
    static bool ensurePmuStatic();

    // UI
    lv_obj_t   *_screen     = nullptr;
    lv_obj_t   *_lbl_time   = nullptr;
    lv_obj_t   *_lbl_date   = nullptr;
    lv_obj_t   *_lbl_bat    = nullptr;
    lv_timer_t *_tick       = nullptr;   // 1s clock + idle check
    lv_timer_t *_wake_poll  = nullptr;   // 50ms button poll

    // PMU init guard. Actual XPowersPMU instance lives as a static in the .cpp
    // so XPowersLib templates don't bleed into this header.
    bool _pmu_ready = false;

    // Settings panel state. Field index map:
    //   0=hour 1=min 2=sec 3=wday 4=mday 5=mon 6=year (2000+)
    static constexpr int FIELD_COUNT = 7;
    lv_obj_t *_settings_panel             = nullptr;
    lv_obj_t *_settings_value_lbl[FIELD_COUNT] = {};
    int       _settings_values[FIELD_COUNT]    = {};

    // State
    State _state = State::ACTIVE;

    // Button debounce
    int  _btn_last_level   = 1;          // pulled up
    int  _btn_stable_cnt   = 0;
    int  _btn_prev_stable  = 1;

    // Touch press edge detection
    bool _touch_was_pressed = false;

    // AXP2101 power-key (PEKEY) software-timed long press detector.
    // Long press of POWERKEY_LONG_PRESS_MS = trigger deep sleep.
    static constexpr int64_t POWERKEY_LONG_PRESS_US = 1500 * 1000; // 1.5 s
    int64_t _pkey_press_us = 0;     // 0 = not pressed; else timestamp of press
    int     _pmu_poll_div  = 0;     // tick divider (poll PMU IRQ slower than 50ms)

    // Sleep monitor task
    TaskHandle_t   _sleep_task       = nullptr;
    volatile bool  _stop_sleep_task  = false;

    // Burn-in protect offsets
    int _shift_x    = 0;
    int _shift_y    = 0;
    int _shift_step = 0;
};

} // namespace esp_brookesia::apps
