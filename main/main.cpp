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

	esp_brookesia::apps::Podometer::releaseSleepPins();

	bsp_i2c_init();
	esp_brookesia::apps::Podometer::handleUlpWakeAndSleep();
	{
		esp_pm_config_t pm_cfg = {
			.max_freq_mhz				= 240,
			.min_freq_mhz				= 40,
			.light_sleep_enable = false,
		};
		esp_err_t pm_err = esp_pm_configure(&pm_cfg);
		if (pm_err != ESP_OK) {
			ESP_UTILS_LOGW("DFS config failed (%s)", esp_err_to_name(pm_err));
		} else {
			ESP_UTILS_LOGI("DFS on: %d..%d MHz", pm_cfg.min_freq_mhz, pm_cfg.max_freq_mhz);
		}
	}

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

	esp_brookesia::apps::Podometer::startBackgroundService();
	esp_brookesia::apps::ClockApp::setBacklight(false);

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

	Phone *phone = new (std::nothrow) Phone();
	ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

	if ((BSP_LCD_H_RES == 410) && (BSP_LCD_V_RES == 502)) {
		Stylesheet *stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_410_502_DARK);
		ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

		ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
		ESP_UTILS_CHECK_FALSE_EXIT( phone->addStylesheet(stylesheet), "Add stylesheet failed");
		ESP_UTILS_CHECK_FALSE_EXIT( phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
		delete stylesheet;
	}

	{
		LvLockGuard gui_guard;

		/* Begin the phone */
		ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
		
		ESP_LOGI("TIMING","t_phone=%lld us", esp_timer_get_time());

		std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
		ESP_UTILS_CHECK_FALSE_EXIT( phone->initAppFromRegistry(inited_apps), "Init app registry failed");
		ESP_UTILS_CHECK_FALSE_EXIT( phone->installAppFromRegistry(inited_apps), "Install app registry failed");
		ESP_LOGI("TIMING","t_apps=%lld us", esp_timer_get_time());

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
