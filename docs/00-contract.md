# 外设模型接口契约

本文件是这套代码的**规范**，不是说明性文档。上层代码只依赖这里写明的行为；实现可以换（STM32F1、其它芯片、主机 mock），行为不许变。

版本：v0.1（第一阶段，GPIO + UART 已落地）

> **快照说明（2026-11-20）**：本文件固定在模型仓 `aa342e5`（v0.1），只覆盖 GPIO/UART。
> 之后接入的 `pm_pwm`/`pm_motor`（v0.7.0，`69a578d`）、`pm_relay`/`pm_track`、
> `pm_encoder` 与 `pm_timer`（v0.8.0，`0e56b1c`）以模型仓契约（含 §20 定时器资源仲裁）
> 为准，本仓差异与回同步方式见 `PATCHES.md`；板端使用口径见 `docs/PROTOCOL.md` §4.9。
> 因此本文件没有 §20 不是缺页，而是版本快照。

---

## 1. 适用范围与非目标

**适用**：单核 Cortex-M 裸机、无 RTOS、无动态内存、单生产者/单消费者。

**非目标（本版本明确不做）**：

- 多核、SMP、RTOS 任务间同步；
- 一个外设实例被多个线程或任务同时写；
- 运行期创建/销毁对象、动态缓冲区、`malloc`；
- 热插拔、电源管理、低功耗模式的自动切换；
- 跨芯片的 ABI 兼容（对象结构体布局属于实现细节，跨版本可能变）。

明确不做，是为了让契约可被静态检查、可在主机上单测。

---

## 2. 分层与依赖方向

```
APP           应用逻辑、调度、业务状态机
 │  只能调 pm_* 公共 API
 ▼
pm_core       外设模型：状态机、环形缓冲、事件队列、参数校验
 │  只能调 pm_port_get() 拿到的 ops 表 + pm_osal
 ▼
pm_port       芯片移植层：把 ops 映射到 HAL/寄存器（或主机 mock）
 │
 ▼
HAL / 寄存器   厂商代码，只允许出现在 port 目录内
```

硬性规则：

1. **向下依赖，禁止反向 include**。`pm_core` 不得 include 任何厂商头或 port 内部头。
2. **厂商类型不出 port**。`GPIO_TypeDef`、`UART_HandleTypeDef`、`USART_TypeDef` 不得出现在 `include/pm/` 下的任何头文件里。
3. **上层不 cast 句柄**。`pm_gpio_pin_t` 只有一个 `id` 字段，其含义由板级表定义，上层不得假设 id 与物理引脚号的对应关系。
4. **ISR 反向通知走内部钩子**。port 的 ISR 只能调 `pm_uart_hw_*`，不得直接改模型字段、不得调用用户回调。
5. **只有一个 `pm_port_get()`**。同一份固件里由链接决定后端，不同时存在两个端口实现。

依赖矩阵：

| 层 | 允许依赖 | 禁止依赖 |
| --- | --- | --- |
| APP | `include/pm/*` | port、HAL、寄存器 |
| pm_core | `include/pm/*`、`pm_osal.h` | port 内部头、HAL |
| pm_port | `pm_port_get` 的义务、HAL、CMSIS | APP |
| board | port 内部头、芯片头 | APP、pm_core |

---

## 3. 错误码模型

全库只有一个错误枚举 `pm_err_t`（`pm_err.h`）：

| 值 | 含义 | 调用方应做什么 |
| --- | --- | --- |
| `PM_OK` (0) | 成功 | — |
| `PM_ERR_PARAM` | 参数值非法（波特率、数据位、大小非 2 的幂等） | 改配置，不要重试 |
| `PM_ERR_NULL` | 必填指针为空 | 修 bug |
| `PM_ERR_RANGE` | id / 索引超出板级表范围 | 改调用，不要重试 |
| `PM_ERR_TIMEOUT` | 有界等待超时，可能已有部分进度（看 `*written`） | 由上层决定是否重试 |
| `PM_ERR_BUSY` | 资源已被占用（重复 init、上一块未发完） | 等待，不要忙轮询 |
| `PM_ERR_IO` | 硬件报错 | 查 `pm_uart_stats_t` |
| `PM_ERR_STATE` | 生命周期不对（未 init 就读写、端口未注册） | 修调用顺序 |
| `PM_ERR_NOMEM` | 固定池耗尽 | 调大静态缓冲 |
| `PM_ERR_AGAIN` | 非阻塞调用当前无法推进 | 下一轮主循环再试 |
| `PM_ERR_OVERFLOW` | 数据被丢弃（队列满） | 看 dropped 计数 |

规则：

- 错误码是**负值**，`if (rc < 0)` 一律等价于失败；`PM_OK == 0`。
- **不允许新增未登记的返回码**。模块内部状态用私有枚举，出口统一翻译成 `pm_err_t`。
- 失败时必须给出可观测证据：要么返回 `PM_ERR_*`，要么计数加一。**不允许静默失败**。
- 参数校验发生在**进入硬件之前**。非法参数不得触碰寄存器。

---

## 4. 对象、句柄与静态分配

模型对象全部由**调用方静态分配**，库内部不使用 `malloc/free`（链接产物里 `_sbrk`、`malloc` 均不存在，见 README 的验证记录）。

```c
static pm_uart_t s_uart;          /* 或 pm_uart_t u = {0}; 局部静态 */
static uint8_t   s_rx[256];       /* 2 的幂，>=32 */
static uint8_t   s_tx[512];       /* 2 的幂，>=64 */
```

约定：

1. `pm_uart_t` 必须**零初始化**（静态存储或 `= {0}`）。`pm_uart_init` 用 `magic` 字段判断对象是否已属于某个实例。
2. 缓冲区指针只被保存，不被复制。调用方必须保证其生命周期覆盖整个对象生命周期。
3. `pm_gpio_pin_t` 是无状态句柄（只有一个 id），不需要初始化函数，直接传值。
4. 一个 `hw_id` 在同一时刻只能被一个对象占用；重复 `init` 返回 `PM_ERR_BUSY`，且**不破坏已初始化对象**。
5. `pm_uart_deinit` 幂等：对未初始化对象调用返回 `PM_OK`。

结构体字段是公开的（C 没有 private），但**上层不得直接读写任何字段**；布局属于实现细节。

---

## 5. 初始化顺序与依赖

固定顺序，前一步失败则不得进入下一步：

```
1. 启动文件 / SystemInit        时钟树（72 MHz 或 HSI 回退）
2. pm_platform_init()           HAL_Init + SysTick 1 ms + OSAL 计数归零
3. board 级引脚/时钟准备        由 port 的 open/configure 按需完成，无需单独调用
4. pm_gpio_configure()          每个要用的逻辑引脚
5. pm_uart_init()               每个要用的串口
6. 主循环：read / write / poll
```

依赖关系：

- `pm_osal_now_ms()` 依赖 `pm_platform_init()`；在它之前调用返回未定义时间（不崩溃，但无意义）。
- `pm_uart_write(timeout > 0)` 依赖 OSAL 时基；没有时基时只能使用 `PM_UART_TIMEOUT_NOW`。
- `pm_uart_init` 内部顺序固定为：参数校验 → 环形缓冲 → 事件队列 → port `open` → 置 READY → `enable_rx`。
  任意一步失败，已获取的资源必须回滚（`open` 失败时对象保持未初始化）。
- 中断使能发生在对象进入 READY **之后**，因此 ISR 看到的对象一定是完整的。

---

## 6. ISR 到主循环：通知模型与所有权

这是本套代码最关键的约定。

**ISR 内允许做的事，仅限三件**：

1. 从数据寄存器取出一个字节（或一段，DMA 场景）；
2. 把字节写入 RX 环（`pm_rb_write`）或从 TX 环推进（`pm_rb_consume`）；
3. 向事件队列投递一个事件（`pm_evtq_push`）。

**ISR 内禁止**：调用用户回调、调用 `pm_osal_delay_ms`、打印、做协议解析、分配内存、长时间循环、调用任何可能阻塞的 HAL 函数。

**所有权**：

| 数据 | 生产者 | 消费者 | 同步方式 |
| --- | --- | --- | --- |
| RX 环 `head` | RX ISR | 主循环 `pm_uart_read` | 单生产者/单消费者，无需临界区 |
| RX 环 `tail` | 主循环 | RX ISR | 同上 |
| TX 环 `head` | 主循环 `pm_uart_write` | TX 完成 ISR | 同上 |
| TX 环 `tail` | TX 完成 ISR | 主循环 | 同上 |
| 事件队列 | 任意 ISR | 主循环 `pm_uart_poll` | 单生产者/单消费者 |
| `tx_inflight` | 主循环 + TX ISR | 两侧 | **需要临界区**（`pm_osal_critical_enter`） |
| `stats` 计数 | ISR 与主循环各自独立字段 | 主循环读取 | 32 位对齐读写在 M3 上原子 |

**事件语义**（`pm_event.h`）：

- `PM_EVT_UART_RX_DATA`：电平型，RX 环非空。连续收到多个字节只保留一个事件。
- `PM_EVT_UART_TX_DONE`：电平型，TX 环变空。
- `PM_EVT_UART_ERROR`：边沿型，硬件错误发生一次投递一次（同一 src 会去重，计数看 `stats.hw_errors`）。
- 队列满时**丢弃最新事件并累加 `dropped`**。静默丢事件是不允许的。
- 队列容量为 `PM_EVTQ_DEPTH - 1`（留一个槽区分空/满）。

---

## 7. 回调穿层规定

- 用户回调只有一种：`pm_uart_evt_cb_t`，由 `pm_uart_poll` 在主循环上下文调用。
- **回调绝不在 ISR 中执行**。port 层不得保存或调用用户回调。
- 回调参数中的 `pm_uart_t *` 是借用，回调不得 `deinit` 或销毁对象。
- 回调内可以调 `pm_uart_read/write`（同一对象重入只发生在主循环，安全）。
- 不允许"注册回调后就不再 poll"的用法：**事件必须被 poll 消费**，否则队列会饱和并开始计数丢弃。

---

## 8. 超时与重试

- 所有可能阻塞的公共 API 都有 `timeout_ms` 参数：
  - `0`（`PM_UART_TIMEOUT_NOW`）：不等待，能推多少推多少，未完成返回 `PM_ERR_AGAIN`；
  - `> 0`：有界等待，超时返回 `PM_ERR_TIMEOUT`，并通过 `*written` 报告已接受字节数。
- 超时比较一律用无符号差值 `now - start`，禁止 `now > start`（49.7 天回绕）。
- 库内部**不实现自动重试**。重试策略由 APP 决定：重试次数、退避间隔、失败上报。
- 库内部不允许 `while (!flag)` 形式的无界循环。所有等待都必须带时间上界。
- `PM_TIMEOUT_FOREVER` 之类的无限等待在本版本**未提供**；启动阶段如需等待，请给出明确的大超时值并记录原因。

---

## 9. 并发与临界区

- 模型假设**单核、中断优先级固定、无 RTOS**。
- 临界区只用于保护"两侧共同读写的状态"，目前只有 `tx_inflight` 和 flush 操作。
- 环形缓冲的 SPSC 结构**不需要**临界区；不要为了"保险"把整个写路径包进临界区，那会拉长中断关闭时间。
- 临界区必须成对、可嵌套，`enter` 返回的状态必须原样传给 `exit`。
- 临界区内禁止：调用 HAL 阻塞函数、延时、打印、调用用户代码。
- 中断优先级：UART 使用优先级 1（数值越大优先级越低），SysTick 保持 HAL 默认。若上层引入更高优先级中断，需重新评估临界区覆盖范围。

---

## 10. 可测试性

硬性要求：**核心逻辑不依赖硬件即可编译并运行**。

- 判断标准：`src/core/*.c` 的 include 列表里只能出现 `include/pm/*` 和 C 标准库。
- 主机侧提供两样东西即可跑测试：
  - `tests/host/pm_port_mock.c`：实现 `pm_port_get()` 的内存版；
  - `tests/host/pm_osal_host.c`：时间由测试推进的 OSAL。
- 当前覆盖：环形缓冲（含回绕、丢弃、consume）、事件队列（去重、溢出）、GPIO 参数校验、UART 生命周期、TX 流控、RX 投递、超时路径。共 **175** 项断言，全部通过（取值来源：`evidence/timebase-hosttest.log`，2026-11-20，`175 checks, 0 failure(s)`；新增测试时同步更新此处）。
- 新增外设时，**先写 core 逻辑和 mock 测试，再接真实后端**。做不到就说明该逻辑还不该进 core。

---

## 11. 新增一个外设的检查清单

第二阶段铺开 SPI/I2C/TIM/ADC/DMA/RTC/Flash/WDT 时，每个外设必须逐条满足：

1. 公共头放在 `include/pm/pm_<periph>.h`，只含标准类型；
2. 错误码复用 `pm_err_t`，不新增；
3. 对象静态分配，必须有 `magic` 与 `deinit`（无状态外设除外，如 GPIO）；
4. 阻塞 API 必须带 `timeout_ms`，非阻塞必须返回 `AGAIN`；
5. ISR 只做搬运 + 投递事件，事件必须可 poll、可计数丢弃；
6. 参数校验在触硬件之前；
7. port 侧只加 ops 函数与 ISR 钩子，不改 core 行为；
8. 主机 mock 测试通过；
9. ARM 侧编译通过，并在 README 的资源表里更新 Flash/RAM 占用。

---

## 12. 版本与兼容

- 本仓库当前为 v0.1，接口可能在不通知的情况下调整。
- 结构体布局不承诺 ABI 稳定；跨版本升级请重新编译全部源码。
- 契约条款的修改必须同时更新本文件，不允许"代码改了文档没改"。
