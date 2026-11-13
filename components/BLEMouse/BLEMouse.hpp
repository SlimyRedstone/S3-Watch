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

    // Touch gestures on the pad.
    static constexpr int64_t    TAP_MAX_US        = 250 * 1000; // press shorter than this ...
    static constexpr int        TAP_MAX_MOVE_PX   = 12;         // ... and moving less than this = left click
    static constexpr int        SCROLL_STEP_PX    = 10;         // two-finger travel per wheel notch
    static constexpr int        SCROLL_JUMP_PX    = 360;         // larger jump = finger swap, ignore
    // Scroll strip: a one-finger drag that starts in the rightmost SCROLL_STRIP_PX
    // of the pad scrolls instead of moving the cursor. Works even if the touch
    // controller only reports one finger (FT3168 is self-capacitive; its 2-point
    // mode is a firmware option, see read_touch()).
    static constexpr int        SCROLL_STRIP_PX   = 48;

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
    lv_obj_t   *_strip       = nullptr;   // visual hint for the scroll strip (not clickable)
    lv_obj_t   *_lbl_status  = nullptr;
    lv_timer_t *_poll        = nullptr;

    lv_point_t _last_point   = {};
    lv_point_t _start_point  = {};
    bool       _has_last     = false;
    bool       _edge_swipe   = false;   // candidate for back-to-launcher gesture

    // Gesture classification for the current press.
    int64_t    _press_us     = 0;       // esp_timer time at LV_EVENT_PRESSED
    int        _move_total   = 0;       // |dx|+|dy| accumulated since press (tap rejection)
    bool       _two_finger   = false;   // latched once a 2nd finger is seen: scroll mode
    int        _scroll_accum = 0;       // raw px since the last wheel notch
    int        _scroll_last_y = -1;     // mean Y of all fingers at the previous sample (-1 = none)
    bool       _strip_scroll = false;   // press started in the right-edge scroll strip
    int        _strip_last_y = 0;       // LVGL point Y at the previous sample (strip mode)

    int        _btn_last     = 1;
    int        _btn_stable   = 0;
    int        _btn_prev     = 1;
    int64_t    _btn_press_us = 0;
    bool       _btn_armed    = false;
};

} // namespace esp_brookesia::apps
