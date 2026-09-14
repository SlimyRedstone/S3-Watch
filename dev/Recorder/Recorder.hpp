#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class Recorder : public systems::phone::App {
public:
    static Recorder *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Recorder();

    bool run()  override;
    bool back() override;
    bool close() override;

protected:
    Recorder(bool use_status_bar, bool use_navigation_bar);

private:
    static Recorder *_instance;
};

} // namespace esp_brookesia::apps
