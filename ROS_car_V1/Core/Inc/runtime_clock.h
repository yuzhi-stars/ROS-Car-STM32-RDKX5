#ifndef RUNTIME_CLOCK_H
#define RUNTIME_CLOCK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Cortex-M4 cycle counter. Initialize after SystemClock_Config(). Unsigned
 * subtraction handles wrap for intervals shorter than one CYCCNT period. */
void RuntimeClock_Init(void);
uint32_t RuntimeClock_Cycles(void);
uint8_t RuntimeClock_BudgetExpired(uint32_t started_at, uint32_t budget_us);
float RuntimeClock_Seconds(uint32_t elapsed_cycles);
#ifdef __cplusplus
}
#endif
#endif
