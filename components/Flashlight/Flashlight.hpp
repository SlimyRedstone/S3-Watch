#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class Flashlight : public systems::phone::App {
public:
    static Flashlight *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Flashlight();

    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;
    bool close()  override;

protected:
    Flashlight(bool use_status_bar, bool use_navigation_bar);

private:
    static Flashlight *_instance;
};

} // namespace esp_brookesia::apps
