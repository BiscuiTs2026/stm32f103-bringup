/*
 * pm_port_stm32f1.c - STM32F1 backend built on the ST HAL.
 *
 * This file and the board file are the only places where vendor types and
 * HAL calls exist. Everything above pm_port.h is vendor-free.
 *
 * Interrupt mapping:
 *   USARTx_IRQHandler -> HAL_UART_IRQHandler -> HAL_UART_*Callback
 *   RX complete       -> pm_uart_hw_rx_byte()   (ISR, no user code)
 *   TX complete       -> pm_uart_hw_tx_complete() (ISR, may start next chunk)
 *   error             -> pm_uart_hw_error()
 */
#include "pm_port_stm32f1.h"
#include "pm_osal.h"
#include "pm_uart.h"
#include "board_f103c8.h"

#include "stm32f1xx_hal.h"

/* ------------------------------------------------------------------ GPIO */

static void gpio_clk_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
}

static uint32_t gpio_speed_to_hal(pm_gpio_speed_t speed)
{
    switch (speed) {
    case PM_GPIO_SPEED_LOW:    return GPIO_SPEED_FREQ_LOW;
    case PM_GPIO_SPEED_MEDIUM: return GPIO_SPEED_FREQ_MEDIUM;
    case PM_GPIO_SPEED_HIGH:   return GPIO_SPEED_FREQ_HIGH;
    default:                   return GPIO_SPEED_FREQ_LOW;
    }
}

static uint32_t gpio_pull_to_hal(pm_gpio_pull_t pull)
{
    switch (pull) {
    case PM_GPIO_PULL_UP:   return GPIO_PULLUP;
    case PM_GPIO_PULL_DOWN: return GPIO_PULLDOWN;
    case PM_GPIO_PULL_NONE:
    default:                return GPIO_NOPULL;
    }
}

static pm_err_t f1_gpio_configure(void *ctx, pm_gpio_pin_t pin, const pm_gpio_cfg_t *cfg)
{
    const pm_f1_gpio_ctx_t *c = (const pm_f1_gpio_ctx_t *)ctx;
    const pm_f1_gpio_map_t *m;
    GPIO_InitTypeDef init;

    if ((c == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if (pin.id >= c->count) {
        return PM_ERR_RANGE;
    }

    m = &c->map[pin.id];
    if ((m->port == NULL) || (m->pin == 0u)) {
        return PM_ERR_PARAM;
    }

    gpio_clk_enable(m->port);

    init.Pin   = m->pin;
    init.Speed = gpio_speed_to_hal(cfg->speed);
    init.Pull  = gpio_pull_to_hal(cfg->pull);
    if (cfg->dir == PM_GPIO_DIR_OUT) {
        init.Mode = GPIO_MODE_OUTPUT_PP;
    } else {
        init.Mode = (cfg->pull == PM_GPIO_PULL_NONE) ? GPIO_MODE_INPUT
                                                     : GPIO_MODE_INPUT;
    }

    HAL_GPIO_Init(m->port, &init);

    if (cfg->dir == PM_GPIO_DIR_OUT) {
        HAL_GPIO_WritePin(m->port, m->pin,
                          (cfg->initial_level == PM_GPIO_LEVEL_HIGH) ? GPIO_PIN_SET
                                                                     : GPIO_PIN_RESET);
    }
    return PM_OK;
}

static pm_err_t f1_gpio_write(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t level)
{
    const pm_f1_gpio_ctx_t *c = (const pm_f1_gpio_ctx_t *)ctx;
    const pm_f1_gpio_map_t *m;

    if (c == NULL) {
        return PM_ERR_NULL;
    }
    if (pin.id >= c->count) {
        return PM_ERR_RANGE;
    }
    m = &c->map[pin.id];
    HAL_GPIO_WritePin(m->port, m->pin,
                      (level == PM_GPIO_LEVEL_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return PM_OK;
}

static pm_err_t f1_gpio_read(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t *level)
{
    const pm_f1_gpio_ctx_t *c = (const pm_f1_gpio_ctx_t *)ctx;
    const pm_f1_gpio_map_t *m;

    if ((c == NULL) || (level == NULL)) {
        return PM_ERR_NULL;
    }
    if (pin.id >= c->count) {
        return PM_ERR_RANGE;
    }
    m = &c->map[pin.id];
    *level = (HAL_GPIO_ReadPin(m->port, m->pin) == GPIO_PIN_SET) ? PM_GPIO_LEVEL_HIGH
                                                                 : PM_GPIO_LEVEL_LOW;
    return PM_OK;
}

static const pm_gpio_ops_t s_gpio_ops = {
    .configure = f1_gpio_configure,
    .write     = f1_gpio_write,
    .read      = f1_gpio_read,
};

/* ------------------------------------------------------------------ UART */

#define PM_F1_UART_COUNT 3u

typedef struct {
    USART_TypeDef       *inst;
    IRQn_Type            irqn;
    UART_HandleTypeDef   huart;
    pm_uart_t           *owner;
    uint8_t              rx_byte;
    uint8_t              opened;
} pm_f1_uart_t;

static pm_f1_uart_t s_uart[PM_F1_UART_COUNT] = {
    { .inst = USART1, .irqn = USART1_IRQn },
    { .inst = USART2, .irqn = USART2_IRQn },
    { .inst = USART3, .irqn = USART3_IRQn },
};

static pm_f1_uart_t *uart_from_handle(UART_HandleTypeDef *huart)
{
    uint8_t i;

    for (i = 0u; i < PM_F1_UART_COUNT; i++) {
        if (&s_uart[i].huart == huart) {
            return &s_uart[i];
        }
    }
    return NULL;
}

static void uart_gpio_init(uint8_t idx)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef    *port;
    uint16_t         tx_pin;
    uint16_t         rx_pin;

    if (idx == 0u) {                          /* USART1: PA9 TX, PA10 RX */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        port   = GPIOA;
        tx_pin = GPIO_PIN_9;
        rx_pin = GPIO_PIN_10;
    } else if (idx == 1u) {                   /* USART2: PA2 TX, PA3 RX */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        port   = GPIOA;
        tx_pin = GPIO_PIN_2;
        rx_pin = GPIO_PIN_3;
    } else {                                  /* USART3: PB10 TX, PB11 RX */
        __HAL_RCC_GPIOB_CLK_ENABLE();
        port   = GPIOB;
        tx_pin = GPIO_PIN_10;
        rx_pin = GPIO_PIN_11;
    }

    /* TX: alternate-function push-pull. */
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Pull  = GPIO_NOPULL;
    init.Pin   = tx_pin;
    init.Mode  = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(port, &init);

    /* RX: plain input with pull-up. On STM32F1 an AF_PP RX pin keeps the
     * output driver active, which blocks the peer's TX line (RXNE never
     * gets set). Confirmed on hardware 2026-11-18. */
    init.Pull  = GPIO_PULLUP;
    init.Pin   = rx_pin;
    init.Mode  = GPIO_MODE_INPUT;
    HAL_GPIO_Init(port, &init);
}

static void uart_clk_enable(uint8_t idx)
{
    if (idx == 0u) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if (idx == 1u) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}

static uint32_t uart_parity_to_hal(pm_uart_parity_t parity)
{
    switch (parity) {
    case PM_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case PM_UART_PARITY_ODD:  return UART_PARITY_ODD;
    case PM_UART_PARITY_NONE:
    default:                  return UART_PARITY_NONE;
    }
}

static pm_err_t f1_uart_open(void *ctx, pm_uart_t *u, const pm_uart_cfg_t *cfg)
{
    pm_f1_uart_t *h;
    uint8_t idx;

    PM_UNUSED(ctx);

    if ((u == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    idx = cfg->hw_id;
    if (idx >= PM_F1_UART_COUNT) {
        return PM_ERR_RANGE;
    }
    h = &s_uart[idx];
    if (h->opened != 0u) {
        return PM_ERR_BUSY;
    }

    uart_clk_enable(idx);
    uart_gpio_init(idx);

    h->huart.Instance          = h->inst;
    h->huart.Init.BaudRate     = cfg->baudrate;
    h->huart.Init.WordLength   = UART_WORDLENGTH_8B;
    h->huart.Init.StopBits     = (cfg->stop_bits == PM_UART_STOP_2) ? UART_STOPBITS_2
                                                                    : UART_STOPBITS_1;
    h->huart.Init.Parity       = uart_parity_to_hal(cfg->parity);
    h->huart.Init.Mode         = UART_MODE_TX_RX;
    h->huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    h->huart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&h->huart) != HAL_OK) {
        return PM_ERR_IO;
    }

    HAL_NVIC_SetPriority(h->irqn, 1u, 0u);
    HAL_NVIC_EnableIRQ(h->irqn);

    h->owner  = u;
    h->opened = 1u;
    return PM_OK;
}

static void f1_uart_close(void *ctx, pm_uart_t *u)
{
    pm_f1_uart_t *h;
    uint8_t idx;

    PM_UNUSED(ctx);

    if (u == NULL) {
        return;
    }
    idx = u->hw_id;
    if (idx >= PM_F1_UART_COUNT) {
        return;
    }
    h = &s_uart[idx];

    HAL_NVIC_DisableIRQ(h->irqn);
    (void)HAL_UART_DeInit(&h->huart);
    h->owner  = NULL;
    h->opened = 0u;
}

static pm_err_t f1_uart_start_tx(void *ctx, pm_uart_t *u, const uint8_t *data, uint16_t len)
{
    pm_f1_uart_t *h;

    PM_UNUSED(ctx);

    if ((u == NULL) || (data == NULL) || (len == 0u)) {
        return PM_ERR_PARAM;
    }
    h = &s_uart[u->hw_id];
    return (HAL_UART_Transmit_IT(&h->huart, (uint8_t *)(uintptr_t)data, len) == HAL_OK)
               ? PM_OK
               : PM_ERR_BUSY;
}

static void f1_uart_enable_rx(void *ctx, pm_uart_t *u)
{
    pm_f1_uart_t *h;

    PM_UNUSED(ctx);

    if (u == NULL) {
        return;
    }
    h = &s_uart[u->hw_id];
    (void)HAL_UART_Receive_IT(&h->huart, &h->rx_byte, 1u);
}

static void f1_uart_disable_rx(void *ctx, pm_uart_t *u)
{
    pm_f1_uart_t *h;

    PM_UNUSED(ctx);

    if (u == NULL) {
        return;
    }
    h = &s_uart[u->hw_id];
    __HAL_UART_DISABLE_IT(&h->huart, UART_IT_RXNE);
}

static const pm_uart_ops_t s_uart_ops = {
    .open       = f1_uart_open,
    .close      = f1_uart_close,
    .start_tx   = f1_uart_start_tx,
    .enable_rx  = f1_uart_enable_rx,
    .disable_rx = f1_uart_disable_rx,
};

/* ------------------------------------------------------------- HAL events */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    pm_f1_uart_t *h = uart_from_handle(huart);

    if (h == NULL) {
        return;
    }
    if (h->owner != NULL) {
        pm_uart_hw_rx_byte(h->owner, h->rx_byte);
    }
    (void)HAL_UART_Receive_IT(huart, &h->rx_byte, 1u);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    pm_f1_uart_t *h = uart_from_handle(huart);

    if ((h != NULL) && (h->owner != NULL)) {
        pm_uart_hw_tx_complete(h->owner);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    pm_f1_uart_t *h = uart_from_handle(huart);

    if (h == NULL) {
        return;
    }
    if (h->owner != NULL) {
        pm_uart_hw_error(h->owner, (uint16_t)huart->ErrorCode);
    }
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    (void)HAL_UART_Receive_IT(huart, &h->rx_byte, 1u);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_uart[0].huart);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_uart[1].huart);
}

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_uart[2].huart);
}

/* ---------------------------------------------------------- port registry */

static const pm_port_t s_port = {
    .gpio           = &s_gpio_ops,
    .gpio_ctx       = (void *)&pm_f1_gpio_ctx,
    .gpio_pin_count = (uint16_t)BOARD_GPIO_COUNT,
    .uart           = &s_uart_ops,
    .uart_ctx       = NULL,
    .uart_count     = (uint8_t)PM_F1_UART_COUNT,
};

const pm_port_t *pm_port_get(void)
{
    return &s_port;
}
