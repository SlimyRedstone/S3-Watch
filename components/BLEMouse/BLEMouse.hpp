#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/gpio.h"

namespace esp_brookesia::apps {

class BLEMouse : public systems::phone::App {
public:
    static constexpr gpio_num_t BTN_GPIO          = GPIO_NUM_0;
    static constexpr int64_t    LONG_PRESS_US     = 500 * 1000;
    static constexpr float      MOVE_GAIN         = 1.5f;

    static BLEMouse *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~BLEMouse();

    bool init()   override;
    bool deinit() override;
    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;
    bool close()  override;

    static void onConnectionChanged(bool connected);

protected:
    BLEMouse(bool use_status_bar, bool use_navigation_bar);

private:
    static void onPadPress(lv_event_t *e);
    static void onPadPressing(lv_event_t *e);
    static void onPadRelease(lv_event_t *e);
    static void onPollTimer(lv_timer_t *t);
    void refreshStatusIcon();

    static BLEMouse *_instance;

    lv_obj_t   *_pad         = nullptr;
    lv_obj_t   *_lbl_status  = nullptr;
    lv_timer_t *_poll        = nullptr;

    lv_point_t _last_point   = {};
    lv_point_t _start_point  = {};
    bool       _has_last     = false;
    bool       _edge_swipe   = false;   // candidate for back-to-launcher gesture

    int        _btn_last     = 1;
    int        _btn_stable   = 0;
    int        _btn_prev     = 1;
    int64_t    _btn_press_us = 0;
    bool       _btn_armed    = false;
};

} // namespace esp_brookesia::apps
