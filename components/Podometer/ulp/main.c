/*
 * Podometer ULP firmware.
 *
 * Runs on the ULP RISC-V co-processor, which stays alive while the main
 * ESP32-S3 is in deep sleep. Woken by the ULP timer every
 * POD_ULP_PERIOD_US, it counts steps and wakes the main CPU once a full
 * batch has accumulated. The host then flushes the batch to NVS and goes
 * back to sleep.
 *
 * Two counting strategies, selected by POD_ULP_MODE in pod_ulp_shared.h:
 *
 *   POD_ULP_MODE_I2C  Bit-bang the shared I2C bus, read the accelerometer
 *                     and run the same baseline/threshold/refractory
 *                     detector the foreground task uses. Self-contained.
 *
 *   POD_ULP_MODE_INT  Count rising edges on the QMI8658C interrupt line.
 *                     Cheaper, but relies on the pedometer engine emitting
 *                     one pulse per step and on that pulse being wider
 *                     than the ULP timer period.
 *
 * main() is re-entered from scratch on every tick. Anything that must
 * survive between ticks lives in a global: globals sit in RTC slow memory
 * and persist across both ULP ticks and main-CPU deep sleep.
 */

#include <stdint.h>

#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#include "pod_ulp_shared.h"

/* ---- Host-visible state. The linker exports each as ulp_<name>. ---- */
volatile uint32_t batch_count = 0;	 /* steps since the host last read */
volatile uint32_t total_pulses = 0;	 /* lifetime count, debug only */
volatile uint32_t batch_target = POD_BATCH_TARGET;
volatile uint32_t last_level = 0;		 /* INT mode: previous pin level */
volatile uint32_t step_thr = 100;		 /* residual threshold, POD_ONE_G units */
volatile uint32_t refract_ticks = 5; /* ticks ignored after a counted step */
volatile uint32_t baseline = POD_ONE_G;	/* gravity tracker */
volatile uint32_t last_mag = 0;			 /* most recent magnitude, debug only */
volatile uint32_t i2c_errors = 0;		 /* failed bus transactions, debug only */

/* Not read by the host, but must survive between ticks. */
static uint32_t refract_left = 0;

#if POD_ULP_MODE == POD_ULP_MODE_I2C

/*
 * Software I2C master.
 *
 * Open-drain is emulated the classic way: to drive a line low we enable the
 * output driver with a 0 level, to release it we disable the output driver
 * and let the bus pull-ups take the line high. The pins are put into the RTC
 * IO mux with their input buffers enabled by the host before ulp_riscv_run().
 */

#define I2C_DELAY() ulp_riscv_delay_cycles(POD_I2C_HALF_CYCLES)

static inline void sda_low(void) {
	ulp_riscv_gpio_output_level((gpio_num_t)POD_SDA_GPIO, 0);
	ulp_riscv_gpio_output_enable((gpio_num_t)POD_SDA_GPIO);
}

static inline void sda_release(void) {
	ulp_riscv_gpio_output_disable((gpio_num_t)POD_SDA_GPIO);
}

static inline void scl_low(void) {
	ulp_riscv_gpio_output_level((gpio_num_t)POD_SCL_GPIO, 0);
	ulp_riscv_gpio_output_enable((gpio_num_t)POD_SCL_GPIO);
}

static inline uint32_t sda_read(void) {
	return ulp_riscv_gpio_get_level((gpio_num_t)POD_SDA_GPIO);
}

/* Release SCL and wait for it to actually go high, so a slave that stretches
 * the clock is honoured. Returns 0 if the line never came up. */
static int scl_release(void) {
	ulp_riscv_gpio_output_disable((gpio_num_t)POD_SCL_GPIO);
	for (int i = 0; i < POD_I2C_STRETCH_TRIES; i++) {
		if (ulp_riscv_gpio_get_level((gpio_num_t)POD_SCL_GPIO)) return 1;
		I2C_DELAY();
	}
	return 0;
}

static void i2c_start(void) {
	/* Works as both a START and a repeated START. */
	sda_release();
	scl_release();
	I2C_DELAY();
	sda_low();
	I2C_DELAY();
	scl_low();
	I2C_DELAY();
}

static void i2c_stop(void) {
	sda_low();
	I2C_DELAY();
	scl_release();
	I2C_DELAY();
	sda_release();
	I2C_DELAY();
}

/* Returns 1 if the slave ACKed. */
static int i2c_write_byte(uint32_t b) {
	for (int i = 0; i < 8; i++) {
		if (b & 0x80u) {
			sda_release();
		} else {
			sda_low();
		}
		b <<= 1;
		I2C_DELAY();
		if (!scl_release()) return 0;
		I2C_DELAY();
		scl_low();
		I2C_DELAY();
	}

	/* Ninth clock: sample ACK from the slave. */
	sda_release();
	I2C_DELAY();
	if (!scl_release()) return 0;
	uint32_t ack = (sda_read() == 0);
	I2C_DELAY();
	scl_low();
	I2C_DELAY();
	return (int)ack;
}

static uint32_t i2c_read_byte(int send_ack) {
	uint32_t v = 0;

	sda_release();
	for (int i = 0; i < 8; i++) {
		I2C_DELAY();
		if (!scl_release()) return 0;
		v = (v << 1) | sda_read();
		I2C_DELAY();
		scl_low();
	}

	/* Ninth clock: ACK to continue reading, NACK to end the transfer. */
	if (send_ack) {
		sda_low();
	} else {
		sda_release();
	}
	I2C_DELAY();
	scl_release();
	I2C_DELAY();
	scl_low();
	sda_release();
	I2C_DELAY();

	return v;
}

/* Burst-read the six accelerometer bytes. Returns 0 on any bus error. */
static int qmi_read_accel(int32_t *ax, int32_t *ay, int32_t *az) {
	uint32_t raw[6];

	i2c_start();
	if (!i2c_write_byte(POD_QMI_ADDR << 1)) goto fail;
	if (!i2c_write_byte(POD_QMI_AX_L)) goto fail;

	i2c_start(); /* repeated start */
	if (!i2c_write_byte((POD_QMI_ADDR << 1) | 1)) goto fail;

	for (int i = 0; i < 6; i++) {
		raw[i] = i2c_read_byte(i < 5); /* NACK the last byte */
	}
	i2c_stop();

	*ax = (int32_t)(int16_t)((raw[1] << 8) | raw[0]);
	*ay = (int32_t)(int16_t)((raw[3] << 8) | raw[2]);
	*az = (int32_t)(int16_t)((raw[5] << 8) | raw[4]);
	return 1;

fail:
	i2c_stop();
	return 0;
}

/* Integer square root. No FPU on the ULP, and a 16-iteration restoring
 * sqrt at 20 Hz is nothing. */
static uint32_t isqrt32(uint32_t v) {
	uint32_t rem = 0;
	uint32_t root = 0;

	for (int i = 0; i < 16; i++) {
		root <<= 1;
		rem = (rem << 2) | (v >> 30);
		v <<= 2;
		if (root < rem) {
			rem -= root | 1u;
			root += 2;
		}
	}
	return root >> 1;
}

#endif /* POD_ULP_MODE_I2C */

int main(void) {
#if POD_ULP_MODE == POD_ULP_MODE_I2C
	int32_t ax, ay, az;

	if (!qmi_read_accel(&ax, &ay, &az)) {
		i2c_errors++;
		return 0;
	}

	/* Scale down so the sum of squares fits in int32. */
	ax >>= POD_ACC_SHIFT;
	ay >>= POD_ACC_SHIFT;
	az >>= POD_ACC_SHIFT;

	uint32_t mag = isqrt32((uint32_t)(ax * ax + ay * ay + az * az));
	last_mag = mag;

	/* Slow baseline tracks gravity, so tilting the wrist does not count. */
	int32_t base = (int32_t)baseline;
	base += ((int32_t)mag - base) >> POD_BASELINE_SHIFT;
	baseline = (uint32_t)base;

	if (refract_left) {
		refract_left--;
		return 0;
	}

	if (((int32_t)mag - base) > (int32_t)step_thr) {
		batch_count++;
		total_pulses++;
		refract_left = refract_ticks;
	}
#else /* POD_ULP_MODE_INT */
	uint32_t level = ulp_riscv_gpio_get_level((gpio_num_t)POD_INT_GPIO);

	if (level == 1 && last_level == 0) {
		batch_count++;
		total_pulses++;
	}
	last_level = level;
#endif

	if (batch_count >= batch_target) {
		ulp_riscv_wakeup_main_processor();
	}
	return 0;
}
