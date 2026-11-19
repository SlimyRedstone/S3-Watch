#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"
#include "sdkconfig.h"

/*
@brief Microphone input: ES7210 ADC, MIC1 on the left slot, MIC2 on the right slot
@note Opened through bsp_extra (shared with the ES8311 speaker codec and the Recorder app)
@note 32-bit slots: the ES7210 24-bit result is left-aligned in each int32, and the I2S master clock stays at 256 x fs
*/
#define I2S_MIC_SAMPLE_RATE             (uint32_t)48000     // the A-weighting coefficients are computed for 48 kHz
#define I2S_MIC_BITS_PER_CHANNEL        (uint32_t)32
#define I2S_MIC_CHANNELS                (uint32_t)2         // ES7210 default: MIC1 + MIC2, I2S stereo
#define I2S_MIC_FRAME_BYTES             (I2S_MIC_CHANNELS * I2S_MIC_BITS_PER_CHANNEL / 8)
#define I2S_MIC_READ_FRAMES             (uint32_t)240       // frames per read, 5 ms @ 48 kHz (one BSP DMA buffer)
#define SOUND_MIC_CHANNEL               0                   // 0 = MIC1 (top, next to the motor), 1 = MIC2 (bottom)

/*
@brief Level calibration: 94 dB SPL (1 Pa) at the mic, expressed in dBFS at the ADC output
@note SOUND_REF_DBFS = sensitivity + PGA gain - ADC full scale, replaces the INMP441's -26 dBFS
@warning SOUND_MIC_SENSITIVITY_DBV is a typical analog MEMS value, the board's mic part is not published: replace it with the datasheet value
@note Full scale = 94 + |SOUND_REF_DBFS| dB SPL (about 114 dB SPL with these values); lower the PGA gain to measure louder sounds
*/
#define SOUND_MIC_SENSITIVITY_DBV       (-38.0f)            // dBV/Pa at 1 kHz, UNVERIFIED
#define SOUND_ADC_PGA_GAIN_DB           (24.0f)             // must equal CODEC_DEFAULT_ADC_VOLUME (bsp_extra), ES7210 steps are 3 dB
#define SOUND_ADC_FULL_SCALE_VRMS       (2.0f)              // ES7210: 2 * AVDD / 3.3 Vrms differential, AVDD = 3.3 V

#define SOUND_LEVEL_MINIMUM     (int32_t)30
#define SOUND_LEVEL_MAXIMUM     (int32_t)120

/*
@brief Sonometer display update rate, in Hz
@note Use a divisor of I2S_MIC_SAMPLE_RATE (48000: 10, 20, 25, 30, 50, 60...) for exact timing
@note Does not change the "Fast" 125 ms time weighting, only how often the value is refreshed
*/
#define SOUND_UPDATE_RATE_HZ    (uint32_t)10
static_assert(SOUND_UPDATE_RATE_HZ > 0 && SOUND_UPDATE_RATE_HZ <= 1000, "SOUND_UPDATE_RATE_HZ must be 1..1000");

/*
@brief Sound graph (Sonometer_objects.sound_graph): one point every SOUND_GRAPH_PERIOD_MS, peak level of that period
@note Range, point count, shift mode and grid are applied by the app after the EEZ screen is built, so a UI re-export keeps them
@note Updated by an LVGL timer, so it runs in the LVGL task and needs no display lock
*/
#define SOUND_GRAPH_PERIOD_MS       1000
#define SOUND_GRAPH_COLOR_HEX       0xFFFFFF
#define SOUND_GRAPH_POINTS          60                  // 60 s of history at one point per second
#define SOUND_GRAPH_Y_MIN           30                  // dB, bottom of the chart, when the UI has no graph_scale
#define SOUND_GRAPH_Y_MAX           105                 // dB, top of the chart, when the UI has no graph_scale
#define SOUND_GRAPH_DIV_H           5                   // horizontal grid lines, when the UI has no graph_scale
#define SOUND_GRAPH_DIV_V           6                   // vertical grid lines

/*
@brief FFT graph (Sonometer_objects.fft_graph): live spectrum, bars on a logarithmic frequency axis
@note FFT of the last FFT_SIZE raw mic samples (no A-weighting), DC removed, Hann window, while the FFT tab is shown
@note Real input: an FFT_SIZE-point FFT gives FFT_BINS distinct bins (the upper half mirrors them): 4096 bins of 5.86 Hz @ 8192
@note Bars split the fft_frequency_scale range (Hz, set in the UI) into equal frequency ratios, their count comes from n_bars_selector;
      a bar is the mean power of the bins centred in it, interpolated between the 2 nearest bins when it is narrower than a bin
@note More bars than plot pixels: one column per pixel, showing the highest bar it covers
@note The bars are rendered into one RGB565A8 image drawn over the chart: a bar series would cost LVGL 9.2 one rounded-rect
      draw task per bar per refresh stripe (3-4 fps at 128+ bars)
@note Levels in dB SPL per bin, with the level's calibration: a pure tone reads about 1.8 dB below its level on a 1-bin bar,
      lower on bars averaging many bins
*/
#define FFT_SIZE                    CONFIG_DSP_MAX_FFT_SIZE // esp-dsp table size (menuconfig: DSP_MAX_FFT_SIZE)
#define FFT_BINS                    (FFT_SIZE / 2)
#define FFT_UPDATE_PERIOD_MS        33                  // 30 spectra per second, each one spans FFT_SIZE samples (171 ms @ 8192)
#define FFT_GRAPH_COLOR_HEX         0xFFFFFF
#define FFT_GRAPH_GAP_FROM_PX       4                   // bars at least this wide get a 1 px gap on their right
#define FFT_GRAPH_DB_MIN            0                   // dB SPL per bin, bottom of the chart
#define FFT_GRAPH_DB_MAX            100                 // dB SPL per bin, top of the chart
#define FFT_GRAPH_F_MIN             20                  // Hz, left end, when the UI has no fft_frequency_scale
#define FFT_GRAPH_F_MAX             20000               // Hz, right end, when the UI has no fft_frequency_scale
#define FFT_GRAPH_BARS_DEFAULT      32                  // when the UI has no n_bars_selector
#define FFT_SCALE_UNITS_PER_OCTAVE  1000                // fft_frequency_scale range unit once it is logarithmic
#define FFT_SCALE_LABELS_MAX        32
static_assert(FFT_SIZE >= 256 && (FFT_SIZE & (FFT_SIZE - 1)) == 0, "FFT_SIZE must be a power of 2");

/*
@brief EEZ variables refresh (Sonometer_ui_tick), same period as the original display loop
*/
#define SOUND_UI_TICK_MS            16

/*
@brief Sound task, created when the app opens and stopped when it closes
*/
#define SOUND_TASK_STACK_SIZE       (5 * 1024)
#define SOUND_TASK_PRIORITY         3
#define SOUND_TASK_CORE             0
#define SOUND_TASK_STOP_TIMEOUT_MS  1500                // > one codec read timeout (1000 ms)

/*
@brief FFT task, created and stopped with the sound task, idle while the FFT tab is hidden
@note Same core, lower priority than the sound task: the level loop always preempts it
*/
#define FFT_TASK_STACK_SIZE         (4 * 1024)
#define FFT_TASK_PRIORITY           2
#define FFT_TASK_CORE               0

namespace esp_brookesia::apps {

class Sonometer : public systems::phone::App {
public:
    static Sonometer *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~Sonometer();

    bool run()  override;
    bool back() override;
    bool close() override;

protected:
    Sonometer(bool use_status_bar, bool use_navigation_bar);

private:
    static Sonometer *_instance;
};

} // namespace esp_brookesia::apps
