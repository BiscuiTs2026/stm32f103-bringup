#include "pm_platform.h"
#include "pm_osal.h"
#include "pm_pwm_tim2_stm32f1.h"
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

static char *dbg_s(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

/* 0x + 8 hex digits, upper case: a register dump must be readable digit by
 * digit, so nothing is shortened. */
static char *dbg_hex(char *p, uint32_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;

    p = dbg_s(p, "0x");
    for (i = 28; i >= 0; i -= 4) {
        *p++ = digits[(v >> (unsigned)i) & 0xFu];
    }
    return p;
}

static char *dbg_u32(char *p, uint32_t v)
{
    char  b[11];
    char *q = &b[sizeof(b) - 1];

    *q = '\0';
    do {
        *--q = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    return dbg_s(p, q);
}

/* "MOTOR dbg" text (temporary, 2026-11-20). The model layer reported a duty
 * but the shaft did not move, so dump what the peripherals actually hold.
 * CNT is sampled three times around bounded spins: at 20 kHz one ARR period
 * is 50 us, so a single short gap can land on the same value even while the
 * counter runs. The loops are finite on purpose, a stalled counter must not
 * hang the main loop. Fields, in reply order:
 *   apb1enr  RCC->APB1ENR, bit 0 = TIM2EN
 *   cr1      TIM2->CR1, CEN says whether the counter runs
 *   ccer     TIM2->CCER, CC1E/CC2E say whether the outputs are on
 *   ccmr1    TIM2->CCMR1, OC1M/OC2M and the preload bits
 *   arr/psc  TIM2->ARR / TIM2->PSC, period and prescaler
 *   cnt=a+b+c three CNT samples
 *   crl      GPIOA->CRL, PA0/PA1 nibbles should read 0xB (AF push-pull) */
const char *pm_platform_motor_dbg(void)
{
    static char s_dbg[192];
    char    *p = s_dbg;
    uint32_t i;

    p = dbg_s(p, "MOTOR dbg apb1enr=");
    p = dbg_hex(p, RCC->APB1ENR);
    p = dbg_s(p, " cr1=");
    p = dbg_hex(p, TIM2->CR1);
    p = dbg_s(p, " ccer=");
    p = dbg_hex(p, TIM2->CCER);
    p = dbg_s(p, " ccmr1=");
    p = dbg_hex(p, TIM2->CCMR1);
    p = dbg_s(p, " arr=");
    p = dbg_u32(p, TIM2->ARR);
    p = dbg_s(p, " psc=");
    p = dbg_u32(p, TIM2->PSC);
    p = dbg_s(p, " cnt=");
    p = dbg_u32(p, TIM2->CNT);
    for (i = 0u; i < 6000u; i++) {
        __NOP();
    }
    p = dbg_s(p, "+");
    p = dbg_u32(p, TIM2->CNT);
    for (i = 0u; i < 6000u; i++) {
        __NOP();
    }
    p = dbg_s(p, "+");
    p = dbg_u32(p, TIM2->CNT);
    p = dbg_s(p, " crl=");
    p = dbg_hex(p, GPIOA->CRL);
    p = dbg_s(p, "\r\n");
    *p = '\0';
    return s_dbg;
}

/* Thin board seam for the debug-only fault injector; the register work lives
 * in src/port/stm32f1/pm_pwm_tim2_stm32f1.c next to the production open path. */
int pm_platform_motor_force(uint8_t mode, int *rc, uint32_t *bits,
                            uint8_t *before, uint8_t *after)
{
    return pm_f1_tim2_force_fault(mode, rc, bits, before, after);
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
