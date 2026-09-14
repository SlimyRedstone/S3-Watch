#include "Podometer.hpp"

#include "ClockApp.hpp"
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
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_lib_utils.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pod_ulp_shared.h"
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
#define QMI_ADDR		 POD_QMI_ADDR
#define QMI_WHO_AM_I 0x00
#define QMI_CTRL1		 0x02
#define QMI_CTRL2		 0x03
#define QMI_CTRL3		 0x04
#define QMI_CTRL7		 0x08
#define QMI_CTRL8		 0x09
#define QMI_CTRL9		 0x0A
#define QMI_STATUS1	 0x2F
#define QMI_AX_L		 POD_QMI_AX_L
#define QMI_WHO_VAL	 0x05

// CTRL8 bit 6 selects which pin carries motion-detection events
// (any/no/sig-motion, pedometer, tap). 0 = INT2, 1 = INT1. GPIO21 on this
// board is wired to INT1, so the bit has to be set -- the reset default
// routes every pedometer event to INT2, where nothing is listening.
#define QMI_CTRL8_INT1_SEL (1 << 6)
#define QMI_CTRL8_PEDO_EN	 (1 << 4)

// ---- Optional SD-card CSV log ----
// Step counts and settings live in NVS. The CSV is an opt-in extra, off by
// default, and is never required for the podometer to work.
#define POD_DIR "/sdcard/podometer"
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

	// Steps counted in RAM by the foreground task but not yet written to NVS.
	// Every step the app counts while awake passes through here; NVS is only
	// touched on day rollover, on sleep and on app exit, so a walk does not
	// burn a flash write per step.
	static volatile int32_t s_unflushed = 0;

	// Local calendar day the counter in NVS belongs to. -1 = not resolved yet
	// (clock not synced), in which case rollover checks are skipped.
	static int32_t s_nvs_day_id = -1;

	// Background worker that owns everything slow: NVS, I2C, ULP firmware.
	// Keeps all of it off the boot critical path and out of the LVGL lock.
	static TaskHandle_t s_bg_task	 = nullptr;
	static volatile bool s_bg_ready = false;

	// True only while the Podometer screen actually exists.
	//
	// Counting is deliberately independent of the UI: it starts before the
	// display is up and keeps running after the app is closed. Every call into
	// LVGL therefore has to be gated on this -- lv_async_call() before
	// lv_init() walks an uninitialised list and takes the whole system down.
	static volatile bool s_ui_ready = false;

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
		qmi_write(QMI_CTRL2, 0x25);	 // aFS=0b010 => +-8g, aODR=0b0101 => 250 Hz
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
		// CTRL2 aFS = 0b010 => +-8g => 1g = 4096 LSB. The previous 8192 here
		// matched a comment, not the register, and halved every reading.
		ax = (float)rx / POD_LSB_PER_G;
		ay = (float)ry / POD_LSB_PER_G;
		az = (float)rz / POD_LSB_PER_G;
		return true;
	}

	// ---- NVS-backed storage ----
	// Everything that has to survive a power cut lives here: settings,
	// today's step count, and the calendar day that count belongs to.
	// The SD card is never required for any of it.
#define POD_NVS_NS		 "podometer"
#define POD_KEY_STEPS	 "steps"
#define POD_KEY_DAY		 "day"
#define POD_KEY_GOAL	 "goal"
#define POD_KEY_SENS	 "sens"
#define POD_KEY_HEIGHT "height_mm"
#define POD_KEY_LOGSD	 "log_sd"

	// Local calendar day as a comparable id. Returns -1 while the RTC is
	// still unset so callers skip the rollover check instead of wrongly
	// zeroing a real step count on a device that booted before time sync.
	static int32_t today_id() {
		time_t now;
		time(&now);
		if (now < 1600000000) return -1;	// before Sep 2020 => clock not synced
		struct tm t;
		localtime_r(&now, &t);
		return (int32_t)t.tm_year * 400 + (int32_t)t.tm_yday;
	}

	// ---- Config load / save ----
	static void load_config() {
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

		int32_t v = 0;
		uint8_t b = 0;
		if (nvs_get_i32(h, POD_KEY_GOAL, &v) == ESP_OK) s_cfg.daily_goal = v;
		if (nvs_get_i32(h, POD_KEY_SENS, &v) == ESP_OK) s_cfg.step_sensitivity = v;
		if (nvs_get_i32(h, POD_KEY_HEIGHT, &v) == ESP_OK)
			s_cfg.height_cm = (float)v / 10.0f;	 // stored in mm
		if (nvs_get_u8(h, POD_KEY_LOGSD, &b) == ESP_OK)
			s_cfg.log_to_sdcard = (b != 0);

		nvs_close(h);
	}

	// Set by the action handlers whenever the user moves a control. The commit
	// itself is deferred until the finger comes off -- see maybe_commit_config().
	// Dragging the sensitivity slider fires VALUE_CHANGED on every pixel of
	// travel, and writing NVS on each one would hammer the flash and stall the
	// LVGL task mid-gesture.
	static volatile bool s_cfg_dirty = false;

	static void save_config() {
		s_cfg_dirty = false;

		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

		nvs_set_i32(h, POD_KEY_GOAL, s_cfg.daily_goal);
		nvs_set_i32(h, POD_KEY_SENS, s_cfg.step_sensitivity);
		nvs_set_i32(h, POD_KEY_HEIGHT, (int32_t)(s_cfg.height_cm * 10.0f));
		nvs_set_u8(h, POD_KEY_LOGSD, s_cfg.log_to_sdcard ? 1 : 0);
		nvs_commit(h);

		nvs_close(h);
	}

	// Safety net for the exit paths: persists a change the user made but whose
	// release we never saw (leaving the app mid-gesture, sleeping, uninstall).
	// No-op when nothing changed, so closing the app does not cost a flash
	// write every time.
	static void save_config_if_dirty() {
		if (s_cfg_dirty) save_config();
	}

	// ---- Step accounting ----
	// s_steps_today is the live count. s_unflushed is the part of it that NVS
	// has not seen yet. Committing adds the delta rather than overwriting, so
	// a batch the ULP counted during deep sleep and a batch the foreground
	// task counted while awake can never clobber one another.
	static void commit_steps(int32_t extra) {
		int32_t delta = s_unflushed + extra;
		s_unflushed		= 0;

		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
			s_unflushed = delta;	// keep it in RAM, retry on the next commit
			return;
		}

		int32_t stored		 = 0;
		int32_t stored_day = -1;
		nvs_get_i32(h, POD_KEY_STEPS, &stored);
		nvs_get_i32(h, POD_KEY_DAY, &stored_day);

		// Day rollover. Only acted on once the RTC is known good, otherwise a
		// boot before time sync would wipe a real count.
		int32_t now_day = today_id();
		if (now_day >= 0 && stored_day >= 0 && now_day != stored_day) {
			stored = 0;
		}
		if (now_day >= 0) {
			s_nvs_day_id = now_day;
			nvs_set_i32(h, POD_KEY_DAY, now_day);
		}

		stored += delta;
		if (stored < 0) stored = 0;
		nvs_set_i32(h, POD_KEY_STEPS, stored);
		nvs_commit(h);
		nvs_close(h);

		s_steps_today = stored;
	}

	// ---- Optional CSV logging ----
	// Purely an extra. Off unless the user flips the switch, and the podometer
	// is fully functional with no SD card present.
	static void ensure_csv_for_today() {
		if (!s_cfg.log_to_sdcard) {
			if (s_csv) {
				fclose(s_csv);
				s_csv = nullptr;
			}
			return;
		}

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

		s_last_day = t.tm_yday;

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

	// slider 1 (least) -> 1.2 g, slider 100 (most) -> 0.05 g.
	// Shared with the ULP so a walk counts the same asleep as awake.
	static float step_threshold_g() {
		float thr = 1.2f - (s_cfg.step_sensitivity - 1) * (1.15f / 99.0f);
		return (thr < 0.05f) ? 0.05f : thr;
	}

	static bool detect_step(float ax, float ay, float az) {
		float mag = sqrtf(ax * ax + ay * ay + az * az);
		// Slow baseline: ~5 s time constant at 50 Hz
		s_baseline = 0.98f * s_baseline + 0.02f * mag;
		float residual = mag - s_baseline;

		int64_t now = esp_timer_get_time();
		if ((now - s_last_step_us) < 250000) return false;   // 4 Hz max step rate

		if (residual > step_threshold_g()) {
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

	// Refresh timer, owned by the LVGL task. Created in run(), destroyed in
	// close().
	//
	// The counting tasks deliberately do NOT call into LVGL at all -- not even
	// lv_async_call(), which is not thread safe: it mutates LVGL's timer list,
	// so calling it from the IMU task while the LVGL task walks that same list
	// corrupts it. The result is a spinning lv_timer_handler: frozen screen,
	// dead touch, no panic and no backtrace.
	//
	// Instead the tasks only ever write plain variables, and this timer polls
	// them from the thread that owns LVGL.
	static lv_timer_t *s_ui_timer = nullptr;
	static constexpr uint32_t UI_REFRESH_MS = 500;

	// Commit pending setting changes once the user has taken their finger off.
	//
	// Driven from the UI timer rather than per-widget LV_EVENT_RELEASED
	// handlers because the spinbox +/- buttons are separate EEZ-generated
	// objects that are not in Podometer_objects, so there is nothing reliable
	// to hang a release callback on. Asking the input device directly catches
	// every control, including press-and-hold auto-repeat, and collapses a
	// whole gesture into exactly one NVS write.
	//
	// Runs on the LVGL task, so touching indev state here is safe.
	static void maybe_commit_config() {
		if (!s_cfg_dirty) return;

		for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
				 indev = lv_indev_get_next(indev)) {
			if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
				return;	// still touching, wait
			}
		}

		save_config();	// clears s_cfg_dirty
		ESP_UTILS_LOGD("Settings committed on release");
	}

	// ---- Foreground IMU task ----
	// Counts steps while the CPU is awake. The ULP takes over the moment we
	// deep sleep; the two never run at the same time, so they cannot fight
	// over the shared I2C bus.
	static void imu_task(void *) {
		if (!qmi_init()) {
			ESP_UTILS_LOGW("IMU init failed, task exiting");
			s_imu_task = nullptr;
			vTaskDelete(NULL);
			return;
		}
		s_imu_ok = true;

		// Housekeeping (day rollover, CSV rotation) only needs to run about
		// once a second, not on every one of the 50 samples per second.
		int housekeeping = 0;
		// Periodically push unflushed steps to NVS so a flat battery costs at
		// most this many steps. 500 steps is roughly one flash write per
		// 6 minutes of brisk walking.
		constexpr int32_t FLUSH_THRESHOLD = 500;

		while (!s_imu_stop) {
			if (--housekeeping <= 0) {
				housekeeping = 50;	// ~1 s at 50 Hz
				ensure_csv_for_today();

				// Roll the day over even if the user never opens the app.
				int32_t now_day = today_id();
				if (now_day >= 0 && s_nvs_day_id >= 0 && now_day != s_nvs_day_id) {
					commit_steps(0);	// commit_steps() performs the reset
				}
			}

			float ax, ay, az;
			if (qmi_read_accel(ax, ay, az)) {
				if (detect_step(ax, ay, az)) {
					s_steps_today++;
					s_unflushed++;
					log_step();
					if (s_unflushed >= FLUSH_THRESHOLD) commit_steps(0);
				}
			}
			vTaskDelay(pdMS_TO_TICKS(20));	// ~50 Hz
		}

		// Do not lose the tail of the walk on the way out.
		commit_steps(0);
		finalize_csv();
		s_imu_task = nullptr;
		vTaskDelete(NULL);
	}

	// ===========================================================================
	// ULP-backed deep sleep pedometer
	//
	// The ULP RISC-V core stays powered while the main CPU is in deep sleep.
	// It counts steps (see ulp/main.c for the two strategies) and wakes the
	// host once POD_BATCH_TARGET steps have accumulated; the host folds the
	// batch into NVS and goes straight back to sleep. A BOOT button press
	// (ext1, active low) instead brings the user into the UI.
	//
	// Ownership of the IMU is strictly exclusive: the ULP counts while
	// asleep, the foreground task counts while awake, never both. The handoff
	// happens in enter_deep_sleep_internal() and Podometer::onWake().
	// ===========================================================================

	extern const uint8_t ulp_pod_bin_start[] asm("_binary_ulp_pod_bin_start");
	extern const uint8_t ulp_pod_bin_end[]   asm("_binary_ulp_pod_bin_end");

	// The ULP's exported symbols are declared by the generated ulp_pod.h,
	// included at the top of this file -- do not redeclare them here or every
	// reference becomes ambiguous.
	//
	// Note that header only lists symbols that survived the ULP link. It is
	// built with -fdata-sections and garbage-collects any global the firmware
	// does not reference, so the inactive mode's variables are simply absent.
	// That is why every use below is behind a POD_ULP_MODE guard.

	static constexpr gpio_num_t POD_BTN_PIN = (gpio_num_t)POD_BTN_GPIO;
	static constexpr gpio_num_t POD_INT_PIN = (gpio_num_t)POD_INT_GPIO;
	static constexpr gpio_num_t POD_SCL_PIN = (gpio_num_t)POD_SCL_GPIO;
	static constexpr gpio_num_t POD_SDA_PIN = (gpio_num_t)POD_SDA_GPIO;

	// RTC slow-memory flags: survive deep sleep, cleared by a power cycle.
	// s_pod_rtc_magic tells main.cpp to autostart Podometer instead of Clock.
	// s_ulp_loaded says the ULP image is already in RTC memory and still has
	// live detector state, so we must not reload it and wipe that state.
	RTC_DATA_ATTR static uint32_t s_pod_rtc_magic = 0;
	RTC_DATA_ATTR static uint32_t s_ulp_loaded		= 0;
	static constexpr uint32_t POD_MAGIC = 0xBEEFD06E;

	static void qmi_enable_pedometer_engine()
	{
		// Only meaningful for the INT fallback. In I2C mode the ULP reads raw
		// acceleration and does its own detection, so the on-chip pedometer
		// engine (barely documented in the Rev 0.9 datasheet) is not relied on.
		qmi_write(QMI_CTRL2, 0x25);								// aFS +-8g, aODR 250 Hz
		qmi_write(QMI_CTRL1, 0x60 | (1 << 3));		// addr auto-inc, big endian, INT1 en
		qmi_write(QMI_CTRL7, 0x01);								// accel on

		uint8_t ctrl8 = 0;
		qmi_read(QMI_CTRL8, &ctrl8, 1);
		// Bit 6 picks the pin motion events land on. Reset default is INT2;
		// GPIO21 on this board is INT1, so without this bit the ULP would
		// watch a pin that never pulses.
		ctrl8 |= QMI_CTRL8_PEDO_EN | QMI_CTRL8_INT1_SEL;
		qmi_write(QMI_CTRL8, ctrl8);

		// CTRL9 command 0x0D commits the pedometer parameters.
		qmi_write(QMI_CTRL9, 0x0D);
		vTaskDelay(pdMS_TO_TICKS(5));	 // datasheet: ack in under 2 ms
		qmi_write(QMI_CTRL9, 0x00);
	}

	static void load_persisted_steps()
	{
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

		int32_t v	 = 0;
		int32_t day = -1;
		nvs_get_i32(h, POD_KEY_DAY, &day);
		if (nvs_get_i32(h, POD_KEY_STEPS, &v) == ESP_OK) s_steps_today = v;
		nvs_close(h);

		s_nvs_day_id = day;

		// Stale count from an earlier day: zero it before the UI shows it.
		int32_t now_day = today_id();
		if (now_day >= 0 && day >= 0 && now_day != day) {
			s_steps_today = 0;
			commit_steps(0);
		}
	}

	// Put the pins the ULP needs into the RTC IO mux. Only ever called on the
	// way into deep sleep -- while the CPU is awake the I2C pads must stay on
	// the digital mux or the touch panel, PMU and IMU all go dark.
	static void configure_rtc_pins_for_ulp()
	{
		// BOOT button, ext1 wake source, active low.
		rtc_gpio_init(POD_BTN_PIN);
		rtc_gpio_set_direction(POD_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
		rtc_gpio_pullup_en(POD_BTN_PIN);
		rtc_gpio_pulldown_dis(POD_BTN_PIN);

#if POD_ULP_MODE == POD_ULP_MODE_I2C
		// Hand the shared I2C bus to the ULP. Deliberately not held across
		// sleep: a held pad cannot be driven, and the ULP needs to pull both
		// lines low to clock bits out.
		const gpio_num_t i2c_pins[2] = { POD_SCL_PIN, POD_SDA_PIN };
		for (gpio_num_t pin : i2c_pins) {
			rtc_gpio_init(pin);
			rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
			rtc_gpio_pullup_en(pin);	// backs up the board's bus pull-ups
			rtc_gpio_pulldown_dis(pin);
		}
#else
		// INT1 idles low, pedometer pulses drive it high.
		rtc_gpio_init(POD_INT_PIN);
		rtc_gpio_set_direction(POD_INT_PIN, RTC_GPIO_MODE_INPUT_ONLY);
		rtc_gpio_pullup_dis(POD_INT_PIN);
		rtc_gpio_pulldown_en(POD_INT_PIN);
#endif
	}

	// Load (once) and start the ULP. Detector state in RTC memory is
	// preserved across sleeps; only the tuning parameters are refreshed, so
	// changing the sensitivity slider takes effect on the next sleep.
	static esp_err_t start_ulp()
	{
		if (s_ulp_loaded != POD_MAGIC) {
			esp_err_t err = ulp_riscv_load_binary(
				ulp_pod_bin_start, (size_t)(ulp_pod_bin_end - ulp_pod_bin_start));
			if (err != ESP_OK) return err;

			ulp_batch_count	 = 0;
			ulp_total_pulses = 0;
#if POD_ULP_MODE == POD_ULP_MODE_I2C
			ulp_baseline	 = POD_ONE_G;
			ulp_i2c_errors = 0;
#else
			ulp_last_level = 0;
#endif
			s_ulp_loaded = POD_MAGIC;
		}

		ulp_batch_target = POD_BATCH_TARGET;
#if POD_ULP_MODE == POD_ULP_MODE_I2C
		ulp_step_thr			= (uint32_t)(step_threshold_g() * POD_ONE_G);
		// 250 ms refractory, same as the foreground detector.
		ulp_refract_ticks = 250000 / POD_ULP_PERIOD_US;
#endif

		configure_rtc_pins_for_ulp();
		ulp_set_wakeup_period(0, POD_ULP_PERIOD_US);
		return ulp_riscv_run();
	}

	// Fold whatever the ULP counted into NVS and clear its counter. Safe to
	// call on every boot: a button wake mid-batch would otherwise throw away
	// up to POD_BATCH_TARGET-1 steps.
	static void absorb_ulp_batch()
	{
		if (s_ulp_loaded != POD_MAGIC) return;

		uint32_t batch = ulp_batch_count;
		if (batch == 0) return;
		ulp_batch_count = 0;

		commit_steps((int32_t)batch);
#if POD_ULP_MODE == POD_ULP_MODE_I2C
		ESP_UTILS_LOGI("ULP batch absorbed: +%u steps (total=%d, i2c_err=%u, "
									 "last_mag=%u)",
			(unsigned)batch, (int)s_steps_today, (unsigned)ulp_i2c_errors,
			(unsigned)ulp_last_mag);
#else
		ESP_UTILS_LOGI("ULP batch absorbed: +%u steps (total=%d)",
			(unsigned)batch, (int)s_steps_today);
#endif
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

		// Drop the rails FIRST, while the I2C pads are still on the digital mux
		// and the PMU is reachable. start_ulp() below moves SCL/SDA into the RTC
		// mux for the bit-bang, and once that happens every driver-based I2C
		// transfer NACKs -- including the ones this call needs.
		esp_brookesia::apps::ClockApp::powerDownForUlpSleep(POD_SLEEP_DROP_RAILS);

		// Now hand the pins over and start counting. Kept as late as possible so
		// the window where other tasks can collide with the RTC-muxed bus is
		// only the few microseconds before the core stops.
		esp_err_t err = start_ulp();
		if (err != ESP_OK) {
			ESP_UTILS_LOGE("ULP start failed (%s) - steps will not be counted "
										 "during sleep", esp_err_to_name(err));
		}

		// Hold only the button. The pins the ULP has to drive must stay
		// unheld, otherwise the pad latches and the ULP cannot toggle it.
		rtc_gpio_hold_en(POD_BTN_PIN);
#if POD_ULP_MODE != POD_ULP_MODE_I2C
		rtc_gpio_hold_en(POD_INT_PIN);
#endif

#if POD_ULP_MODE == POD_ULP_MODE_I2C
		ESP_UTILS_LOGI("Podometer: deep sleep, ULP counting via I2C (thr=%u, "
									 "one_g=%d)", (unsigned)ulp_step_thr, POD_ONE_G);
#else
		ESP_UTILS_LOGI("Podometer: deep sleep, ULP counting INT1 edges");
#endif

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
	s_cfg_dirty = true;
}
void action_step_spinbox_decrement(lv_event_t *e) {
	lv_spinbox_decrement(Podometer_objects.step_count_selector);
	s_cfg.daily_goal
			= lv_spinbox_get_value(Podometer_objects.step_count_selector);
	s_cfg_dirty = true;
}

void action_sensitivity_slider(lv_event_t *e) {
	lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
	int v						 = (int)lv_slider_get_value(slider);

	// Update the model only. Deliberately NOT via
	// set_var_sensitivity_slider_value(), which calls lv_slider_set_value() on
	// this very slider with LV_ANIM_ON -- writing an animated value back into
	// the widget from inside its own VALUE_CHANGED handler.
	sensitivity_slider_value = v;
	s_cfg.step_sensitivity	 = v;

	char str_value[32];
	snprintf(str_value, sizeof(str_value), "Sensitivity: %d%%", v);
	set_var_sensitivity_slider_label(str_value);

	// Committed on finger-lift, not here.
	s_cfg_dirty = true;
}

void action_save_sdcard_option_switch(lv_event_t *e) {
	lv_obj_t *sw				= (lv_obj_t *)lv_event_get_target(e);
	s_cfg.log_to_sdcard = lv_obj_has_state(sw, LV_STATE_CHECKED);
	s_cfg_dirty					= true;
}

void action_height_spinbox_increment(lv_event_t *e) {
	lv_spinbox_increment(Podometer_objects.height_selector);
	s_cfg.height_cm
			= (float)lv_spinbox_get_value(Podometer_objects.height_selector);
	s_cfg_dirty = true;
}
void action_height_spinbox_decrement(lv_event_t *e) {
	lv_spinbox_decrement(Podometer_objects.height_selector);
	s_cfg.height_cm
			= (float)lv_spinbox_get_value(Podometer_objects.height_selector);
	s_cfg_dirty = true;
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

	// Everything slow lives here: NVS, I2C, ULP firmware. Runs on its own
	// task so none of it sits on the boot critical path, and none of it holds
	// the LVGL lock that Podometer::init() is called under.
	void Podometer::backgroundInit(void *) {
		// NVS may not be initialised yet if no other component got there first.
		esp_err_t nvs_err = nvs_flash_init();
		if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES
				|| nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			nvs_flash_erase();
			nvs_flash_init();
		}

		// One line that says exactly what happened across the last sleep.
		// cause=5 ULP, cause=4 EXT1, cause=0 means this was a cold power-on --
		// i.e. the rail was cut and nothing could have been counted.
		esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
		ESP_UTILS_LOGI("wake cause=%d, ulp_loaded=%s, magic=%s, raw_batch=%u",
			(int)cause,
			(s_ulp_loaded == POD_MAGIC) ? "yes" : "no",
			(s_pod_rtc_magic == POD_MAGIC) ? "yes" : "no",
			(unsigned)((s_ulp_loaded == POD_MAGIC) ? ulp_batch_count : 0));

		load_config();
		load_persisted_steps();
		ESP_UTILS_LOGI("restored: steps=%d day=%d goal=%d sens=%d height=%.1f",
			(int)s_steps_today, (int)s_nvs_day_id, (int)s_cfg.daily_goal,
			(int)s_cfg.step_sensitivity, s_cfg.height_cm);

		// Fold in whatever the ULP counted while we were asleep. Done for any
		// wake cause, not just ESP_SLEEP_WAKEUP_ULP: a button press part way
		// through a batch would otherwise throw those steps away.
		//
		// The ULP was already stopped by releaseSleepPins() at the top of
		// app_main -- it has to be, or it fights the I2C driver during display
		// bring-up. These two are idempotent belt-and-braces.
		ulp_riscv_timer_stop();
		ulp_riscv_halt();
		absorb_ulp_batch();

		// The ULP is stopped and the pads are back on the digital mux, so the
		// IMU is ours again.
		if (qmi_init()) {
#if POD_ULP_MODE != POD_ULP_MODE_I2C
			qmi_enable_pedometer_engine();
#endif
		}

#if POD_OWNS_DEEP_SLEEP
		// Take over how the watch sleeps. ClockApp's default is a full PMU
		// shutdown, which cuts the rail the RTC domain and the ULP run on --
		// with that path nothing can be counted while the watch is "asleep",
		// and RTC memory (including the autostart flag) is wiped every time.
		ClockApp::setDeepSleepHandler(&Podometer::enterDeepSleepWithUlp);
#endif

		s_bg_ready = true;
		// No UI poke from here: this is not the LVGL task. If the screen is
		// open its refresh timer will pick the new values up within
		// UI_REFRESH_MS.

		s_imu_stop = false;
		if (s_imu_task == nullptr) {
			xTaskCreate(imu_task, "imu_pod", 6 * 1024, NULL, 3, &s_imu_task);
		}

		s_bg_task = nullptr;
		vTaskDelete(NULL);
	}

	bool Podometer::init() {
		// Deliberately does almost nothing. Counting starts as soon as the
		// background worker comes up, whether or not the user ever opens the
		// app, and boot is not held up waiting for I2C or flash.
		startBackgroundService();
		return true;
	}

	void Podometer::startBackgroundService() {
		if (s_bg_task != nullptr || s_bg_ready) return;
		// Priority 1 == the main task's. Deliberately not higher: at priority 2
		// it preempted app_main and raced ahead of the display bring-up.
		xTaskCreate(backgroundInit, "pod_bg", 4 * 1024, nullptr, 1, &s_bg_task);
	}

	void Podometer::releaseSleepPins() {
		// STOP THE ULP FIRST. Waking the main CPU does not stop the ULP -- its
		// timer keeps firing every POD_ULP_PERIOD_US straight through the wake.
		// In I2C mode that means it is still bit-banging SCL/SDA while the rest
		// of app_main brings the display and touch controller up. It yanks the
		// lines low in the middle of a driver transaction and the I2C master
		// blocks forever, which shows up as the task watchdog killing app_main
		// inside esp_lcd_touch_new_i2c_ft5x06().
		ulp_riscv_timer_stop();
		ulp_riscv_halt();

		// A pad hold applied before deep sleep stays latched after wake until
		// it is explicitly released. ClockApp::enterDeepSleep() turns the
		// global hold on and nothing ever turned it back off, so every pad
		// came back latched and gpio_config() calls silently did nothing.
		gpio_deep_sleep_hold_dis();

		// Undo configure_rtc_pins_for_ulp(). Must run before anything touches
		// I2C, because while these pads sit in the RTC mux the touch panel,
		// the PMU and the IMU are all unreachable.
		const gpio_num_t pins[] = {
			POD_BTN_PIN,
			POD_INT_PIN,
			POD_SCL_PIN,
			POD_SDA_PIN,
		};
		for (gpio_num_t pin : pins) {
			rtc_gpio_hold_dis(pin);
			rtc_gpio_deinit(pin);
		}

		// The ULP may have been halted mid-byte, leaving a slave part way
		// through a transfer and holding SDA low. Clock the bus until it lets
		// go, then issue a STOP. Standard I2C recovery, and cheap insurance.
		gpio_config_t scl_cfg = {};
		scl_cfg.pin_bit_mask	= 1ULL << POD_SCL_GPIO;
		scl_cfg.mode					= GPIO_MODE_OUTPUT_OD;
		scl_cfg.pull_up_en		= GPIO_PULLUP_ENABLE;
		gpio_config(&scl_cfg);

		gpio_config_t sda_cfg = {};
		sda_cfg.pin_bit_mask	= 1ULL << POD_SDA_GPIO;
		sda_cfg.mode					= GPIO_MODE_INPUT_OUTPUT_OD;
		sda_cfg.pull_up_en		= GPIO_PULLUP_ENABLE;
		gpio_config(&sda_cfg);

		gpio_set_level(POD_SDA_PIN, 1);
		for (int i = 0; i < 9 && gpio_get_level(POD_SDA_PIN) == 0; i++) {
			gpio_set_level(POD_SCL_PIN, 0);
			esp_rom_delay_us(5);
			gpio_set_level(POD_SCL_PIN, 1);
			esp_rom_delay_us(5);
		}

		// STOP: SDA low->high while SCL is high.
		gpio_set_level(POD_SDA_PIN, 0);
		esp_rom_delay_us(5);
		gpio_set_level(POD_SCL_PIN, 1);
		esp_rom_delay_us(5);
		gpio_set_level(POD_SDA_PIN, 1);
		esp_rom_delay_us(5);

		// Hand both pads back so the I2C driver can claim them.
		gpio_reset_pin(POD_SCL_PIN);
		gpio_reset_pin(POD_SDA_PIN);
	}

	bool Podometer::deinit() {
		s_imu_stop = true;
		commit_steps(0);
		save_config_if_dirty();
		return true;
	}

	bool Podometer::run() {
		Podometer_ui_init();
		s_ui_ready = true;

		// Poll the counters from the LVGL task. Nothing else is allowed to
		// touch LVGL, so there is no cross-task access to race on.
		if (s_ui_timer == nullptr) {
			s_ui_timer = lv_timer_create(
					[](lv_timer_t *) {
						ui_update_async(nullptr);
						maybe_commit_config();
					},
					UI_REFRESH_MS, nullptr);
		}

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
		save_config_if_dirty();
		commit_steps(0);
		notifyCoreClosed();
		return true;
	}

	bool Podometer::close() {
		// Closing the window must not stop the counting: the foreground task
		// and the ULP both keep running. Only persist what we have, and stop
		// poking widgets that are about to be destroyed.
		s_ui_ready = false;
		if (s_ui_timer != nullptr) {
			lv_timer_del(s_ui_timer);
			s_ui_timer = nullptr;
		}
		save_config_if_dirty();
		commit_steps(0);
		return true;
	}

	// ---- Public static API ----

	void Podometer::enterDeepSleepWithUlp() {
		// Stop the foreground counter and let it release the IMU before the
		// ULP takes the bus over.
		s_imu_stop = true;
		for (int i = 0; i < 20 && s_imu_task != nullptr; i++) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}

		// Persist anything the foreground task counted but had not flushed.
		save_config_if_dirty();
		commit_steps(0);

		enter_deep_sleep_internal();
	}

	int32_t Podometer::loadPersistedSteps() {
		nvs_handle_t h;
		if (nvs_open(POD_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
		int32_t v = 0;
		nvs_get_i32(h, POD_KEY_STEPS, &v);
		nvs_close(h);
		return v;
	}

	bool Podometer::wokeFromPodometer() {
		esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
		return (cause == ESP_SLEEP_WAKEUP_ULP ||
		        cause == ESP_SLEEP_WAKEUP_EXT1) &&
		       s_pod_rtc_magic == POD_MAGIC;
	}

	bool Podometer::handleUlpWakeAndSleep() {
		if (!wokeFromUlpBatch()) return false;

		// The user may have hit the button in the moment between the ULP
		// waking us and getting here. GPIO0 is active low: if it is down,
		// treat this as a user wake and boot to the UI instead of sleeping
		// again, otherwise the press is silently swallowed.
		if (gpio_get_level(POD_BTN_PIN) == 0) {
			ESP_UTILS_LOGI("ULP wake but button is down -> booting to UI");
			return false;
		}

		// Only what the flush needs. No display, no LVGL, no app registry.
		esp_err_t err = nvs_flash_init();
		if (err == ESP_ERR_NVS_NO_FREE_PAGES
				|| err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			nvs_flash_erase();
			nvs_flash_init();
		}
		load_config();				 // step_thr for the next ULP run
		load_persisted_steps();
		absorb_ulp_batch();

		ESP_UTILS_LOGI("ULP batch folded in, returning to sleep "
									 "without waking the screen");
		enter_deep_sleep_internal();	// does not return
		return true;
	}

	bool Podometer::wokeFromUlpBatch() {
		// ULP only. An EXT1 wake is the user pressing the button, and they
		// expect the watch face, not the step screen.
		return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP
					 && s_pod_rtc_magic == POD_MAGIC;
	}

	ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
			systems::base::App, Podometer, APP_NAME, []() {
				return std::shared_ptr<Podometer>(
						Podometer::requestInstance(), [](Podometer *p) {});
			})

}	 // namespace esp_brookesia::apps
