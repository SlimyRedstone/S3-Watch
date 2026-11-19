#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

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
#define SOUND_GRAPH_Y_MIN           30                  // dB, bottom of the chart
#define SOUND_GRAPH_Y_MAX           120                 // dB, top of the chart (louder peaks leave the plot area)
#define SOUND_GRAPH_DIV_H           5                   // horizontal grid lines
#define SOUND_GRAPH_DIV_V           6                   // vertical grid lines

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
