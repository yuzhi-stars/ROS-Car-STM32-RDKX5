#include "runtime_clock.h"
#include "main.h"

void RuntimeClock_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

uint32_t RuntimeClock_Cycles(void) { return DWT->CYCCNT; }

uint8_t RuntimeClock_BudgetExpired(uint32_t started_at, uint32_t budget_us)
{
    return (uint8_t)((uint32_t)(DWT->CYCCNT - started_at) >=
                      budget_us * (SystemCoreClock / 1000000U));
}

float RuntimeClock_Seconds(uint32_t elapsed_cycles)
{
    return (float)elapsed_cycles / (float)SystemCoreClock;
}
