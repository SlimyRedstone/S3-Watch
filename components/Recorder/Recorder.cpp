#include "Recorder.hpp"

#ifdef ESP_UTILS_LOG_TAG
	#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Recorder"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_lib_utils.h"
// #include <sys/stat.h>
#include <dirent.h>
#include <strings.h>

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_dsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_vfs_common.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "actions.h"
#include "fonts.h"
#include "images.h"
#include "screens.h"
#include "structs.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"
LV_IMG_DECLARE(recorder_icon);
}

// ---------------------------------------------------------------------------
// State + buffers
// ---------------------------------------------------------------------------
namespace {

#define REC_DIR			"/sdcard/recordings"
#define SAMPLE_RATE 44100
#define FFT_N				1024
#define BARS				32

	static volatile bool s_recording = false;
	static volatile bool s_playing	 = false;
	static TaskHandle_t s_rec_task	 = nullptr;
	static FILE *s_wav							 = nullptr;
	static uint32_t s_data_bytes		 = 0;
	static int64_t s_rec_start_us		 = 0;
	static char s_selected_path[160] = "";
	static bool s_audio_inited			 = false;

	__attribute__((aligned(16))) static float s_fft_wind[FFT_N];
	__attribute__((aligned(16))) static float s_fft_buf[FFT_N * 2];
	static float s_bar_db[BARS];
	static float s_bar_peak[BARS];

	// EEZ-bridged label texts.
	static char s_record_btn[16]		= "Record";
	static char s_play_btn[16]			= "Play";
	static char s_record_time[64]		= "Duration: 00:00:00";
	static char s_playback_time[64] = "Duration: 00:00:00";
	static char s_sd_size[64]				= "SD Card: -- G / -- G";
	static int32_t s_led_brightness = 0;

	// ---------------------------------------------------------------------------
	// WAV header (16-bit mono PCM)
	// ---------------------------------------------------------------------------
	static void write_wav_header(FILE *f, uint32_t rate, uint32_t data_size) {
		uint32_t total		 = data_size + 36;
		uint16_t ch				 = 1;
		uint16_t bits			 = 16;
		uint32_t byte_rate = rate * ch * bits / 8;
		uint16_t block		 = ch * bits / 8;
		uint32_t fmt_size	 = 16;
		uint16_t fmt			 = 1;

		fseek(f, 0, SEEK_SET);
		fwrite("RIFF", 1, 4, f);
		fwrite(&total, 4, 1, f);
		fwrite("WAVE", 1, 4, f);
		fwrite("fmt ", 1, 4, f);
		fwrite(&fmt_size, 4, 1, f);
		fwrite(&fmt, 2, 1, f);
		fwrite(&ch, 2, 1, f);
		fwrite(&rate, 4, 1, f);
		fwrite(&byte_rate, 4, 1, f);
		fwrite(&block, 2, 1, f);
		fwrite(&bits, 2, 1, f);
		fwrite("data", 1, 4, f);
		fwrite(&data_size, 4, 1, f);
	}

	// ---------------------------------------------------------------------------
	// SD helpers
	// ---------------------------------------------------------------------------
	static bool sd_ready() {
		struct stat st;
		return stat("/sdcard", &st) == 0;
	}

	static void update_sd_label() {
		/* struct statvfs s;
		if (!sd_ready() || sdmmc("/sdcard", &s) != 0) {
				snprintf(s_sd_size, sizeof(s_sd_size), "SD Card: -- G / -- G");
		} else {
				double total_g = (double)s.f_blocks * s.f_frsize / (1024.0 * 1024.0 *
		1024.0); double avail_g = (double)s.f_bavail * s.f_frsize / (1024.0 * 1024.0
		* 1024.0); snprintf(s_sd_size, sizeof(s_sd_size), "SD Card: %.1f G / %.0f
		G", avail_g, total_g);
		}
		if (Recorder_objects.sd_card_size_label)
				lv_label_set_text(Recorder_objects.sd_card_size_label, s_sd_size);
		if (Recorder_objects.sd_card_size_label2)
				lv_label_set_text(Recorder_objects.sd_card_size_label2, s_sd_size); */
	}

	// ---------------------------------------------------------------------------
	// Recording task: I2S read -> downmix -> WAV write -> FFT
	// ---------------------------------------------------------------------------
	static void rec_task(void *) {
		int16_t *raw = (int16_t *)heap_caps_aligned_alloc(
				16, FFT_N * 2 * sizeof(int16_t), MALLOC_CAP_DMA);
		int16_t *mono = (int16_t *)heap_caps_aligned_alloc(
				16, FFT_N * sizeof(int16_t), MALLOC_CAP_INTERNAL);
		float *audio = (float *)heap_caps_aligned_alloc(
				16, FFT_N * sizeof(float), MALLOC_CAP_INTERNAL);

		while (s_recording) {
			size_t bytes_read = 0;
			if (bsp_extra_i2s_read(
							raw, FFT_N * 2 * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(200))
					!= ESP_OK) {
				continue;
			}
			int frames = bytes_read / (2 * sizeof(int16_t));
			if (frames <= 0) continue;

			for (int i = 0; i < frames; i++) {
				int32_t l = raw[i * 2];
				int32_t r = raw[i * 2 + 1];
				int32_t m = (l + r) / 2;
				if (m > 32767) m = 32767;
				if (m < -32768) m = -32768;
				mono[i]	 = (int16_t)m;
				audio[i] = (float)m / 32768.0f;
			}
			// Pad if short
			for (int i = frames; i < FFT_N; i++) audio[i] = 0.0f;

			if (s_wav) {
				size_t w = fwrite(mono, sizeof(int16_t), frames, s_wav);
				s_data_bytes += (uint32_t)(w * sizeof(int16_t));
			}

			dsps_mul_f32(audio, s_fft_wind, audio, FFT_N, 1, 1, 1);
			for (int i = 0; i < FFT_N; i++) {
				s_fft_buf[2 * i]		 = audio[i];
				s_fft_buf[2 * i + 1] = 0;
			}
			dsps_fft2r_fc32(s_fft_buf, FFT_N);
			dsps_bit_rev_fc32(s_fft_buf, FFT_N);

			for (int b = 0; b < BARS; b++) {
				int idx		= b * (FFT_N / 2) / BARS;
				float re	= s_fft_buf[2 * idx];
				float im	= s_fft_buf[2 * idx + 1];
				float mag = sqrtf(re * re + im * im);
				float db	= 20.0f * log10f(mag / (FFT_N / 2) + 1e-9f);
				if (db > 0) db = 0;
				if (db < -90) db = -90;
				s_bar_db[b] = db;
			}
		}

		free(raw);
		free(mono);
		free(audio);
		s_rec_task = nullptr;
		vTaskDelete(NULL);
	}

	// ---------------------------------------------------------------------------
	// Start / stop record + playback
	// ---------------------------------------------------------------------------
	static bool start_recording() {
		if (s_recording || !sd_ready()) return false;

		mkdir(REC_DIR, 0775);

		char path[160];
		time_t t;
		time(&t);
		struct tm tm;
		localtime_r(&t, &tm);
		snprintf(
				path, sizeof(path), REC_DIR "/rec_%04d%02d%02d_%02d%02d%02d.wav",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
				tm.tm_sec);

		s_wav = fopen(path, "wb");
		if (!s_wav) return false;
		write_wav_header(s_wav, SAMPLE_RATE, 0);
		s_data_bytes	 = 0;
		s_rec_start_us = esp_timer_get_time();

		bsp_extra_codec_set_fs(SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO);

		s_recording = true;
		xTaskCreate(rec_task, "rec", 16 * 1024, NULL, 2, &s_rec_task);
		return true;
	}

	static void stop_recording() {
		if (!s_recording) return;
		s_recording = false;
		while (s_rec_task != nullptr) vTaskDelay(pdMS_TO_TICKS(10));
		if (s_wav) {
			write_wav_header(s_wav, SAMPLE_RATE, s_data_bytes);
			fflush(s_wav);
			fclose(s_wav);
			s_wav = nullptr;
		}
		s_led_brightness = 0;
		if (Recorder_objects.record_led)
			lv_led_set_brightness(Recorder_objects.record_led, 0);
	}

	// ---------------------------------------------------------------------------
	// File list
	// ---------------------------------------------------------------------------
	static void on_file_clicked(lv_event_t *e) {
		lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
		const char *name
				= lv_list_get_button_text(Recorder_objects.record_files_list, btn);
		if (!name) return;
		snprintf(s_selected_path, sizeof(s_selected_path), REC_DIR "/%s", name);

		FILE *f				 = fopen(s_selected_path, "rb");
		uint32_t dsize = 0;
		if (f) {
			fseek(f, 40, SEEK_SET);
			if (fread(&dsize, 4, 1, f) != 1) dsize = 0;
			fclose(f);
		}
		uint32_t secs = dsize / (SAMPLE_RATE * 2);
		uint32_t hh		= secs / 3600;
		uint32_t mm		= (secs / 60) % 60;
		uint32_t ss		= secs % 60;
		snprintf(
				s_playback_time, sizeof(s_playback_time), "Duration: %02u:%02u:%02u",
				(unsigned)hh, (unsigned)mm, (unsigned)ss);
		set_var_playback_time_text(s_playback_time);
	}

	static void refresh_file_list() {
		if (!Recorder_objects.record_files_list) return;
		lv_obj_clean(Recorder_objects.record_files_list);

		// Collect filenames first so we can sort.
		static constexpr int MAX_FILES = 128;
		static char names[MAX_FILES][96];
		int n = 0;

		DIR *d = opendir(REC_DIR);
		if (!d) return;
		struct dirent *de;
		while ((de = readdir(d)) != NULL && n < MAX_FILES) {
			const char *name = de->d_name;
			size_t l				 = strlen(name);
			if (l < 4 || strcasecmp(name + l - 4, ".wav") != 0) continue;
			strncpy(names[n], name, sizeof(names[0]) - 1);
			names[n][sizeof(names[0]) - 1] = 0;
			n++;
		}
		closedir(d);

		// Newest first. Filenames are rec_YYYYmmdd_HHMMSS.wav so descending
		// lexicographic order = chronologically newest first.
		qsort(names, n, sizeof(names[0]), [](const void *a, const void *b) -> int {
			return strcmp((const char *)b, (const char *)a);
		});

		for (int i = 0; i < n; i++) {
			lv_obj_t *btn = lv_list_add_btn(
					Recorder_objects.record_files_list, LV_SYMBOL_FILE, names[i]);
			lv_obj_add_event_cb(btn, on_file_clicked, LV_EVENT_CLICKED, NULL);
		}
	}

	// ---------------------------------------------------------------------------
	// Spectrum draw + status tick (LV-context timers)
	// ---------------------------------------------------------------------------
	static void draw_spectrum_cb(lv_timer_t *) {
		lv_obj_t *canvas = Recorder_objects.spectrum_canvas;
		if (!canvas) return;
		int W = lv_obj_get_width(canvas);
		int H = lv_obj_get_height(canvas);
		if (W <= 0 || H <= 0) return;

		lv_layer_t layer;
		lv_canvas_init_layer(canvas, &layer);
		lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

		int sw	 = W / BARS;
		int cy	 = H / 2;
		int half = H / 2;
		for (int i = 0; i < BARS; i++) {
			float db = s_recording ? s_bar_db[i] : -90.0f;
			float n	 = (db + 90.0f) / 90.0f;
			if (n < 0) n = 0;
			if (n > 1) n = 1;
			n			= sqrtf(n);
			int h = (int)(n * half);

			if (s_bar_peak[i] < h)
				s_bar_peak[i] = h;
			else {
				s_bar_peak[i] -= 2;
				if (s_bar_peak[i] < 0) s_bar_peak[i] = 0;
			}

			lv_color_t c
					= lv_color_hsv_to_rgb((uint16_t)(i * 270.0f / BARS), 100, 100);
			lv_draw_rect_dsc_t rd;
			lv_draw_rect_dsc_init(&rd);
			rd.bg_color = c;
			rd.bg_opa		= LV_OPA_COVER;

			lv_area_t bar = { i * sw + 1, cy - h, (i + 1) * sw - 2, cy + h };
			lv_draw_rect(&layer, &rd, &bar);

			lv_area_t pt = { i * sw + 1, cy - (int)s_bar_peak[i] - 2,
											 (i + 1) * sw - 2, cy - (int)s_bar_peak[i] };
			lv_area_t pb = { i * sw + 1, cy + (int)s_bar_peak[i], (i + 1) * sw - 2,
											 cy + (int)s_bar_peak[i] + 2 };
			lv_draw_rect(&layer, &rd, &pt);
			lv_draw_rect(&layer, &rd, &pb);
		}
		lv_canvas_finish_layer(canvas, &layer);
	}

	static void status_tick_cb(lv_timer_t *) {
		if (s_recording) {
			if (Recorder_objects.record_led)
				lv_led_set_brightness(Recorder_objects.record_led, 255);
			int64_t elapsed = (esp_timer_get_time() - s_rec_start_us) / 1000000;
			int hh					= (int)(elapsed / 3600);
			int mm					= (int)((elapsed / 60) % 60);
			int ss					= (int)(elapsed % 60);
			snprintf(
					s_record_time, sizeof(s_record_time), "Duration: %02d:%02d:%02d", hh,
					mm, ss);
			set_var_record_time_text(s_record_time);
		} else {
			if (Recorder_objects.record_led)
				lv_led_set_brightness(Recorder_objects.record_led, 0);
		}

		if (s_playing && !bsp_extra_player_is_playing_by_path(s_selected_path)) {
			s_playing = false;
			set_var_play_button_text("Play");
		}

		update_sd_label();

		bool has_sd = sd_ready();
		if (Recorder_objects.record_button) {
			if (has_sd)
				lv_obj_clear_state(Recorder_objects.record_button, LV_STATE_DISABLED);
			else
				lv_obj_add_state(Recorder_objects.record_button, LV_STATE_DISABLED);
		}
		if (Recorder_objects.play_button) {
			bool enable = has_sd && s_selected_path[0] != 0;
			if (enable)
				lv_obj_clear_state(Recorder_objects.play_button, LV_STATE_DISABLED);
			else
				lv_obj_add_state(Recorder_objects.play_button, LV_STATE_DISABLED);
		}
	}

	// ---------------------------------------------------------------------------
	// One-shot audio init
	// ---------------------------------------------------------------------------
	static void ensure_audio_init() {
		if (s_audio_inited) return;
		dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
		dsps_wind_hann_f32(s_fft_wind, FFT_N);
		bsp_extra_codec_init();
		bsp_extra_player_init();
		bsp_extra_codec_volume_set(60, NULL);
		s_audio_inited = true;
	}

}	 // anonymous namespace

// ===========================================================================
// EEZ-callable C API: actions + var getters/setters
// ===========================================================================
extern "C" {

void action_record_button_pressed(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	if (!s_recording) {
		if (start_recording()) set_var_record_button_text("Stop");
	} else {
		stop_recording();
		set_var_record_button_text("Record");
		refresh_file_list();
	}
}

void action_play_button_pressed(lv_event_t *e) {
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	if (!s_playing) {
		if (s_selected_path[0] == 0 || !sd_ready()) return;
		bsp_extra_codec_set_fs(SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO);
		if (bsp_extra_player_play_file(s_selected_path) == ESP_OK) {
			s_playing = true;
			set_var_play_button_text("Stop");
		}
	} else {
		bsp_extra_codec_dev_stop();
		s_playing = false;
		set_var_play_button_text("Play");
	}
}

void action_volume_slider_changed(lv_event_t *e) {
	lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
	int v						 = (int)lv_slider_get_value(slider);
	bsp_extra_codec_volume_set(v, NULL);
}

void action_file_list_selected(lv_event_t *e) { (void)e; }

// EEZ flow string vars + LED brightness — keep the bridges they expect.
int32_t get_var_recording_led_brightness() { return s_led_brightness; }
void set_var_recording_led_brightness(int32_t v) {
	s_led_brightness = v;
	if (Recorder_objects.record_led)
		lv_led_set_brightness(Recorder_objects.record_led, v);
}

const char *get_var_sd_card_size_text() { return s_sd_size; }
void set_var_sd_card_size_text(const char *v) {
	strncpy(s_sd_size, v, sizeof(s_sd_size) - 1);
	if (Recorder_objects.sd_card_size_label)
		lv_label_set_text(Recorder_objects.sd_card_size_label, s_sd_size);
	if (Recorder_objects.sd_card_size_label2)
		lv_label_set_text(Recorder_objects.sd_card_size_label2, s_sd_size);
}

const char *get_var_record_time_text() { return s_record_time; }
void set_var_record_time_text(const char *v) {
	strncpy(s_record_time, v, sizeof(s_record_time) - 1);
	if (Recorder_objects.record_time_label)
		lv_label_set_text(Recorder_objects.record_time_label, s_record_time);
}

const char *get_var_record_button_text() { return s_record_btn; }
void set_var_record_button_text(const char *v) {
	strncpy(s_record_btn, v, sizeof(s_record_btn) - 1);
	if (Recorder_objects.record_button_label)
		lv_label_set_text(Recorder_objects.record_button_label, s_record_btn);
}

const char *get_var_play_button_text() { return s_play_btn; }
void set_var_play_button_text(const char *v) {
	strncpy(s_play_btn, v, sizeof(s_play_btn) - 1);
	if (Recorder_objects.play_button_label)
		lv_label_set_text(Recorder_objects.play_button_label, s_play_btn);
}

const char *get_var_playback_time_text() { return s_playback_time; }
void set_var_playback_time_text(const char *v) {
	strncpy(s_playback_time, v, sizeof(s_playback_time) - 1);
	if (Recorder_objects.playback_time_label)
		lv_label_set_text(Recorder_objects.playback_time_label, s_playback_time);
}

}	 // extern "C"

// ===========================================================================
// Brookesia App lifecycle
// ===========================================================================
namespace esp_brookesia::apps {

	static constexpr char APP_NAME[] = "Recorder";

	Recorder *Recorder::_instance = nullptr;

	Recorder *Recorder::requestInstance(
			bool use_status_bar, bool use_navigation_bar) {
		if (_instance == nullptr)
			_instance = new Recorder(use_status_bar, use_navigation_bar);
		return _instance;
	}

	Recorder::Recorder(bool use_status_bar, bool use_navigation_bar)
			: App(APP_NAME, &recorder_icon, /*use_default_screen=*/true,
						use_status_bar, use_navigation_bar) {}

	Recorder::~Recorder() { _instance = nullptr; }

	bool Recorder::run() {
		Recorder_ui_init();
		ensure_audio_init();

		// Initial UI state.
		if (Recorder_objects.record_led)
			lv_led_set_brightness(Recorder_objects.record_led, 0);
		set_var_record_button_text("Record");
		set_var_play_button_text("Play");
		set_var_playback_time_text("Duration: 00:00:00");
		set_var_record_time_text("Duration: 00:00:00");
		update_sd_label();
		refresh_file_list();

		// Periodic LV timers (run with LV lock held).
		// lv_timer_create(draw_spectrum_cb, 100,  NULL);
		lv_timer_create(status_tick_cb, 500, NULL);

		return true;
	}

	bool Recorder::back() {
		notifyCoreClosed();
		return true;
	}

	bool Recorder::close() {
		if (s_recording) stop_recording();
		if (s_playing) {
			bsp_extra_codec_dev_stop();
			s_playing = false;
		}
		return true;
	}

	ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
			systems::base::App, Recorder, APP_NAME, []() {
				return std::shared_ptr<Recorder>(
						Recorder::requestInstance(), [](Recorder *p) {});
			})

}	 // namespace esp_brookesia::apps
