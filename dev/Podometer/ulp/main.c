/*
 * Podometer ULP firmware.
 *
 * Wakes via the ULP timer every 50 ms. Polls QMI_INT1 (GPIO21). Each
 * rising edge = one step pulse from the QMI8658C pedometer engine.
 * Maintains a counter. When the counter reaches STEP_BATCH the ULP
 * wakes the main CPU; the host reads, flushes to NVS, resets counter,
 * goes back to deep sleep.
 */

#include <stdint.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
// #include "ulp/ulp_riscv/ulp_core/include/ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"

#define QMI_INT1_GPIO   GPIO_NUM_21
#define STEP_BATCH      100

/* Shared variables (read/write from host via ulp_<name>) */
volatile uint32_t batch_count   = 0;   /* steps since last host read */
volatile uint32_t total_pulses  = 0;   /* lifetime debug counter */
volatile uint32_t batch_target  = STEP_BATCH;
volatile uint32_t last_level    = 0;   /* previous GPIO state for edge detect */

int main(void)
{
    /* Read current GPIO level */
    int level = ulp_riscv_gpio_get_level(QMI_INT1_GPIO);

    /* Rising-edge detect */
    if (level == 1 && last_level == 0) {
        batch_count++;
        total_pulses++;
    }
    last_level = level;

    /* Wake host once we have a full batch */
    if (batch_count >= batch_target) {
        ulp_riscv_wakeup_main_processor();
    }
    return 0;
}
