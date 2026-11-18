#include "pm_platform.h"
#include "pm_osal.h"
#include "stm32f1xx_hal.h"

/* Clock tree: HSE 8 MHz -> PLL x9 -> SYSCLK 72 MHz,
 * AHB /1 (72 MHz), APB1 /2 (36 MHz), APB2 /1 (72 MHz).
 *
 * The vendored CMSIS SystemInit() only sets the vector table; it does not
 * program the PLL, so without this the core would stay on HSI 8 MHz.
 * If the crystal fails to start we keep HSI 8 MHz instead of blocking boot.
 * HAL_RCC_ClockConfig() re-arms SysTick for the new core clock. */
static void pm_clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType  = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState        = RCC_HSE_ON;
    osc.HSEPredivValue  = RCC_HSE_PREDIV_DIV1;
    osc.HSIState        = RCC_HSI_ON;
    osc.PLL.PLLState    = RCC_PLL_ON;
    osc.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL      = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return;             /* HSE/PLL failed: stay on HSI 8 MHz */
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

void pm_platform_init(void)
{
    HAL_Init();          /* SysTick 1 ms on the reset clock (HSI 8 MHz) */
    pm_clock_init();     /* raises the core to 72 MHz when HSE is present */
}

uint32_t pm_platform_core_clock(void)
{
    return SystemCoreClock;
}

/* One ISR feeds both time bases. HAL uses its own tick for HAL_Delay and
 * timeouts; the library uses the OSAL counter. They stay coherent because
 * both are incremented here, in the same interrupt.
 *
 * The arch-allow marker below is deliberate: check_layering.py rule 17 lets a
 * port-layer ISR call *_tick_inc, but its reverse-dependency rule has no such
 * exception, so it would flag this sanctioned call as bsp -> osal. */
void SysTick_Handler(void)
{
    HAL_IncTick();
    pm_osal_tick_inc();   /* arch-allow: bsp 允许 tick_inc（检查器第 17 项） */
}
