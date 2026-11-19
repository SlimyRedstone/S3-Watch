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
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <functional>

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

constexpr bool EXAMPLE_SHOW_MEM_INFO = false;

// Run `fn` synchronously on a task pinned to `core` and wait for it.
//
// Peripheral drivers allocate their interrupt on the core that calls the
// init (spi_bus / i2c_new_master_bus with the default AUTO affinity). app_main
// runs on core 0, so without this the LCD DMA-done and I2C interrupts would
// fire on the radio core and wake the LVGL task on the other one. Core 0 keeps
// BT controller, NimBLE host, Wi-Fi/ESP-NOW and lwIP; core 1 keeps LVGL,
// display and touch.
static void run_on_core(BaseType_t core, std::function<void()> fn)
{
	struct Job { std::function<void()> fn; SemaphoreHandle_t done; };
	Job job{ std::move(fn), xSemaphoreCreateBinary() };
	ESP_UTILS_CHECK_NULL_EXIT(job.done, "Create semaphore failed");
	BaseType_t ok = xTaskCreatePinnedToCore(
		[](void *p) {
			auto *j = static_cast<Job *>(p);
			j->fn();
			xSemaphoreGive(j->done);
			vTaskDelete(nullptr);
		},
		"core_init", 8 * 1024, &job, 5, nullptr, core);
	ESP_UTILS_CHECK_FALSE_EXIT(ok == pdPASS, "Create core init task failed");
	xSemaphoreTake(job.done, portMAX_DELAY);
	vSemaphoreDelete(job.done);
}

extern "C" void app_main(void) {
	
	
	ESP_LOGI("TIMING","t_app_main=%lld us", esp_timer_get_time());


	ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

	esp_brookesia::apps::Podometer::releaseSleepPins();

	// I2C bus (touch, PMU, IMU, RTC): interrupt on the UI core.
	run_on_core(1, []() { bsp_i2c_init(); });
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
			.task_priority		= 1,         
			.task_stack			= 16 * 1024, 
			.task_affinity		= 1,        
			.task_max_sleep_ms 	= 5,       
			.timer_period_ms	= 1,         
		}
	};
	// Display + touch brought up from core 1 so the LCD SPI (QSPI DMA-done) and
	// touch interrupts are allocated there, next to the LVGL task.
	lv_display_t *disp = nullptr;
	run_on_core(1, [&]() { disp = bsp_display_start_with_config(&cfg); });
	ESP_UTILS_CHECK_NULL_EXIT(disp, "Start display failed");

	// Move LVGL onto internal, DMA-capable draw buffers.
	//
	// The BSP hard-codes one 410x50 px draw buffer allocated with plain malloc,
	// which lands in PSRAM. The SPI master does not treat PSRAM as DMA-capable
	// (esp_ptr_dma_capable() = internal SRAM only), so for EVERY flush it did
	// heap_caps_aligned_alloc(<stripe bytes>, MALLOC_CAP_DMA) + memcpy, up to
	// 41 kB of contiguous internal RAM per stripe, freed again in the DMA-done
	// ISR. As soon as BLE (or anything else) fragments the internal heap that
	// allocation fails with ESP_ERR_NO_MEM, esp_lcd logs
	//   panel_io_spi_tx_color(395): spi transmit (queue) color failed
	// once per stripe and the frame is torn.
	//
	// Two 410x20 px internal buffers (16.4 kB each, allocated once at boot) are
	// handed to the DMA directly. Double buffering lets LVGL render stripe N+1
	// while stripe N is on the wire. The BSP's PSRAM buffer is simply unused.
	{
		constexpr int    DRAW_LINES = 20;
		const size_t     draw_bytes = (size_t)BSP_LCD_H_RES * DRAW_LINES * sizeof(uint16_t); // RGB565
		const uint32_t   caps       = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
		void *draw1 = heap_caps_aligned_alloc(64, draw_bytes, caps);
		void *draw2 = heap_caps_aligned_alloc(64, draw_bytes, caps);
		if (draw1 != nullptr && draw2 != nullptr) {
			bsp_display_lock(0);
			lv_display_set_buffers(disp, draw1, draw2, draw_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
			bsp_display_unlock();
			ESP_UTILS_LOGI("LVGL draw buffers: 2 x %u B internal DMA (%d lines)", (unsigned)draw_bytes, DRAW_LINES);
		} else {
			ESP_UTILS_LOGW("Internal DMA draw buffers unavailable, keeping BSP PSRAM buffer");
			heap_caps_free(draw1);
			heap_caps_free(draw2);
		}
	}
	
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
						.core_id		= 1,       // UI-side helper: it only touches LVGL
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
				ESP_UTILS_LOGI("heap: SRAM %u free / %u max block | PSRAM %u free / %u max block",
						(unsigned)internal_free, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
						(unsigned)external_free, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

				{
					LvLockGuard gui_guard;
					ESP_UTILS_CHECK_FALSE_EXIT(
							phone->getDisplay().getRecentsScreen()->setMemoryLabel(
									internal_free / 1024, internal_total / 1024,
									external_free / 1024, external_total / 1024),
							"Set memory label failed");
				}

				// Was boost::this_thread::sleep_for(5 s). Under IDF 5.5.2 that wait
				// returned without blocking: task_wdt caught this thread spinning
				// inside boost's wait loop (IDLE0 starved, LVGL lock hammered,
				// touch lag). Block in the kernel instead; this is a pthread so
				// vTaskDelay() is valid here.
				vTaskDelay(pdMS_TO_TICKS(5000));
			}
		}).detach();
	}
}
