#pragma once

/*
 * Constants shared between the host (Podometer.cpp) and the ULP RISC-V
 * firmware (ulp/main.c).
 *
 * The ULP toolchain compiles a freestanding RISC-V target: keep this header
 * free of IDF host headers, C++ and enums. Plain integer macros only.
 */

/* ---- ULP counting strategy -------------------------------------------
 * POD_ULP_MODE_I2C : the ULP drives a software I2C master on the shared
 *                    bus, reads the QMI8658C accelerometer and runs the
 *                    same step detector the foreground task uses.
 *                    Does not depend on the pedometer engine at all.
 * POD_ULP_MODE_INT : the ULP counts rising edges on the QMI8658C interrupt
 *                    line. Depends on the pedometer engine emitting one
 *                    pulse per step, which the Rev 0.9 datasheet does not
 *                    document.
 *
 * Both are compiled into the source. Flip this to A/B them on hardware.
 */
#define POD_ULP_MODE_I2C 1
#define POD_ULP_MODE_INT 2

#ifndef POD_ULP_MODE
	#define POD_ULP_MODE POD_ULP_MODE_I2C
#endif

/* ---- Pins. All must be RTC-capable, i.e. GPIO0..21 on ESP32-S3. ---- */
#define POD_SCL_GPIO 14 /* shared I2C bus: touch + PMU + IMU */
#define POD_SDA_GPIO 15
#define POD_INT_GPIO 21 /* QMI8658C INT1 */
#define POD_BTN_GPIO 0	/* BOOT button, ext1 wake source */

/* ---- QMI8658C ---- */
#define POD_QMI_ADDR 0x6B
#define POD_QMI_AX_L 0x35 /* accel X low, 6 bytes of XYZ follow */

/* ---- Detector scaling ----
 * CTRL2 is programmed 0x25, whose aFS field (bits 6:4) is 0b010 = +-8g, so
 * 1g = 4096 LSB. The old code's comments claimed +-4g / 8192 LSB and scaled
 * accordingly, which made every reading come out at half its real value.
 * Confirmed on hardware: a watch at rest logged last_mag=260, i.e. 4096 >> 4.
 *
 * The ULP shifts each axis right by POD_ACC_SHIFT before squaring so that
 * ax^2 + ay^2 + az^2 stays well inside int32. After the shift 1g == POD_ONE_G.
 */
#define POD_ACC_SHIFT 4
#define POD_ONE_G			256 /* 4096 >> 4 */
#define POD_LSB_PER_G 4096.0f

/* Baseline low-pass shift. At POD_ULP_PERIOD_US this is roughly a 1 s
 * time constant, matching the foreground detector. */
#define POD_BASELINE_SHIFT 4

/* ULP timer period. 20 Hz is ample for a <=4 Hz step rate and costs far
 * less average current than the 50 Hz the foreground task polls at. */
#define POD_ULP_PERIOD_US 50000

/* Half-bit delay for the bit-banged I2C, in ULP cycles. The ULP RISC-V
 * runs at 17.5 MHz, so 88 cycles ~= 5 us -> ~100 kHz SCL. */
#define POD_I2C_HALF_CYCLES 88

/* Bounded wait for clock stretching, in half-bit periods. */
#define POD_I2C_STRETCH_TRIES 100

/* Who owns the watch's sleep.
 *
 * 1 = the Podometer installs a handler on ClockApp so the watch uses real
 *     ESP32-S3 deep sleep. The RTC domain stays powered, so the ULP keeps
 *     counting. Costs roughly 100-200 uA of standby current.
 * 0 = leave ClockApp's default, a full AXP2101 shutdown. Lowest possible
 *     standby current, but every rail dies, so the ULP stops and no steps
 *     are counted while the watch is off.
 */
#ifndef POD_OWNS_DEEP_SLEEP
	#define POD_OWNS_DEEP_SLEEP 1
#endif

/* AXP2101 rails to switch OFF during ULP sleep. Default 0 = drop nothing.
 *
 * LEAVE THIS AT 0 ON THIS BOARD. Confirmed by the hardware owner: DC1 feeds
 * the ESP32-S3, the QMI8658C *and* the display. DC1 is never droppable -- it
 * is the rail the chip runs on -- so there is no display or IMU current to be
 * saved by dropping anything else, and the remaining rails are small.
 *
 * The panel is put to sleep properly instead, with DISPOFF + SLPIN via
 * bsp_display_enter_sleep(), which is the only way to quieten a display that
 * shares the CPU rail.
 *
 * The risk of getting this wrong is not symmetric. Cutting a rail is a one-way
 * door: if the rail also feeds the I2C pull-ups or the AXP2101's own interface,
 * the ESP32-S3 cannot reach the PMU on the next boot to switch it back on. The
 * watch then wakes with a dead screen and nothing in software can fix it --
 * recovery is a >4 s power-key press to force the PMU off, resetting its
 * registers to defaults. That has already happened once on this board.
 *
 * If you ever do experiment, add ONE rail, sleep, wake, confirm the screen
 * returns and i2c_errors did not climb, then add the next:
 *
 *   DC2 0x0001  DC3 0x0002  DC4 0x0004  DC5 0x0008
 *   ALDO1 0x0010  ALDO2 0x0020  ALDO3 0x0040  ALDO4 0x0080
 *   BLDO1 0x0100  BLDO2 0x0200
 *   CPUSLDO 0x0400  DLDO1 0x0800  DLDO2 0x1000
 */
#ifndef POD_SLEEP_DROP_RAILS
	#define POD_SLEEP_DROP_RAILS 0
#endif

/* Steps the ULP accumulates before it wakes the host to flush them to NVS.
 * Larger = fewer wakeups = less power, but more steps at risk if the
 * battery dies mid-batch. */
#define POD_BATCH_TARGET 100
