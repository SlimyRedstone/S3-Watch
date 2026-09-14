/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "boost/thread.hpp"
// #include "boost/thread/thread.hpp"
#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "ClockApp.hpp"
#include "Podometer.hpp"
#ifdef ESP_UTILS_LOG_TAG
	#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "./dark/stylesheet.hpp"
#include "esp_lib_utils.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

constexpr bool EXAMPLE_SHOW_MEM_INFO = true;

extern "C" void app_main(void) {
	
	
	ESP_LOGI("TIMING","t_app_main=%lld us", esp_timer_get_time());


	ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

	// Hand the pads the ULP borrowed for deep-sleep step counting back to the
	// digital IO mux. Must happen before anything touches I2C, otherwise the
	// touch panel, the PMU and the IMU are all still stuck in the RTC mux.
	esp_brookesia::apps::Podometer::releaseSleepPins();

	// Bring the shared I2C bus up here, on this task. bsp_i2c_init() guards
	// itself with a plain bool and no mutex, so two tasks racing into it would
	// end up creating the bus twice.
	bsp_i2c_init();

	// If the ULP woke us only to hand over a full step batch, fold it into NVS
	// and go straight back to sleep. Never returns in that case, so the display
	// is never powered and the user never sees a flash of UI at 3am. A button
	// press instead falls through and boots normally.
	esp_brookesia::apps::Podometer::handleUlpWakeAndSleep();

	// Dynamic frequency scaling: run at 240 MHz when there is work, drop to the
	// 40 MHz XTAL whenever the CPU is idle. The idle task does the switching,
	// and drivers that care (SPI, I2C) take a power-management lock so their
	// transfers still see a stable APB clock.
	//
	// Note this does NOT apply to deep sleep, where the CPU is powered off
	// entirely and there is no clock to scale. It pays off in the awake-but-idle
	// stretches: the dimmed/AOD state and the inactivity window before sleep.
	{
		esp_pm_config_t pm_cfg = {
			.max_freq_mhz				= 240,
			.min_freq_mhz				= 40,
			// Automatic light sleep in the idle task. Left off: the LVGL task
			// wakes every task_max_sleep_ms (5 ms) so there is little idle time
			// to exploit, and it costs UART characters on every entry/exit.
			.light_sleep_enable = false,
		};
		esp_err_t pm_err = esp_pm_configure(&pm_cfg);
		if (pm_err != ESP_OK) {
			ESP_UTILS_LOGW("DFS config failed (%s)", esp_err_to_name(pm_err));
		} else {
			ESP_UTILS_LOGI("DFS on: %d..%d MHz", pm_cfg.min_freq_mhz, pm_cfg.max_freq_mhz);
		}
	}

	// If we came out of a ULP-backed deep sleep, the AXP2101 rails feeding the
	// panel and touch are still switched off -- their registers survived the
	// sleep. Put them back before the display is brought up. No-op on a cold
	// boot.
	esp_brookesia::apps::ClockApp::powerUpAfterUlpSleep();

	bsp_display_cfg_t cfg = {
		.lvgl_port_cfg = {                                 
			.task_priority		= 2,         
			.task_stack			= 16 * 1024, 
			.task_affinity		= 1,        
			.task_max_sleep_ms 	= 5,       
			.timer_period_ms	= 1,         
		}
	};
	ESP_UTILS_CHECK_NULL_EXIT( bsp_display_start_with_config(&cfg), "Start display failed");
	
	ESP_LOGI("TIMING","t_display=%lld us", esp_timer_get_time());

	// Start counting steps now, in the background. Runs whether or not the
	// user ever opens the Podometer app. Placed after the display init because
	// that is what brings LVGL up, and the counter reports step changes to the
	// UI -- starting it earlier meant it could call into LVGL before lv_init().
	esp_brookesia::apps::Podometer::startBackgroundService();

	// Keep backlight OFF here. We turn it on at the very end, after ClockApp
	// has built its UI, so the user never sees the empty launcher.
	// Via setBacklight() so it holds the LVGL lock: the backlight command
	// shares the QSPI panel-IO handle with the flush, and the LVGL task is
	// already running by now.
	esp_brookesia::apps::ClockApp::setBacklight(false);

	/* Configure GUI lock */
	LvLock::registerCallbacks(
			[](int timeout_ms) {
				if (timeout_ms < 0) {
					timeout_ms = 0;
				} else if (timeout_ms == 0) {
					timeout_ms = 1;
				}
				ESP_UTILS_CHECK_FALSE_RETURN(
						bsp_display_lock(timeout_ms), false, "Lock failed");

				return true;
			},
			[]() {
				bsp_display_unlock();

				return true;
			});

	/* Create a phone object */
	Phone *phone = new (std::nothrow) Phone();
	ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

	/* Try using a stylesheet that corresponds to the resolution */
	if ((BSP_LCD_H_RES == 410) && (BSP_LCD_V_RES == 502)) {
		Stylesheet *stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_410_502_DARK);
		ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

		ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
		ESP_UTILS_CHECK_FALSE_EXIT( phone->addStylesheet(stylesheet), "Add stylesheet failed");
		ESP_UTILS_CHECK_FALSE_EXIT( phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
		delete stylesheet;
	}

	{
		// When operating on non-GUI tasks, should acquire a lock before operating
		// on LVGL
		LvLockGuard gui_guard;

		/* Begin the phone */
		ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
		
		ESP_LOGI("TIMING","t_phone=%lld us", esp_timer_get_time());
		// assert(phone->getDisplay().showContainerBorder() && "Show container
		// border failed");

		/* Init and install apps from registry */
		std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
		ESP_UTILS_CHECK_FALSE_EXIT( phone->initAppFromRegistry(inited_apps), "Init app registry failed");
		ESP_UTILS_CHECK_FALSE_EXIT( phone->installAppFromRegistry(inited_apps), "Install app registry failed");
		ESP_LOGI("TIMING","t_apps=%lld us", esp_timer_get_time());

		/* Auto-launch first app. Default = Clock, including every button wake:
		 * pressing the button means "show me the watch", not "show me steps".
		 * Only a ULP wake -- where the co-processor filled a step batch while
		 * we slept -- opens the Podometer.
		 *
		 * This is purely which screen appears. Step counting is started from
		 * app_main by startBackgroundService() and keeps running whatever is
		 * in the foreground, so the choice here cannot affect the count.      */
		{
			const char *AUTOSTART_APP_NAME = "Clock";
			if (esp_brookesia::apps::Podometer::wokeFromUlpBatch()) {
				AUTOSTART_APP_NAME = "Podometer";
			}
			int autostart_id = -1;
			for (auto &info : inited_apps) {
				auto &name = std::get<0>(info);
				auto &app  = std::get<1>(info);
				if (name == AUTOSTART_APP_NAME && app != nullptr) {
					autostart_id = app->getId();
					break;
				}
			}
			if (autostart_id >= systems::base::App::APP_ID_MIN) {
				systems::base::Context::AppEventData ev = {
					.id   = autostart_id,
					.type = systems::base::Context::AppEventType::START,
					.data = nullptr,
				};
				ESP_UTILS_LOGI("Auto-starting app '%s' (id=%d)", AUTOSTART_APP_NAME, autostart_id);
				if (!phone->sendAppEvent(&ev)) {
					ESP_LOGI("TIMING","Auto-start failed");
				}
			} else {
				ESP_LOGI("TIMING","Auto-start app '%s' not found in registry", AUTOSTART_APP_NAME);
			}
		}
	esp_brookesia::apps::ClockApp::setBacklight(true);
		/* Create a timer to update the clock */
		lv_timer_create(
				[](lv_timer_t *t) {
					time_t now;
					struct tm timeinfo;
					Phone *phone = (Phone *)t->user_data;

					ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

					time(&now);
					localtime_r(&now, &timeinfo);

					ESP_UTILS_CHECK_FALSE_EXIT(
							phone->getDisplay().getStatusBar()->setClock(
									timeinfo.tm_hour, timeinfo.tm_min),
							"Refresh status bar failed");
				},
				500, phone);
	}

	// ClockApp::run() has now executed and the LVGL task has had time to
	// flush the first frame to the panel. Light up the screen.
	// vTaskDelay(pdMS_TO_TICKS(50));
	ESP_LOGI("TIMING","t_done=%lld us", esp_timer_get_time());

	if constexpr (EXAMPLE_SHOW_MEM_INFO) {
		esp_utils::thread_config_guard thread_config(
				{
						.name				= "mem_info",
						.stack_size = 4096,
				});
		boost::thread([=]() {
			char buffer[128]; /* Make sure buffer is enough for `sprintf` */
			size_t internal_free	= 0;
			size_t internal_total = 0;
			size_t external_free	= 0;
			size_t external_total = 0;

			while (1) {
				internal_free	 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
				internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
				external_free	 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
				external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
				sprintf(
						buffer,
						"\t           Biggest /     Free /    Total\n"
						"\t  SRAM : [%8d / %8d / %8d]\n"
						"\t PSRAM : [%8d / %8d / %8d]",
						heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
						internal_free, internal_total,
						heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free,
						external_total);
				// ESP_UTILS_LOGI("\n%s", buffer);

				{
					LvLockGuard gui_guard;
					ESP_UTILS_CHECK_FALSE_EXIT(
							phone->getDisplay().getRecentsScreen()->setMemoryLabel(
									internal_free / 1024, internal_total / 1024,
									external_free / 1024, external_total / 1024),
							"Set memory label failed");
				}

				boost::this_thread::sleep_for(boost::chrono::seconds(5));
			}
		}).detach();
	}
}
