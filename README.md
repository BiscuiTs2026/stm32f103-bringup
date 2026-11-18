# STM32F1 外设通用模型

一套按"接口契约优先"方式组织的 STM32F1 外设代码：上层只依赖 `include/pm/` 里的公共 API，芯片相关的一切都收敛在 port 层。目标是换板子只改一张引脚表，换芯片只换一个 port 目录，核心逻辑永远能在主机上跑单测。

当前版本 **v0.1**，已实现 GPIO 与 UART；其余外设按同一套契约逐步铺开。

---

## 验证状态

| 项目 | 状态 | 证据 |
| --- | --- | --- |
| ARM 交叉编译 | 通过 | `arm-none-eabi-gcc 14.3.1`，无项目代码警告 |
| 链接 | 通过 | Flash 6440 B / 64 KB（9.83%），RAM 2224 B / 20 KB（10.86%） |
| 产物 | 已生成 | `pm_f103_demo.elf` / `.bin`（6440 B）/ `.hex` / `.map` |
| 主机单测 | 通过 | 96 项断言，0 失败（MSVC 19.51，C11） |
| 动态分配 | 不存在 | `nm` 中无 `malloc` / `free` / `_sbrk` |
| 向量表 | 正确 | 首字 `0x20005000`（20 KB RAM 顶），Reset_Handler `0x08000228` |
| **上板运行** | **未验证** | 本轮未接板，HEX/BIN 未烧录 |

未做上板验证的部分不写成"可用"。

---

## 目录

```
docs/00-contract.md     接口契约（规范，先读这个）
docs/01-layering.md     目录、依赖、时序、移植
include/pm/             公共头，无厂商类型
src/core/               与硬件无关的模型（可在主机编译）
src/osal/               Cortex-M3 的时基与临界区
src/port/stm32f1/       HAL 后端、UART ISR、pm_port_get()
platform/stm32f103/     板级引脚表、启动文件、链接脚本、HAL 配置
tests/host/             内存 mock 后端 + 主机单测
third_party/            CMSIS 与 ST HAL 子集（BSD-3-Clause，原样分发）
```

---

## 构建

工具链：`arm-none-eabi-gcc` 14.x、CMake ≥ 3.20、Ninja。三者都在本机 STM32Cube bundle 里，未加入全局 PATH 时可用绝对路径。

```powershell
cmake -S . -B build-arm -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc-toolchain.cmake
cmake --build build-arm
```

产物在 `build-arm/`：`pm_f103_demo.elf`、`.bin`、`.hex`、`.map`，构建结束会打印 Flash/RAM 占用。

换工具链路径：在配置时加 `-DTOOLCHAIN_ROOT=<gcc bin 目录>`。

## 主机单测

不需要任何硬件，也不需要 ARM 工具链：

```powershell
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
./build-host/Debug/pm_host_tests.exe
```

输出形如 `96 checks, 0 failure(s)`，退出码 0 表示全过。

---

## 用法

### GPIO

```c
#include "pm_gpio.h"
#include "board_f103c8.h"

pm_gpio_pin_t led = { .id = BOARD_GPIO_LED_BLUE };
pm_gpio_cfg_t cfg = {
    .dir = PM_GPIO_DIR_OUT,
    .pull = PM_GPIO_PULL_NONE,
    .speed = PM_GPIO_SPEED_LOW,
    .initial_level = PM_GPIO_LEVEL_LOW,
};

(void)pm_gpio_configure(led, &cfg);
(void)pm_gpio_toggle(led);
```

### UART（异步、非阻塞）

```c
static pm_uart_t s_uart;
static uint8_t   s_rx[256];
static uint8_t   s_tx[512];

static const pm_uart_cfg_t cfg = {
    .hw_id = BOARD_UART_DEBUG,      /* USART2, PA2/PA3 */
    .baudrate = 115200,
    .parity = PM_UART_PARITY_NONE,
    .stop_bits = PM_UART_STOP_1,
    .data_bits = 8,
    .rx_storage = s_rx, .rx_size = sizeof(s_rx),
    .tx_storage = s_tx, .tx_size = sizeof(s_tx),
};

(void)pm_uart_init(&s_uart, &cfg);

/* 主循环里 */
size_t got;
if (pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_OK) {
    (void)pm_uart_write(&s_uart, buf, got, NULL, PM_UART_TIMEOUT_NOW);
}
pm_evt_t evt;
while (pm_uart_poll(&s_uart, &evt, NULL, NULL)) {
    /* 处理 RX_DATA / TX_DONE / ERROR */
}
```

关键行为：

- `pm_uart_write` 只入队，不忙等发送完成；
- 接收字节全部由 ISR 搬进环形缓冲，主循环读取；
- 用户回调只在 `pm_uart_poll` 里执行，永远不在中断上下文；
- 缓冲区满时丢数据并计数，可通过 `pm_uart_get_stats` 观测。

完整规则见 [docs/00-contract.md](docs/00-contract.md)。

---

## 与四层架构的关系

这套代码对应四层架构里 **BSP 及其接口** 的部分：

```
APP → components → OSAL → BSP(本仓库的 pm_* 契约) → 芯片后端 → HAL
```

- `pm_osal.h` 是 OSAL 的对外面，裸机实现放在 `src/osal/`；
- `pm_port.h` 是 BSP 的接缝，芯片实现放在 `src/port/`；
- APP 与 components 只 include `include/pm/`。

---

## 已知限制

- RTC、Flash、看门狗、SPI、I2C、TIM/PWM、ADC、DMA 尚未实现；
- UART 目前是逐字节中断，尚未提供 DMA + 空闲中断的接收路径；
- 尚未接入 CMake presets，尚未提供 CI；
- 对象结构体布局不承诺 ABI 稳定。

## 第三方代码

`third_party/` 内为 ARM 的 CMSIS（Apache-2.0）与 ST 的 HAL 驱动子集（BSD-3-Clause），原样分发，版权归各自所有者。本仓库自带链接脚本与启动配置，未复制 ST 的链接脚本模板（其条款禁止再分发）。
