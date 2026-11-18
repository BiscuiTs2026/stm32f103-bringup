# STM32F1 外设通用模型

## 定位

本目录是 **上板验证台架**，不是对外交付的库；远程仓为 `BiscuiTs2026/stm32f103-bringup`（**私有**）。
`pm_*` 源码是 `BiscuiTs2026/stm32-peripheral-model` 某一提交的快照：基线 **`aa342e5`**（v0.1，GPIO + UART）。
复制发生在该提交成型前的工作树中间态——7 个 GPIO/UART 文件与 `aa342e5` 逐字节相同，
`include/pm/pm_port.h` 少 167 行 SPI 声明；凡涉及 `pm_*` 契约的漂移，**以模型仓为准**，按 `PATCHES.md` 回同步。
本仓只保留这块 KEYSKING 学习板的实测改动与上板证据（`evidence/INDEX.md` 为登记表），不对外交付。

---

一套按"接口契约优先"方式组织的 STM32F1 外设代码：上层只依赖 `include/pm/` 里的公共 API，芯片相关的一切都收敛在 port 层。目标是换板子只改一张引脚表，换芯片只换一个 port 目录，核心逻辑永远能在主机上跑单测。

当前处于 **上板验证阶段**：`pm_*` 契约层仍是 GPIO + UART；这块板的实测功能（USART2/USART3 共享命令解析、I2C1 轮询 + AHT20、HC-SR04 非阻塞测距、CH1116 OLED 含 500 ms live 与 1 Hz 自动测距）写在 APP 胶水里，尚未回灌成新契约。

---

## 验证状态

每个上板结论都有对应日志，登记表在 [evidence/INDEX.md](evidence/INDEX.md)，逐条推导与失败记录在 [PATCHES.md](PATCHES.md)（下文 § 号均指该文件）。

| 项目 | 状态 | 证据 / 数字 |
| --- | --- | --- |
| 交叉编译 | 通过 | `arm-none-eabi-gcc 14.3.1`，`-Wall -Wextra -Wshadow`，项目代码无警告 |
| 尺寸 | 通过 | Flash **16480 B / 64 KB（25.15%）**，RAM **3424 B / 20 KB（16.72%）**（§19、§20） |
| 产物 | 已生成 | `pm_f103_demo`（ELF）、`.bin`（16480 B）、`.hex`、`.map` |
| 主机单测 | 通过 | 96 项断言，0 失败（MSVC 19.51，C11） |
| 动态分配 | 不存在 | `nm` 中无 `malloc` / `free` / `_sbrk` |
| 时钟 | 72 MHz | `RCC_CFGR = 0x001D040A`（HSE+PLL×9、APB1÷2）；banner `core clock = 72000000 Hz`（§2） |
| USART2 调试口 | 通过 | 115200 8N1 双向收发，`hb` 每秒一行（§5） |
| USART3 蓝牙口 | 通过 | 9600 8N1，与 USART2 同一解析器；长回包不丢尾（§11、§14），当前 `HELP` 完整回包 **356 B / 9 行**（§19） |
| I2C1 | 通过 | 扫描到 `0x70`（AHT20）与 `0x7A`（OLED），PB6/PB7，100 kHz（§9） |
| AHT20 温湿度 | 通过 | `AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0`（§12） |
| HC-SR04 测距 | 通过 | `DIST cm=<n> us=<n>`，非阻塞，EXTI + TIM2 计宽（§14） |
| CH1116 OLED | 通过 | 文字、`frame` 图案（col=2 下 9 条竖线、四边完整）、`live` 500 ms + 1 Hz 自动测距（§15–§19） |
| 按键 | 通过 | 20 ms 消抖，6 组 pressed/released 无重复无漏（§7） |
| SPI / ADC / PWM | **未验证** | 无实际器件；SPI 只有 HAL 源与默认关闭的 `PM_DEMO_SPI` 开关，本树没有 demo 实现 |

未做上板验证的部分不写成"可用"。

---

## 目录

```
docs/00-contract.md     接口契约（规范，先读这个）
docs/01-layering.md     目录、依赖、时序、移植
docs/PROTOCOL.md        串口命令协议：分帧、响应约定、命令表、判读陷阱
include/pm/             公共头，无厂商类型
src/core/               与硬件无关的模型（可在主机编译）
src/osal/               Cortex-M3 的时基与临界区
src/port/stm32f1/       HAL 后端、UART ISR、pm_port_get()
src/components/         CH1116 驱动（总线访问由回调注入）
src/app/main.c          入口胶水：时钟、按键、命令解析、I2C 轮询、测距、OLED 调度
platform/stm32f103/     板级引脚表、启动文件、链接脚本、HAL 配置
tests/host/             内存 mock 后端 + 主机单测
evidence/               上板抓包日志 + INDEX.md 登记表
tools/check_layering.py 分层静态检查器（拷自 firmware-four-layer-architecture）
.arch-roles             检查器的角色清单，如实描述本仓现有分层
.github/workflows/      CI 门禁：layering
third_party/            CMSIS 与 ST HAL 子集（BSD-3-Clause，原样分发）
```

---

## 构建

工具链：`arm-none-eabi-gcc` 14.3.1、CMake ≥ 3.20、Ninja。三者在 STM32Cube bundle 里，未加入全局 PATH 时用绝对路径（版本号随安装变化）：

```
%LOCALAPPDATA%\stm32cube\bundles\cmake\4.4.0+st.1\bin\cmake.exe
%LOCALAPPDATA%\stm32cube\bundles\ninja\1.13.2+st.1\bin\ninja.exe
%LOCALAPPDATA%\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe
```

```powershell
cmake -S . -B build-arm -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc-toolchain.cmake
cmake --build build-arm
```

换工具链路径：配置时加 `-DTOOLCHAIN_ROOT=<gcc bin 目录>`。产物在 `build-arm/`：`pm_f103_demo`（ELF）、`.bin`（16480 B）、`.hex`、`.map`，构建结束会打印 Flash/RAM 占用（当前 16480 B / 3424 B）。

烧录（ST-Link，带校验、烧完复位）：

```powershell
STM32_Programmer_CLI -c port=SWD mode=UR -w build-arm/pm_f103_demo.hex -v -rst
```

本树没有 CMake presets，`build-arm` 只是示例目录名；任务并行时按 `AGENTS.md` 一任务一构建目录，故历史目录叫 `build-bringup/`、`build-oled-live-dist/` 等。

## 主机单测

不需要任何硬件，也不需要 ARM 工具链：

```powershell
cmake -S tests/host -B build-host
cmake --build build-host --config Debug
./build-host/Debug/pm_host_tests.exe
```

输出形如 `96 checks, 0 failure(s)`，退出码 0 表示全过。

---

## 静态分层检查（CI 门禁）

`tools/check_layering.py` 是**从 `firmware-four-layer-architecture` skill 拷来的**四层检查器（v3），不是本仓原创代码，本仓也不改它：文件头记了来源路径、取用日期和 SHA-256；来源目录不是独立 git 仓库，所以没有提交哈希可引用。升级方式是整文件覆盖、更新文件头的 SHA-256 与日期、重跑下面的命令，确认 ERROR 仍为 0 且 WARN 的变化能逐条解释。

本仓是两层结构（`src/app` + `src/port` + `src/components`，**没有独立的 OSAL 层**），角色清单 [.arch-roles](.arch-roles) 按真实目录写：`third_party/`、`platform/`、`src/port/` = bsp；`src/osal/` = osal（只有 Cortex-M3 时基与临界区）；`src/core/`、`src/components/` = components；`include/pm/` = common（纯类型/常量/原型，任何层可包含）；`src/app/main.c` = glue。

本地复跑与 CI 是同一条命令（见 [.github/workflows/layering.yml](.github/workflows/layering.yml)）：

```powershell
python tools/check_layering.py . --glue-max-lines 1280
```

**ERROR 必须为 0（否则 CI 红）；WARN 只打印、不判失败。**

当前 3 条 WARN 是同一种、也是设计使然：`src/port/stm32f1/pm_port_stm32f1.c` 的 UART 中断里调用 `pm_uart_hw_rx_byte` / `pm_uart_hw_tx_complete` / `pm_uart_hw_error`。中断服务留在端口层，只把字节和事件递给 `src/core/pm_uart.c` 的钩子，正落在检查器第 4 项“bsp 调组件 ISR 钩子”的 WARN 分支上；确认仅中断上下文使用即可，既不升级成 ERROR 也不隐藏。

`--glue-max-lines 1280` 是棘轮：`src/app/main.c` 现在 **1277 行代码**，是“板级功能还没回灌成 `pm_*` 契约”的已知状态，取舍理由在 [PATCHES.md](PATCHES.md) §20。上限只比现状高 3 行，胶水层再长 CI 就会红。

本地跑若出现 `[NOTE] 另有 4 个未分层文件`，那是 `build-*/CMakeFiles/.../CMakeCCompilerId.c`（构建产物），CI 干净检出时不存在，且不含平台调用，不影响 ERROR/WARN 计数。

---

## 命令接口

USART2（115200）与 USART3（9600，蓝牙透传）跑**同一个解析器，两路完全等价**：命令大小写不敏感；行尾 `\r` / `\n` 可带可不带，不带时空闲 **100 ms** 自动提交；**任何命令都必有一条回包**（未知命令回 `ERR unknown=<原文>`），回包是单行 `KEY=value`，以 `\r\n` 结尾。

| 命令 | 作用 |
| --- | --- |
| `HELP` | 列出全部命令（9 行、356 B） |
| `PING` | 回 `PONG` |
| `STAT` | 时钟、运行秒数、按键计数、串口累计字节 |
| `I2C` | 扫描 I2C1，列 8 位写地址（本板 `0x70`、`0x7A`） |
| `KEYS` | 读按键计数并清零 |
| `AHT` / `AHTS` | 读 AHT20 温湿度；`AHTS` 是 repeated START 对照，拿到的是上一次结果 |
| `DIST` | 单次 HC-SR04 测距，回 `cm` 与 `us` |
| `OLED on\|off\|clear\|test\|show\|frame\|col\|live` | 显示开关、清屏、测试页、静态文字、边框图案、列偏移、500 ms live（live 期间自带 1 Hz 测距） |

参数、成功/失败回包原文、是否改变设备状态、蓝牙端的判读陷阱（回包被拆成多个气泡等），见 **[docs/PROTOCOL.md](docs/PROTOCOL.md)**。

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

- `pm_*` 契约只有 GPIO 与 UART；I2C1 时序、HC-SR04 捕获、OLED 调度都在 `src/app/main.c` 里用裸寄存器与胶水实现，尚未抽成 `pm_i2c` 等契约；
- SPI 只有 HAL 源和默认关闭的 `PM_DEMO_SPI` 开关，本树没有 demo 实现；ADC、PWM、RTC、Flash 编程、看门狗、DMA 均未实现；
- UART 是逐字节中断，没有 DMA + 空闲中断的接收路径；TX 环 512 B，超长回包靠"边发边等"（`tx_bytes_all`）；
- live 模式每 500 ms 推一页（130 B ≈ 12 ms @ 100 kHz I2C），整屏 8 页约 98 ms；live 期间 `OLED show` 回 `err=busy live`；
- 无行尾命令有 100 ms 判定窗口，碎片间隔小于它会被当成同一条命令的延续；
- 自动测距结束后 60 ms 内手动 `DIST` 可能回 `err=timeout`，是 HC-SR04 余振，不是故障；
- 没有 CMake presets，没有 CI；对象结构体布局不承诺 ABI 稳定。

## 第三方代码

`third_party/` 内为 ARM 的 CMSIS（Apache-2.0）与 ST 的 HAL 驱动子集（BSD-3-Clause），原样分发，版权归各自所有者。本仓库自带链接脚本与启动配置，未复制 ST 的链接脚本模板（其条款禁止再分发）。
