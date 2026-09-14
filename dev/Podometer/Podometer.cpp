#include "Podometer.hpp"

#include "Settings.hpp"

#ifdef ESP_UTILS_LOG_TAG
	#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Podometer"
#include <dirent.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_lib_utils.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ulp_riscv.h"
#include "ulp_pod.h"

extern "C" {
#include "actions.h"
#include "fonts.h"
#include "images.h"
#include "screens.h"
#include "structs.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"
LV_IMG_DECLARE(podometer_icon);
}

// ---- QMI8658C registers ----
#define QMI_ADDR		 0x6B
#define QMI_WHO_AM_I 0x00
#define QMI_CTRL1		 0x02
#define QMI_CTRL2		 0x03
#define QMI_CTRL3		 0x04
#define QMI_CTRL7		 0x08
#define QMI_AX_L		 0x35
#define QMI_WHO_VAL	 0x05

// ---- Podometer config ----
#define POD_DIR "/sdcard/podometer"

#if false
    #define CFG_PATH_TEMP Settings::CFG_PATH
#else
    #define CFG_PATH_TEMP "/sdcard/.settings.json"
#endif
namespace {

	struct PodConfig {
		int32_t daily_goal			 = 10000;
		bool log_to_sdcard			 = false;
		int32_t step_sensitivity = 100;	 // slider 1..100
		float height_cm					 = 175.0f;
	};

	static PodConfig s_cfg;
	static volatile int32_t s_steps_today		 = 0;
	static int s_last_day										 = -1;
	static TaskHandle_t s_imu_task					 = nullptr;
	static volatile bool s_imu_stop					 = false;
	static i2c_master_dev_handle_t s_imu_dev = nullptr;
	static bool s_imu_ok										 = false;
	static FILE *s_csv											 = nullptr;
	static char s_csv_path[128]							 = "";

	// ---- EEZ bridged vars ----
	static int32_t sensitivity_slider_value	 = 25;
	static int32_t step_count_value					 = 0;
	static int32_t step_count_percent				 = 0;
	static char sensitivity_slider_label[64] = "Sensitivity: 25%";
	static char step_count_str[16]					 = "0";
	static float height_value								 = 175.0f;

	// ---- I2C helpers ----
	static bool qmi_write(uint8_t reg, uint8_t val) {
		uint8_t buf[2] = { reg, val };
		return i2c_master_transmit(s_imu_dev, buf, 2, 50) == ESP_OK;
	}

	static bool qmi_read(uint8_t reg, uint8_t *data, size_t len) {
		return i2c_master_transmit_receive(s_imu_dev, &reg, 1, data, len, 50)
					 == ESP_OK;
	}

	static bool qmi_init() {
		if (s_imu_dev) return true;
		i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
		if (!bus) return false;
		i2c_device_config_t cfg = {};
		cfg.dev_addr_length			= I2C_ADDR_BIT_LEN_7;
		cfg.device_address			= QMI_ADDR;
		cfg.scl_speed_hz				= 400000;
		if (i2c_master_bus_add_device(bus, &cfg, &s_imu_dev) != ESP_OK)
			return false;

		uint8_t who = 0;
		if (!qmi_read(QMI_WHO_AM_I, &who, 1) || who != QMI_WHO_VAL) {
			ESP_UTILS_LOGW("QMI8658 not found (who=0x%02X)", who);
			return false;
		}
		qmi_write(QMI_CTRL1, 0x60);	 // addr auto-inc, big endian
		qmi_write(QMI_CTRL2, 0x25);	 // accel 50 Hz, ±4g
		qmi_write(QMI_CTRL7, 0x01);	 // enable accel
		ESP_UTILS_LOGI("QMI8658 OK");
		return true;
	}

	static bool qmi_read_accel(float &ax, float &ay, float &az) {
		uint8_t buf[6];
		if (!qmi_read(QMI_AX_L, buf, 6)) return false;
		int16_t rx = (int16_t)((buf[1] << 8) | buf[0]);
		int16_t ry = (int16_t)((buf[3] << 8) | buf[2]);
		int16_t rz = (int16_t)((buf[5] << 8) | buf[4]);
		// ±4g -> 1g = 8192 LSB
		ax = (float)rx / 8192.0f;
		ay = (float)ry / 8192.0f;
		az = (float)rz / 8192.0f;
		return true;
	}

	// ---- Config load / save ----
	static void load_config() {
		FILE *f = fopen(CFG_PATH_TEMP, "r");
		if (!f) return;
		char buf[512] = {};
		fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
		cJSON *root = cJSON_Parse(buf);
		if (!root) return;
		cJSON *pod = cJSON_GetObjectItem(root, "podometer");
		if (cJSON_IsObject(pod)) {
			cJSON *v;
			if ((v = cJSON_GetObjectItem(pod, "daily_goal")) && cJSON_IsNumber(v))
				s_cfg.daily_goal = v->valueint;
			if ((v = cJSON_GetObjectItem(pod, "log_to_sdcard")) && cJSON_IsBool(v))
				s_cfg.log_to_sdcard = cJSON_IsTrue(v);
			if ((v = cJSON_GetObjectItem(pod, "step_sensitivity"))
					&& cJSON_IsNumber(v))
				s_cfg.step_sensitivity = v->valueint;
			if ((v = cJSON_GetObjectItem(pod, "height_cm")) && cJSON_IsNumber(v))
				s_cfg.height_cm = (float)v->valuedouble;
		}
		cJSON_Delete(root);
	}

	static void save_config() {
		// Read existing JSON, merge podometer key, write back.
		cJSON *root = nullptr;
		{
			FILE *f = fopen(CFG_PATH_TEMP, "r");
			if (f) {
				char buf[512] = {};
				fread(buf, 1, sizeof(buf) - 1, f);
				fclose(f);
				root = cJSON_Parse(buf);
			}
		}
		if (!root) root = cJSON_CreateObject();
		cJSON_DeleteItemFromObject(root, "podometer");
		cJSON *pod = cJSON_AddObjectToObject(root, "podometer");
		cJSON_AddNumberToObject(pod, "daily_goal", s_cfg.daily_goal);
		cJSON_AddBoolToObject(pod, "log_to_sdcard", s_cfg.log_to_sdcard);
		cJSON_AddNumberToObject(pod, "step_sensitivity", s_cfg.step_sensitivity);
		cJSON_AddNumberToObject(pod, "height_cm", s_cfg.height_cm);

		char *out = cJSON_Print(root);
		cJSON_Delete(root);
		if (out) {
			FILE *f = fopen(CFG_PATH_TEMP, "w");
			if (f) {
				fputs(out, f);
				fclose(f);
			}
			cJSON_free(out);
		}
	}

	// ---- CSV logging ----
	static void ensure_csv_for_today() {
		time_t now;
		time(&now);
		struct tm t;
		localtime_r(&now, &t);
		if (t.tm_yday == s_last_day && s_csv) return;

		// Close previous day.
		if (s_csv) {
			fclose(s_csv);
			s_csv = nullptr;
		}

		s_last_day		= t.tm_yday;
		s_steps_today = 0;

		if (!s_cfg.log_to_sdcard) return;
		mkdir(POD_DIR, 0775);
		snprintf(
				s_csv_path, sizeof(s_csv_path), POD_DIR "/imu_log_%02d-%02d-%04d.csv",
				t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

		bool exists = (access(s_csv_path, F_OK) == 0);
		s_csv				= fopen(s_csv_path, "a");
		if (s_csv && !exists) {
			fprintf(s_csv, "daily_goal,distance_m,height_cm,total_steps\n");
			fprintf(s_csv, "%ld,0.00,%.1f,0\n", s_cfg.daily_goal, s_cfg.height_cm);
		}
	}

	static void log_step() {
		if (!s_csv || !s_cfg.log_to_sdcard) return;
		time_t now;
		time(&now);
		struct tm t;
		localtime_r(&now, &t);
		fprintf(s_csv, "%02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
		fflush(s_csv);
	}

	static void finalize_csv() {
		if (!s_csv) return;
		// Rewrite second line with final totals.
		// Simple approach: close and reopen to rewrite.
		fclose(s_csv);
		s_csv = nullptr;

		FILE *f = fopen(s_csv_path, "r");
		if (!f) return;
		// Read all content.
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *content = (char *)malloc(sz + 1);
		if (!content) {
			fclose(f);
			return;
		}
		fread(content, 1, sz, f);
		content[sz] = 0;
		fclose(f);

		// Find end of first line (header).
		char *nl1 = strchr(content, '\n');
		if (!nl1) {
			free(content);
			return;
		}
		// Find end of second line (old totals).
		char *nl2 = strchr(nl1 + 1, '\n');
		if (!nl2) {
			free(content);
			return;
		}

		float stride_m = s_cfg.height_cm * 0.415f / 100.0f;
		float dist		 = s_steps_today * stride_m;

		f = fopen(s_csv_path, "w");
		if (f) {
			// Rewrite header.
			fwrite(content, 1, nl1 - content + 1, f);
			// New totals line.
			fprintf(
					f, "%ld,%.2f,%.1f,%d\n", s_cfg.daily_goal, dist, s_cfg.height_cm,
					(int)s_steps_today);
			// Rest of file (timestamps).
			fwrite(nl2 + 1, 1, sz - (nl2 + 1 - content), f);
			fclose(f);
		}
		free(content);
	}

	// ---- Step detection (software, baseline + refractory) ----
	//
	// Why the old hysteresis version never fired:
	//   thr_band   = 1.0 + thr*0.1   -> 1.03 .. 1.198
	//   lower_band = thr_band - 0.15 -> 0.88 .. 1.048   (mostly below 1g rest)
	// The 0.8/0.2 low-pass also flattens peaks. At sens=100 the upper band was
	// just 0.03g above gravity but residual peaks were rarely strong enough
	// after smoothing to cross. At sens=1 even raw peaks couldn't reach it.
	//
	// New approach:
	//   - slow baseline tracks gravity (rejects orientation drift)
	//   - threshold = direct residual peak (g above baseline)
	//   - linear slider map 1..100 -> 1.2g .. 0.05g
	//   - 250 ms refractory between counted steps
	static float   s_baseline     = 1.0f;
	static int64_t s_last_step_us = 0;

	static bool detect_step(float ax, float ay, float az) {
		float mag = sqrtf(ax * ax + ay * ay + az * az);
		// Slow baseline: ~5 s time constant at 50 Hz
		s_baseline = 0.98f * s_baseline + 0.02f * mag;
		float residual = mag - s_baseline;

		// slider 1 (least) -> 1.2 g     slider 100 (most) -> 0.05 g
		float thr = 1.2f - (s_cfg.step_sensitivity - 1) * (1.15f / 99.0f);
		if (thr < 0.05f) thr = 0.05f;

		int64_t now = esp_timer_get_time();
		if ((now - s_last_step_us) < 250000) return false;   // 4 Hz max step rate

		if (residual > thr) {
			s_last_step_us = now;
			return true;
		}
		return false;
	}

	// ---- UI update (async, safe from non-LV task) ----
	static void ui_update_async(void *) {
		if (Podometer_objects.step_count_label) {
			char buf[16];
			snprintf(buf, sizeof(buf), "%ld", s_steps_today);
			// set_var_step_count_str(buf);
			set_var_step_count(s_steps_today);
			// lv_label_set_text(Podometer_objects.step_count_label, buf);
		}
		if (Podometer_objects.step_count_percent_bar) {
			int pct = (s_cfg.daily_goal > 0) ?
										(int)(s_steps_today * 100 / s_cfg.daily_goal) :
										0;
			if (pct > 100) pct = 100;
			set_var_step_count_percent(pct);
			// lv_bar_set_value(
			// 		Podometer_objects.step_count_percent_bar, pct, LV_ANIM_ON);
		}
	}

	// ---- Background IMU task ----
	static void imu_task(void *) {
		if (!qmi_init()) {
			ESP_UTILS_LOGW("IMU init failed, task exiting");
			s_imu_task = nullptr;
			vTaskDelete(NULL);
			return;
		}
		s_imu_ok = true;

		while (!s_imu_stop) {
			ensure_csv_for_today();

			float ax, ay, az;
			if (qmi_read_accel(ax, ay, az)) {
				if (detect_step(ax, ay, az)) {
					s_steps_today++;
					log_step();
					lv_async_call(ui_update_async, nullptr);
				}
			}
			vTaskDelay(pdMS_TO_TICKS(20));	// ~50 Hz
		}

		finalize_csv();
		s_imu_task = nullptr;
		vTaskDelete(NULL);
	}

	// ===========================================================================
	// ULP-backed deep sleep pedometer
	//
	// ULP RISC-V firmware polls QMI_INT1 (GPIO21) every 50 ms, counts rising
	// edges, wakes the main CPU once a batch of 100 pulses has accumulated.
	// Main CPU then flushes the batch to NVS and goes back to deep sleep.
	// The BOOT button (GPIO0) ext1 wake brings the user back into the UI.
	// ===========================================================================

	extern const uint8_t ulp_pod_bin_start[] asm("_binary_ulp_pod_bin_start");
	extern const uint8_t ulp_pod_bin_end[]   asm("_binary_ulp_pod_bin_end");

	// Symbols exported by the ULP firmware (linker generates ulp_<name>).
	extern "C" uint32_t ulp_batch_count;
	extern "C" uint32_t ulp_total_pulses;
	extern "C" uint32_t ulp_batch_target;
	extern "C" uint32_t ulp_last_level;

	#define POD_NVS_NS      "podometer"
	#define POD_NVS_KEY     "steps"
	#define POD_BTN_GPIO    GPIO_NUM_0
	#define POD_INT_GPIO    GPIO_NUM_21

	// RTC slow-memory flag: survives deep sleep. main.cpp reads it to know
	// whether to autostart Podometer instead of ClockApp.
	RTC_DATA_ATTR static uint32_t s_pod_rtc_magic = 0;
	static constexpr uint32_t POD_MAGIC = 0xBEEFD06E;

	static void qmi_enable_pedometer_engine()
	{
		// Best-effort pedometer engine setup. Datasheet rev dependent.
		// Enable accelerometer first.
		qmi_write(QMI_CTRL2, 0x25);            // accel 50 Hz, ±4g
		// CTRL1: addr auto-inc, big endian, INT1 push-pull, INT1 enable bit 3
		qmi_write(QMI_CTRL1, 0x60 | (1 << 3)); // 0x68
		// CTRL8: enable pedometer engine (bit 4 PEDO_EN per datasheet)
		qmi_write(QMI_CTRL7, 0x01);            // accel on
		uint8_t ctrl8 = 0;
		qmi_read(0x09, &ctrl8, 1);
		ctrl8 |= (1 << 4);
		qmi_write(0x09, ctrl8);
		// CTRL9 commit pedometer config: command 0x0D (write pedometer parms).
		// Default-precision config: leave CAL registers at chip defaults.
		qmi_write(0x0A, 0x0D);
		// Wait briefly for CTRL9 ack (datasheet says <2 ms).
		vTaskDelay(pdMS_TO_TICKS(5));
		qmi_write(0x0A, 0x00);  // ack
	}

	static void load_persisted_steps()
	{
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
		int32_t v = 0;
		if (nvs_get_i32(h, POD_NVS_KEY, &v) == ESP_OK) {
			s_steps_today = v;
		}
		nvs_close(h);
	}

	static void flush_steps_to_nvs(int32_t delta)
	{
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
		int32_t cur = 0;
		nvs_get_i32(h, POD_NVS_KEY, &cur);
		cur += delta;
		nvs_set_i32(h, POD_NVS_KEY, cur);
		nvs_commit(h);
		nvs_close(h);
		s_steps_today = cur;
	}

	static esp_err_t load_ulp_firmware()
	{
		ulp_batch_count   = 0;
		ulp_total_pulses  = 0;
		ulp_batch_target  = 100;
		ulp_last_level    = 0;

		esp_err_t err = ulp_riscv_load_binary(
			ulp_pod_bin_start,
			(ulp_pod_bin_end - ulp_pod_bin_start));
		if (err != ESP_OK) return err;

		// Configure GPIO21 as RTC input (pull-down so it's low when IDLE,
		// QMI INT1 active-high pulses go through).
		rtc_gpio_init(POD_INT_GPIO);
		rtc_gpio_set_direction(POD_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
		rtc_gpio_pullup_dis(POD_INT_GPIO);
		rtc_gpio_pulldown_en(POD_INT_GPIO);

		// Configure BOOT button as RTC input with pull-up for ext1 wake.
		rtc_gpio_init(POD_BTN_GPIO);
		rtc_gpio_set_direction(POD_BTN_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
		rtc_gpio_pullup_en(POD_BTN_GPIO);
		rtc_gpio_pulldown_dis(POD_BTN_GPIO);

		ulp_set_wakeup_period(0, 50000); // 50 ms
		return ulp_riscv_run();
	}

	// Read the ULP's batch counter, flush to NVS, reset the counter.
	static void absorb_ulp_batch()
	{
		uint32_t batch = ulp_batch_count;
		if (batch == 0) return;
		ulp_batch_count = 0;
		flush_steps_to_nvs((int32_t)batch);
		ESP_UTILS_LOGI("ULP batch absorbed: +%u steps (total=%d)",
			(unsigned)batch, (int)s_steps_today);
	}

	static void enter_deep_sleep_internal()
	{
		// Mark "wake to podometer" before we sleep.
		s_pod_rtc_magic = POD_MAGIC;

		// Clear any stale wake sources.
		esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

		// Wake source 1 = BOOT button (active LOW on GPIO0).
		uint64_t io_mask = 1ULL << POD_BTN_GPIO;
		esp_sleep_enable_ext1_wakeup(io_mask, ESP_EXT1_WAKEUP_ANY_LOW);

		// Wake source 2 = ULP (after it counts a full step batch).
		esp_sleep_enable_ulp_wakeup();

		// Keep RTC peripheral domain alive — ULP needs it.
		esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

		// Hold the GPIO pulls across deep sleep.
		rtc_gpio_hold_en(POD_BTN_GPIO);
		rtc_gpio_hold_en(POD_INT_GPIO);
		gpio_deep_sleep_hold_en();

		ESP_UTILS_LOGI("Podometer: deep sleep with ULP running");
		esp_deep_sleep_start();
		// Not reached.
	}

}	 // namespace

// ---- EEZ C API ----
extern "C" {

float get_var_height_value() { return height_value; }
void set_var_height_value(float value) {
	height_value		= value;
	s_cfg.height_cm = value;
}

int32_t get_var_sensitivity_slider_value() { return sensitivity_slider_value; }
void set_var_sensitivity_slider_value(int32_t value) {
	sensitivity_slider_value = value;
	s_cfg.step_sensitivity	 = value;
	if (Podometer_objects.sensitivity_slider)
		lv_slider_set_value(
				Podometer_objects.sensitivity_slider, value, LV_ANIM_ON);
}

const char *get_var_sensitivity_slider_label() {
	return sensitivity_slider_label;
}
void set_var_sensitivity_slider_label(const char *value) {
	strncpy(
			sensitivity_slider_label, value, sizeof(sensitivity_slider_label) - 1);
	if (Podometer_objects.sensitivity_label)
		lv_label_set_text(
				Podometer_objects.sensitivity_label, sensitivity_slider_label);
}

int32_t get_var_step_count() { return step_count_value; }
void set_var_step_count(int32_t value) {
	step_count_value = value;
	itoa(step_count_value, step_count_str, 10);
	if (Podometer_objects.step_count_label)
		lv_label_set_text(Podometer_objects.step_count_label, step_count_str);
}

int32_t get_var_step_count_percent() { return step_count_percent; }
void set_var_step_count_percent(int32_t value) {
	step_count_percent = value;
	if (Podometer_objects.step_count_percent_bar)
		lv_bar_set_value(
				Podometer_objects.step_count_percent_bar, value, LV_ANIM_ON);
}

const char *get_var_step_count_str() { return step_count_str; }
void set_var_step_count_str(const char *value) {
	strncpy(step_count_str, value, sizeof(step_count_str) - 1);
	if (Podometer_objects.step_count_label)
		lv_label_set_text(Podometer_objects.step_count_label, step_count_str);
}

void action_step_spinbox_increment(lv_event_t *e) {
	lv_spinbox_increment(Podometer_objects.step_count_selector);
	s_cfg.daily_goal
			= lv_spinbox_get_value(Podometer_objects.step_count_selector);
}
void action_step_spinbox_decrement(lv_event_t *e) {
	lv_spinbox_decrement(Podometer_objects.step_count_selector);
	s_cfg.daily_goal
			= lv_spinbox_get_value(Podometer_objects.step_count_selector);
}

void action_sensitivity_slider(lv_event_t *e) {
	lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
	int v						 = (int)lv_slider_get_value(slider);
	set_var_sensitivity_slider_value(v);
	char str_value[32];
	snprintf(str_value, sizeof(str_value), "Sensitivity: %d%%", v);
	set_var_sensitivity_slider_label(str_value);
	s_cfg.step_sensitivity = v;
}

void action_save_sdcard_option_switch(lv_event_t *e) {
	lv_obj_t *sw				= (lv_obj_t *)lv_event_get_target(e);
	s_cfg.log_to_sdcard = lv_obj_has_state(sw, LV_STATE_CHECKED);
	save_config();
}

void action_height_spinbox_increment(lv_event_t *e) {
	lv_spinbox_increment(Podometer_objects.height_selector);
	s_cfg.height_cm
			= (float)lv_spinbox_get_value(Podometer_objects.height_selector);
}
void action_height_spinbox_decrement(lv_event_t *e) {
	lv_spinbox_decrement(Podometer_objects.height_selector);
	s_cfg.height_cm
			= (float)lv_spinbox_get_value(Podometer_objects.height_selector);
}

}	 // extern "C"

// ---- Brookesia App ----
namespace esp_brookesia::apps {

	static constexpr char APP_NAME[] = "Podometer";

	Podometer *Podometer::_instance = nullptr;

	Podometer *Podometer::requestInstance(
			bool use_status_bar, bool use_navigation_bar) {
		if (_instance == nullptr)
			_instance = new Podometer(use_status_bar, use_navigation_bar);
		return _instance;
	}

	Podometer::Podometer(bool use_status_bar, bool use_navigation_bar)
			: App(APP_NAME, &podometer_icon, true, use_status_bar,
						use_navigation_bar) {}

	Podometer::~Podometer() { _instance = nullptr; }

	bool Podometer::init() {
		load_config();

		// NVS may not be initialized if no other component did it.
		esp_err_t nvs_err = nvs_flash_init();
		if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			nvs_flash_erase();
			nvs_flash_init();
		}

		load_persisted_steps();

		// If we just woke from the ULP pedometer path, absorb the batch.
		esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
		if (cause == ESP_SLEEP_WAKEUP_ULP) {
			ulp_riscv_halt();
			absorb_ulp_batch();
		}

		// Configure QMI8658C interrupt + pedometer engine on first install.
		if (qmi_init()) {
			qmi_enable_pedometer_engine();
		}

		// Load and run the ULP firmware (no-op if already running).
		if (load_ulp_firmware() != ESP_OK) {
			ESP_UTILS_LOGW("ULP firmware load failed");
		}

		s_imu_stop = false;
		if (s_imu_task == nullptr) {
			xTaskCreate(imu_task, "imu_pod", 6 * 1024, NULL, 3, &s_imu_task);
		}
		return true;
	}

	bool Podometer::deinit() {
		s_imu_stop = true;
		save_config();
		return true;
	}

	bool Podometer::run() {
		Podometer_ui_init();

		// Sync UI with current config.
		set_var_sensitivity_slider_value(s_cfg.step_sensitivity);
		char sl[32];
		snprintf(sl, sizeof(sl), "Sensitivity: %ld%%", s_cfg.step_sensitivity);
		set_var_sensitivity_slider_label(sl);

		if (Podometer_objects.step_count_selector)
			lv_spinbox_set_value(
					Podometer_objects.step_count_selector, s_cfg.daily_goal);
		if (Podometer_objects.height_selector)
			lv_spinbox_set_value(
					Podometer_objects.height_selector, (int32_t)s_cfg.height_cm);
		if (Podometer_objects.sdcard_option_switch) {
			if (s_cfg.log_to_sdcard)
				lv_obj_add_state(
						Podometer_objects.sdcard_option_switch, LV_STATE_CHECKED);
			else
				lv_obj_clear_state(
						Podometer_objects.sdcard_option_switch, LV_STATE_CHECKED);
		}

		ui_update_async(nullptr);
		return true;
	}

	bool Podometer::back() {
		save_config();
		notifyCoreClosed();
		return true;
	}

	bool Podometer::close() {
		save_config();
		return true;
	}

	// ---- Public static API ----

	void Podometer::enterDeepSleepWithUlp() {
		// Save current daily step count.
		flush_steps_to_nvs(0);
		// Stop the foreground IMU helper task (ULP takes over).
		s_imu_stop = true;
		enter_deep_sleep_internal();
	}

	int32_t Podometer::loadPersistedSteps() {
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
		int32_t v = 0;
		nvs_get_i32(h, POD_NVS_KEY, &v);
		nvs_close(h);
		return v;
	}

	bool Podometer::wokeFromPodometer() {
		esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
		return (cause == ESP_SLEEP_WAKEUP_ULP ||
		        cause == ESP_SLEEP_WAKEUP_EXT1) &&
		       s_pod_rtc_magic == POD_MAGIC;
	}

	ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
			systems::base::App, Podometer, APP_NAME, []() {
				return std::shared_ptr<Podometer>(
						Podometer::requestInstance(), [](Podometer *p) {});
			})

}	 // namespace esp_brookesia::apps
