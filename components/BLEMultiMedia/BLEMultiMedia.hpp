#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class BLEMultiMedia : public systems::phone::App {
public:
    static BLEMultiMedia *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~BLEMultiMedia();

    bool run()  override;
    bool back() override;
    bool pause()  override;
    bool resume() override;
    bool close() override;

protected:
    BLEMultiMedia(bool use_status_bar, bool use_navigation_bar);

private:
    static BLEMultiMedia *_instance;
};

} // namespace esp_brookesia::apps
