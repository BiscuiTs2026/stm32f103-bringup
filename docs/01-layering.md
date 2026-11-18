# 目录、分层与移植

## 1. 目录结构

```
work/
├── docs/                         契约与移植说明
│   ├── 00-contract.md            接口契约（规范）
│   └── 01-layering.md            本文件：目录、依赖、时序、移植
├── include/pm/                   公共头（不含任何厂商类型）
│   ├── pm_common.h               基础类型、编译期工具、内存屏障
│   ├── pm_err.h                  统一错误码
│   ├── pm_osal.h                 时基、延时、临界区
│   ├── pm_event.h                事件类型与事件队列（SPSC）
│   ├── pm_ringbuf.h              环形缓冲（SPSC）
│   ├── pm_port.h                 后端 ops 与注册表（唯一接缝）
│   ├── pm_gpio.h                 GPIO 模型 API
│   └── pm_uart.h                 UART 模型 API
├── src/
│   ├── core/                     与硬件无关的模型实现
│   │   ├── pm_err.c
│   │   ├── pm_event.c
│   │   ├── pm_ringbuf.c
│   │   ├── pm_gpio.c             参数校验 + 转发到 port
│   │   └── pm_uart.c             生命周期、TX 流控、RX 投递、事件
│   ├── osal/
│   │   └── pm_osal_port_cm3.c    Cortex-M3 实现（PRIMASK + SysTick 计数）
│   ├── port/stm32f1/             STM32F1 后端（唯一允许厂商类型的地方）
│   │   ├── pm_port_stm32f1.h     内部头：GPIO 映射表、上下文
│   │   └── pm_port_stm32f1.c     HAL 实现、UART ISR、pm_port_get()
│   ├── port/mock/                预留给第二阶段的 mock 目录
│   └── app/
│       └── main.c                示例：LED 闪烁 + 串口回显 + 心跳
├── platform/stm32f103/           板级与芯片级文件
│   ├── board_f103c8.h/.c         逻辑引脚表、UART 编号
│   ├── pm_platform.h/.c          平台启动与 SysTick
│   ├── stm32f1xx_hal_conf.h      HAL 模块开关
│   ├── stm32f103c8.ld            自行编写的链接脚本（64K/20K）
│   └── startup_stm32f103xb.s     启动文件（来自 CMSIS 设备包）
├── tests/host/                   主机测试
│   ├── test_main.c               96 项断言
│   ├── pm_port_mock.c            内存后端
│   ├── pm_osal_host.c            可控时间
│   └── CMakeLists.txt            独立构建（MSVC/GCC 均可）
├── third_party/
│   ├── cmsis/                    CMSIS Core + STM32F1 设备头
│   └── stm32f1xx_hal/            ST HAL 子集（仅编译用到的模块）
├── cmake/arm-gcc-toolchain.cmake
├── CMakeLists.txt                ARM 固件构建
└── README.md
```

## 2. 每个目录的职责边界

| 目录 | 职责 | 不负责 |
| --- | --- | --- |
| `include/pm/` | 对外承诺的行为与类型 | 任何实现细节 |
| `src/core/` | 状态机、缓冲、事件、参数校验 | 寄存器、HAL、引脚 |
| `src/osal/` | 时间与临界区 | 业务时间语义 |
| `src/port/stm32f1/` | 把 ops 映射到 HAL；ISR 搬运 | 协议解析、业务判断 |
| `platform/` | 板级引脚表、时钟启动、链接脚本、启动文件 | 外设行为 |
| `tests/host/` | 证明 core 可脱离硬件运行 | 替代上板验证 |
| `third_party/` | 原样随仓库分发的依赖 | 修改厂商代码 |

## 3. 依赖关系

```
main.c ──► pm_gpio.h / pm_uart.h / pm_platform.h / board_f103c8.h
              │
              ▼
          pm_core (src/core)
              │  pm_port_get()
              ▼
      pm_port_stm32f1 ──► HAL ──► CMSIS ──► 寄存器
              │
              └──► board_f103c8.c（引脚表）
```

- `src/core` 编译时**看不到** `stm32f1xx.h`（CMake 的 include 路径里没有它）；
- `src/port` 编译时看不到 APP；
- `platform/` 同时被 port 与可执行目标引用，但只提供表与启动代码。

这条链路可以用编译失败来验证：把 `stm32f1xx_hal` 的 include 路径从 `pm_core` 移除后，`pm_core` 仍应编译通过。

## 4. 一次 UART 收发的完整时序

**发送 5 个字节**（`pm_uart_write(&u, "hello", 5, &n, 0)`）：

1. `pm_core`：校验参数与 state；
2. 临界区：`pm_rb_write` 把 5 字节写入 TX 环，`uart_kick_tx_locked` 取一段连续数据调用 `ops->start_tx`，记录 `tx_inflight = 5`；
3. 临界区退出；
4. port：`HAL_UART_Transmit_IT` 启动，返回 `PM_OK`；
5. 硬件发完一字节触发 TXE 中断，HAL 继续取下一字节；
6. 最后一字节发完，HAL 调用 `HAL_UART_TxCpltCallback`；
7. port 回调调用 `pm_uart_hw_tx_complete`；
8. `pm_core`（ISR 上下文）：`tx_inflight` 清零，`pm_rb_consume(5)`，`tx_bytes += 5`；TX 环为空则投递 `PM_EVT_UART_TX_DONE`；
9. 主循环 `pm_uart_poll` 取到事件，回调在主循环执行。

**接收 1 个字节**：

1. RXNE 中断 → `HAL_UART_IRQHandler` → `HAL_UART_RxCpltCallback`；
2. port 调 `pm_uart_hw_rx_byte(u, byte)`；
3. `pm_core`（ISR 上下文）：`pm_rb_write` 写入 RX 环，`rx_bytes++`，投递 `PM_EVT_UART_RX_DATA`（已存在则去重）；
4. port 立即重新 `HAL_UART_Receive_IT` 备好下一字节；
5. 主循环 `pm_uart_read` 取数据、`pm_uart_poll` 取事件。

**环满**：`pm_rb_write` 返回 0，`stats.rx_dropped++`，同时投递 `PM_EVT_UART_ERROR`。数据丢失是可观测的。

## 5. 移植到另一块 STM32F1 板

只需改 `platform/stm32f103/board_f103c8.c` 与 `.h`：

1. 在 `board_*.h` 里定义逻辑引脚枚举与 UART 编号；
2. 在 `board_*.c` 里填写 id → `(GPIOx, GPIO_PIN_n)` 的映射表；
3. 若使用不同的串口引脚，修改 `src/port/stm32f1/pm_port_stm32f1.c` 的 `uart_gpio_init()`；
4. 改链接脚本里的 Flash/RAM 大小（`stm32f103c8.ld`）。

上层 `main.c` 与 `pm_core` 不需要改。

## 6. 移植到另一种芯片

1. 新建 `src/port/<chip>/`，实现 `pm_gpio_ops_t`、`pm_uart_ops_t` 和 `pm_port_get()`；
2. 新建 `src/osal/pm_osal_port_<arch>.c`（用该芯片的关中断原语）；
3. 新建 `platform/<chip>/` 放启动文件与链接脚本；
4. `pm_core` 与 `include/pm` 不动；
5. 主机测试仍然跑 mock 版本，用于回归。

## 7. 编译期校验建议

- `pm_core` 的 include 路径里不放 `third_party/`，从构建层面阻断厂商头泄漏；
- 用 `-Werror=implicit-function-declaration` 防止漏 include；
- 链接后用 `nm` 检查 `malloc/free/_sbrk` 不存在；
- 用 `size`/`--print-memory-usage` 记录每次改动的 Flash/RAM 增量。
