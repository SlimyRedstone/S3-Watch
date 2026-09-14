#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esp_brookesia::apps {

class Jamy : public systems::phone::App {
public:
    static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_0;

    static Jamy *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Jamy();

    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;
    bool close()  override;

protected:
    Jamy(bool use_status_bar, bool use_navigation_bar);

private:
    static void onPollTimer(lv_timer_t *t);
    static void floodTask(void *arg);
    void startFlood();
    void stopFlood();
    void refreshUi(bool flooding);

    static Jamy *_instance;

    lv_obj_t   *_lbl_title  = nullptr;
    lv_obj_t   *_lbl_status = nullptr;
    lv_timer_t *_poll       = nullptr;

    int  _btn_last   = 1;
    int  _btn_stable = 0;
    int  _btn_prev   = 1;

    TaskHandle_t  _flood_task   = nullptr;
    volatile bool _flood_run    = false;
    bool          _wifi_started = false;
};

} // namespace esp_brookesia::apps
