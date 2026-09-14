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
    // True if last wake came from this app's ULP / button path.
    static bool wokeFromPodometer();

protected:
    Podometer(bool use_status_bar, bool use_navigation_bar);

private:
    static Podometer *_instance;
};

} // namespace esp_brookesia::apps
