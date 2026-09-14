#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class Podometer : public systems::phone::App {
public:
    static Podometer *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Podometer();

    bool init()   override;
    bool deinit() override;
    bool run()    override;
    bool back()   override;
    bool close()  override;

    // Public so main.cpp / any external caller can trigger ULP-backed deep sleep.
    static void enterDeepSleepWithUlp();
    // Read latest NVS-persisted step count. Used by UI / autostart logic.
    static int32_t loadPersistedSteps();
    // True if last wake came from this app's ULP / button path (either one).
    static bool wokeFromPodometer();

    // True only when the ULP woke us to hand over a full step batch. A button
    // press reports false: that is the user asking for the watch face.
    // Neither affects counting -- the counter runs regardless of foreground app.
    static bool wokeFromUlpBatch();

    // Fast path for a ULP wake: fold the batch into NVS and go straight back
    // to deep sleep, without ever powering the display or building any UI.
    //
    // Call early in app_main, after releaseSleepPins() and bsp_i2c_init() but
    // before the display is started. Does not return when it takes the fast
    // path. Returns false when this was not a ULP wake (or the user is holding
    // the button), meaning the caller should carry on booting normally.
    static bool handleUlpWakeAndSleep();

    // Start step counting without opening the app. Idempotent, returns
    // immediately; all the slow work happens on a background task. Called
    // from init(), and callable directly from app_main to begin counting
    // before the app registry is even walked.
    static void startBackgroundService();

    // Return the pins the ULP borrowed for deep sleep to the digital IO mux.
    // MUST be called early in app_main, before anything uses I2C: while
    // those pads sit in the RTC mux the touch panel, PMU and IMU are all
    // unreachable. Safe to call on a cold boot where nothing was borrowed.
    static void releaseSleepPins();

protected:
    Podometer(bool use_status_bar, bool use_navigation_bar);

private:
    static void backgroundInit(void *arg);

    static Podometer *_instance;
};

} // namespace esp_brookesia::apps
