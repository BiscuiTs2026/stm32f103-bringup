# STM32F1 外设通用模型

## 定位

本目录是 **上板验证台架**，不是对外交付的库；远程仓为 [`BiscuiTs2026/stm32f103-bringup`](https://github.com/BiscuiTs2026/stm32f103-bringup)（**公开**）。
`pm_*` 源码是 `BiscuiTs2026/stm32-peripheral-model` 的快照：GPIO/UART 等早期文件对应基线 **`aa342e5`**（v0.1，GPIO + UART）；
电机链于 2026-11-20 接入模型 **`69a578d`**（v0.7.0）的 `pm_pwm` / `pm_motor` core，逐字并入 6 个文件：
`include/pm/pm_pwm.h`、`include/pm/pm_pwm_math.h`、`include/pm/pm_motor.h`、`src/core/pm_pwm.c`、`src/core/pm_motor.c`、`src/common/pm_pwm_math.c`（按 git blob 双向核对一致）。
编码器链同源接入提交 **`69a578d`**（v0.7.0）的 `pm_encoder` core，逐字并入 `include/pm/pm_encoder.h`、`src/core/pm_encoder.c`，并把 `include/pm/pm_err.h`、`src/core/pm_err.c` 同步到同一提交（补 `PM_ERR_DISABLED` / `PM_ERR_NACK` / `PM_ERR_BUS`），共 4 个文件按 git blob 双向核对一致；板端 port 为自有实现（EXTI9_5 优先级仍为 4），组件层改调模型 API，`ENC` 回包文本不变。
继电器与循迹同样接入 `69a578d` 的 `pm_relay` / `pm_track` core，逐字并入 `include/pm/pm_relay.h`、`src/core/pm_relay.c`、`include/pm/pm_track.h`、`src/core/pm_track.c`（4 个文件 blob 双向一致）；继电器走 `pm_gpio`，PB5 作为 `BOARD_GPIO_RELAY` 加入板级逻辑引脚表，循迹 port 改为读 PB14 原始电平，组件回包文本不变。
定时器仲裁于 2026-11-20 接入模型 **`0e56b1c`**（v0.8.0）的 `pm_timer` claim 表，逐字并入 `include/pm/pm_timer.h`（blob `53c9403…`）、`src/common/pm_timer.c`（blob `43b4043…`）；TIM2（电机）与 TIM4（舵机）走 `claim/release/retime`，板端只读探测保留用于把 `PM_ERR_BUSY` 细分为 `err=timer` / `err=remap`，可见回包不变（决策 0023、INDEX §144）。
板端原有舵机/蜂鸣器 API 已整体迁到 `include/pm/pm_servo.h`（仅改 include guard 与 `pm_pwm_init`→`pm_servo_init`，函数语义不变）。
早期 GPIO/UART 复制发生在 `aa342e5` 成型前的工作树中间态——7 个 GPIO/UART 文件与该提交逐字节相同，
`include/pm/pm_port.h` 少 167 行 SPI 声明；凡涉及 `pm_*` 契约的漂移，**以模型仓为准**，按 `PATCHES.md` 回同步。
本仓只保留这块 KEYSKING 学习板的实测改动与上板证据（`evidence/INDEX.md` 为登记表），不对外交付。

---

一套按"接口契约优先"方式组织的 STM32F1 外设代码：上层只依赖 `include/pm/` 里的公共 API，芯片相关的一切都收敛在 port 层。目标是换板子只改一张引脚表，换芯片只换一个 port 目录，核心逻辑永远能在主机上跑单测。

当前处于 **上板验证阶段**：`pm_*` 契约层覆盖 GPIO、UART、PWM、输入侧、继电器、电机与定时器仲裁（`pm_pwm.h`、`pm_track.h`、`pm_encoder.h`、`pm_relay.h`、`pm_motor.h`、`pm_timer.h`）。原先留在 APP 胶水里的部分已在 S3–S10 拆分中下沉：USART2/USART3 共享解析与空闲帧判定在 `src/components/pm_cmd/`，`HELP`/`PING`/`STAT`/`I2C` 回包在 `src/components/sys_cmds/`，AHT20 在 `src/components/aht20_dev/`，OLED 调度与显示在 `src/components/oled_ui/`、`oled_ch1116/`，测距作业在 `src/components/sonar_dev/`（默认 OFF、`DIST` 回 `disabled`；`PM_ENABLE_SONAR=ON` 时可用，见验证状态表）；`src/app/main.c` 只剩初始化、主循环与钩子绑定。

---

## 验证状态

每个上板结论都有对应日志，登记表在 [evidence/INDEX.md](evidence/INDEX.md)，逐条推导与失败记录在 [PATCHES.md](PATCHES.md)（下文 § 号均指该文件）。

| 项目 | 状态 | 证据 / 数字 |
| --- | --- | --- |
| 交叉编译 | 通过 | `arm-none-eabi-gcc 14.3.1`，`-Wall -Wextra -Wshadow`，项目代码无警告 |
| 尺寸 | 通过 | **最终固件 `-DPM_ENABLE_SONAR=ON`**：Flash **35968 B / 64 KB（54.88%）**，RAM **6048 B / 20 KB（29.53%）**（`build-v080-force-diag`，`evidence/v080-force-diag-build.log`、`v080-force-diag-flash.log`、INDEX §145；较 claim 移植时 35032 / 6040 B 为 **+936 B / +8 B**，来自两条调试用故障注入命令）。**默认 `PM_ENABLE_SONAR=OFF`（HEAD）**：Flash **35132 B / 64 KB（53.61%）**，RAM **6024 B / 20 KB（29.41%）**（本任务新建 `build-doc-align`，`evidence/v080-off-doc-align-build.log`，2026-11-20；同日 23:07 烧录验证后恢复 ON，见 INDEX §147；比 ON 少 836 B / 24 B）。旧 31324 / 5448 是 claim 表与故障诊断之前的数字，已不代表 HEAD |
| 产物 | 已生成 | `pm_f103_demo`（ELF）、`.bin`（ON 35968 B；OFF 35132 B）、`.hex`、`.map` |
| 主机单测 | 通过 | 175 项断言，0 失败（MSVC 19.51，C11；来源：`evidence/timebase-hosttest.log`，2026-11-20） |
| 动态分配 | 不存在 | `nm` 中无 `malloc` / `free` / `_sbrk` |
| 时钟 | 72 MHz | `RCC_CFGR = 0x001D040A`（HSE+PLL×9、APB1÷2）；banner `core clock = 72000000 Hz`（§2） |
| USART2 调试口 | 通过 | 115200 8N1 双向收发，`hb` 每秒一行（§5） |
| USART3 蓝牙口 | 通过 | 9600 8N1，与 USART2 同一解析器；2026-11-18 端到端验过（`bt-verify2.log`：`HELP` 266 B 完整、`AHT` 51 B 与页面一致，§14.1）；此后 `HELP` 增至 **15 行**（ON 实测 660 B、OFF 上板实测 654 B，§135、INDEX §147），蓝牙侧未再做回归——期间改动不涉及 USART3 收发路径，不丢尾机制见 §11、§14 |
| I2C1 | 通过 | 扫描到 `0x70`（AHT20）与 `0x7A`（OLED），PB6/PB7，100 kHz（§9） |
| AHT20 温湿度 | 通过 | `AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0`（§12） |
| HC-SR04 测距 | 可选启用（默认 OFF） | 默认 `PM_ENABLE_SONAR=OFF`：`DIST` 固定回 `DIST err=disabled`。`-DPM_ENABLE_SONAR=ON` 时改用 `DWT->CYCCNT`（72 MHz）计宽、`EXTI15_10` 优先级 5（编码器 `EXTI9_5` 仍为 4），2026-11-20 上板连续 20+ 次 `DIST cm=5.9/6.2/7.1 us=340/358/414`、无 timeout（§133、决策 0020）；读数对应的物理遮挡物未由人现场确认 |
| CH1116 OLED | 通过 | 文字、`frame` 图案（col=2 下 9 条竖线、四边完整）、`live` 500 ms（§15–§19）；live 期间的 1 Hz 自动测距在 `PM_ENABLE_SONAR=ON` 时恢复，默认 OFF 下静默跳过 |
| 按键 | 通过（带限制） | 20 ms 消抖，6 组 pressed/released 无重复无漏（§7）。**限制：主循环每 500 ms 约 98 ms 被 OLED 整页 I2C 占住，短于约 100 ms 的点按可能整段丢失**（M1-e、`evidence/MANUAL-CHECKS.md`、`TECH_DEBT.md` D23） |
| ADC1 | **部分通过** | PA5 电位器静态 2124–2126（30 次极差 2 LSB）；PA4 NTC 升温 raw 2293→1792、降温回升，判定 NTC 在分压低端（§28）。**电位器转动响应未验证：缺十字螺丝刀** |
| PWM 舵机 / 蜂鸣器 | **部分通过** | PB8 五档脉宽 500/1000/1500/2000/2500 µs 与回包一致；`PWM dbg` 实测 `psc=71`、`fpwm=50.0Hz`、`CCR3` 0/180→500/2500（§32）。**舵机"有动作"已由用户目视确认；角度精度、到位抖动/回弹、带负载表现未验证；蜂鸣器听感未采集**（§30.3） |
| 循迹 PB14 | **部分通过** | 盖黑物 2 轮：`TRACK raw=1` → `raw=0` → `raw=1`，同状态内无跳变（1.78 s 采样，§31.3）。**只有一路，单路遮挡与全黑不可区分**。2026-11-20 改走模型 `pm_track` core（port 自有实现读 PB14 原始电平，组件保持 `raw=` 语义）后，`TRACK raw=1` 与 `TRACK err=usage` 与基线逐字一致（`evidence/relay-track-20261120.log`）；本项只证明命令行为不变，遮挡动态**本轮未复测** |
| 编码器 PA8/PA9 + 按键 PB15 | **部分通过** | `ENC` 双向计数 0→89→−32，cw 为正、ccw 为负，`ENC clear` 正常（§31.3）；2026-11-20 手转复测逆 24→−12、顺 −12→24 精确回零，`btn` 1→0→1 跟随按压约 3.5 s（`evidence/manual-m8-encoder.log`）。同日编码器链改走模型 `pm_encoder` core（板端 port 自有实现、EXTI9_5 优先级仍 4，组件改调模型 API，回包文本不变），上电 `ENC` 回包与基线逐字相同（`count=0 dir=idle a=1 b=1 btn=1`，`evidence/manual-m8-encoder-model.log`）；**本轮两次抓包窗口（60 s + 90 s）无人工转轮，改走模型后的动态计数轨迹与 `btn` 跟随未复测，记为未验证**。**符号与物理转向的对应未目视确认；`dir` 停手后保持最后一次方向、不回落 `idle`** |
| 继电器 PB5 | **部分通过** | `RELAY stat/on/off` 三轮 `pb5=` 读回与请求一致（§34.3、`evidence/relay-20261119-a.log`）。**吸合"咔哒"声、触点通断、高电平=吸合的极性未验证**（§34.4）。2026-11-20 改走模型 `pm_relay` core（PB5 加入板级逻辑引脚表，组件调模型 API）后，`RELAY stat=0`、`on=1`、`off=0` 与基线逐字一致（`evidence/relay-track-20261120.log`）；本项只证明命令行为不变，物理通断**仍未验证** |
| 电机 DRV8833（PA0/PA1） | **部分通过** | 2026-11-19：`MOTOR fwd/rev/stop/duty` 的 `ccr1/ccr2` 组合、`duty` 0/100/101 边界与错误分支全部与寄存器读回一致（§35.3、`evidence/motor-20261119-a.log`）；初始化另做 10‰ 时基复核与 TIM2 占用探测，失败回 `err=timer` / `err=remap`。2026-11-20：电机链改走模型 `pm_pwm` / `pm_motor` core（port 新增 TIM2 PWM ops），同一 M4 序列复跑**逐条一致**（`evidence/motor-20261120-a.log`），烧录带回读校验（`evidence/motor-20261120-flash.log`）；舵机链路因头文件迁移复跑五档脉宽与 `PWM dbg`，与既有证据**逐条一致**（`evidence/pwm-20261120-a.log`）。2026-11-20 晚：TIM2/TIM4 接入模型 v0.8.0 `pm_timer` claim 表，`MOTOR claims` 可查两路 owner，同 `hw_id` 二次申请回 `conflict`、蜂鸣器占用时回 `busy`，既有回包逐字不变；`err=timer` / `err=remap` 两条失败文案已用隐藏的 `MOTOR force timer|remap` 现场触发，注入后 claim 表与寄存器恢复、故障前后 MOTOR 命令逐字一致（INDEX §145，命令仅调试用）。**电机是否真转、`fwd/rev` 对应的轴方向、转速随占空比的变化未验证**（§35.4）；`MOTOR dbg` 与 live 60 s 浸泡本轮未复跑，记为**未验证**、不回填为通过 |
| SPI | **未验证** | 无实际器件；只有 HAL 源与默认关闭的 `PM_DEMO_SPI` 开关，本树没有 demo 实现 |

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
src/components/         CH1116 驱动（总线访问由回调注入）、各器件组件（aht20_dev、adc_dev、sonar_dev 等）、sys_cmds（HELP/PING/STAT/I2C）
src/app/                main.c 只做初始化顺序与主循环调度；app_wiring.c 放命令表与命令适配器
platform/stm32f103/     板级引脚表、启动文件、链接脚本、HAL 配置
tests/host/             内存 mock 后端 + 主机单测
evidence/               上板抓包日志 + INDEX.md 登记表
tools/check_layering.py 分层静态检查器（拷自 firmware-four-layer-architecture）
tools/CHECKER.lock      检查器版本/来源仓/来源提交/blob 的身份记录
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
# 二选一：设环境变量，或配置时传 -DTOOLCHAIN_ROOT
$env:ARM_GCC_ROOT = "$env:LOCALAPPDATA\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin"

cmake -S . -B build-arm -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc-toolchain.cmake
cmake --build build-arm
```

等价写法（不设环境变量，直接传参）：

```powershell
cmake -S . -B build-arm -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc-toolchain.cmake `
      -DTOOLCHAIN_ROOT="$env:LOCALAPPDATA\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin"
cmake --build build-arm
```

工具链根目录没有写死默认值：设 `ARM_GCC_ROOT`，或配置时加 `-DTOOLCHAIN_ROOT=<gcc bin 目录>`；两者都没有时 configure 直接报错并提示这两种写法。产物在 `build-arm/`：`pm_f103_demo`（ELF）、`.bin`、`.hex`、`.map`，构建结束会打印 Flash/RAM 占用。**HEAD 两版实测**：最终固件 `-DPM_ENABLE_SONAR=ON` 为 **35968 B / 6048 B（54.88% / 29.53%）**（`build-v080-force-diag`，`evidence/v080-force-diag-build.log`、INDEX §145）；默认 `PM_ENABLE_SONAR=OFF` 为 **35132 B / 6024 B（53.61% / 29.41%）**（本任务新建 `build-doc-align`，`evidence/v080-off-doc-align-build.log`，2026-11-20；同日烧录验证后恢复 ON，见 INDEX §147）。两版都含 v0.8.0 claim 表与故障诊断；ON 比 OFF 多 836 B / 24 B。旧 31324 / 5448 与 32084 / 5472 是接入 claim 表之前的数字，不再代表 HEAD。

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

输出形如 `175 checks, 0 failure(s)`，退出码 0 表示全过（数字取自 `evidence/timebase-hosttest.log`，2026-11-20；新增测试时同步更新此处）。

---

## 静态分层检查（CI 门禁）

`tools/check_layering.py` 是**从 `firmware-four-layer-architecture` skill 拷来的**四层检查器，不是本仓原创代码。当前同步到上游 **`4da1a77`（checker 3.4.0，2026-11-20 升锁）**，用 `git worktree add --detach` 取干净内容，**代码逐字未改**，只按上游规定把文件头 `SOURCE_COMMIT` 一行改成上游提交哈希。身份记录在 [tools/CHECKER.lock](tools/CHECKER.lock)：`checker-version` / `source-repo` / `source-commit` 三项由检查器自己比对，`source-blob` 是上游该提交里该文件的 git blob 哈希（人工核对用，检查器不读）。

升级步骤：`git worktree add --detach <临时目录> <新提交>` → 覆盖 `tools/check_layering.py` → 改 `SOURCE_COMMIT` → 同步 `tools/CHECKER.lock` 四行 → 跑下面两条命令，确认锁校验通过、ERROR 仍为 0，并逐条解释 WARN 的变化。

本仓没有独立的 OSAL 层（`src/osal` 只有 Cortex-M3 时基与临界区），角色清单 [.arch-roles](.arch-roles) 按真实目录写：`platform/`、`src/port/` = bsp；`src/osal/` = osal；`src/core/`、`src/components/` = components；`include/pm/` = common（纯类型/常量/原型，任何层可包含）；`src/app/main.c` = glue。`third_party/` 与 `build-*/` 用 `ignore` 指令排除在扫描外——厂商包在最底层、不会反调上层，扫它只花时间。**代价**：`third_party/` 里的厂商宏不再进宏索引，若将来组件层直接用 HAL 宏拼寄存器，第 14 项可能漏报；本仓目前没有这种用法。

CI 是两步（见 [.github/workflows/layering.yml](.github/workflows/layering.yml)），任一步非零即失败；本地复跑同样两条：

```powershell
python tools/check_layering.py --version --lock tools/CHECKER.lock   # 检查器身份/版本漂移
python tools/check_layering.py . --glue-max-lines 86                 # 扫描
```

**ERROR 必须为 0（否则 CI 红）；WARN 只打印、不判失败。**

当前 4 条 WARN 是同一种、也是设计使然：`src/port/stm32f1/pm_port_stm32f1.c` 的 UART 中断里调用 `pm_uart_hw_rx_byte` / `pm_uart_hw_tx_complete` / `pm_uart_hw_error`，以及 `src/port/stm32f1/pm_encoder_stm32f1.c` 的 EXTI9_5 中断里调用 `pm_encoder_hw_edge`。中断服务留在端口层，只把字节/事件/AB 电平递给 `src/core/pm_uart.c`、`src/core/pm_encoder.c` 的钩子，正落在检查器第 4 项“bsp 调组件 ISR 钩子”的 WARN 分支上；确认仅中断上下文使用即可，既不升级成 ERROR 也不隐藏。

扫描耗时（本机 Windows，同一棵树）：检查器 3.0 无 ignore **24.1 s**；3.2.0 无 ignore **22.8 s**；3.2.0 加 `ignore` 后 **0.19 s**。三者 ERROR/WARN 逐条相同。

`--glue-max-lines` 是棘轮：经过 S3–S10 拆分，`src/app/main.c` 现在为 **86 行**（检查器口径；2026-11-20 复跑 `--glue-max-lines 86` 为 0 ERROR，压到 85 时提示 `入口胶水层 86 行代码（上限 85）`），CI 与本地都用 `--glue-max-lines 86`——**86 是实测值、无余量，再增一行即红**。拆分前的历史（1277 → 1283 → 1500 → 1663/1668，上限一度抬到 1700）与取舍理由在 [PATCHES.md](PATCHES.md) §20、§27.5、§29.5；再往 `main.c` 加行前先评估是否该下沉到组件层，确认后再同步抬高闸门。

检查器 3.2.0 新增的 `layers` 指令本仓没有启用：它改变的是“没有 components 层时 APP 可直调 BSP”这一条放行规则，而本仓有真实的 components 层，声明了也不会改变任何一条判定。

---

## 命令接口

USART2（115200）与 USART3（9600，蓝牙透传）跑**同一个解析器，两路完全等价**：命令大小写不敏感；行尾 `\r` / `\n` 可带可不带，不带时空闲 **100 ms** 自动提交；**任何命令都必有一条回包**（未知命令回 `ERR unknown=<原文>`），回包是单行 `KEY=value`，以 `\r\n` 结尾。

| 命令 | 作用 |
| --- | --- |
| `HELP` | 列出全部命令（15 行；ON 660 B / OFF 654 B） |
| `PING` | 回 `PONG` |
| `STAT` | 时钟、运行秒数、按键计数、串口累计字节 |
| `I2C` | 扫描 I2C1，列 8 位写地址（本板 `0x70`、`0x7A`） |
| `KEYS` | 读按键计数并清零 |
| `AHT` / `AHTS` | 读 AHT20 温湿度；`AHTS` 是 repeated START 对照，拿到的是上一次结果 |
| `DIST` | 单次 HC-SR04 测距；默认 `PM_ENABLE_SONAR=OFF` 回 `DIST err=disabled`；ON 时用 DWT 时基输出 `cm/us`（决策 0020） |
| `ADC` | 读 PA5 电位器与 PA4 NTC 的原始值与 mV |
| `PWM` | `stat` 看舵机/蜂鸣器状态；`servo <0..180>` 设角度；`buz <100..5000> [duty]` 开蜂鸣器、`buz off` 关；`sweep` 0→180→0 慢扫两轮（每档 300 ms，结束回 90°），用于肉眼确认；`dbg` 只读打印 TIM4/RCC/GPIOB 寄存器、两次 CNT 采样与按 PSC/ARR 反算的 `fpwm`；`test` 把 PB8 临时切成 GPIO 推 1 Hz 方波 10 s（诊断用，阻塞 10 s，结束后恢复复用与 90°） |
| `TRACK` | 读 PB14 循迹模块的原始电平（`raw=1` 未遮挡 / `raw=0` 黑物或遮挡） |
| `ENC` | 读编码器 32 位计数、最后方向、A/B 电平与 PB15 按键（`1`=松开）；`ENC clear` 清零 |
| `RELAY` | `on` / `off` 驱动 PB5 继电器线圈，`stat` 读回 PB5 实际电平（`pb5=0\|1`） |
| `MOTOR` | `fwd [duty]` / `rev [duty]` / `stop` / `brake` / `duty <0..100>` / `stat`；PA0/PA1 = TIM2_CH1/CH2，20 kHz，回包带 `hz/arr/ccr1/ccr2` 寄存器读回；TIM2/TIM4 由模型 `pm_timer` claim 表仲裁（open claim / close release / 改频 retime）。另有隐藏诊断 `claims/conflict/busy/force timer\|remap`：只用于调试，不在 `HELP`，不影响正常启动与 `fwd/stat/stop` 回包，生产固件不得依赖 |
| `OLED on\|off\|clear\|test\|show\|frame\|col\|live\|dump` | 显示开关、清屏、测试页、静态文字、边框图案、列偏移、500 ms live（live 期间 1 Hz 自动测距仅在 `PM_ENABLE_SONAR=ON` 时启用） |

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

- `pm_*` 契约只有 GPIO、UART、PWM、输入侧、继电器、电机与定时器仲裁（`pm_pwm.h`、`pm_track.h`、`pm_encoder.h`、`pm_relay.h`、`pm_motor.h`、`pm_timer.h`）；I2C1 轮询主机、HC-SR04 捕获已下沉到 `src/port/stm32f1/pm_i2c1_stm32f1.c` 与 `pm_sonar_stm32f1.c`，OLED 调度在 `src/components/oled_ui/`，`src/app/main.c` 只剩初始化与主循环；
- SPI 只有 HAL 源和默认关闭的 `PM_DEMO_SPI` 开关，本树没有 demo 实现；RTC、Flash 编程、看门狗、DMA 均未实现（ADC1 已在 `src/components/adc_dev/` + `src/port/stm32f1/pm_adc1_stm32f1.c` 上板）；
- PWM：TIM4 固定 50 Hz 只服务舵机 PB8；时基由模型 `pm_pwm_timing_compute` 选定为 `PSC=21`、`ARR=65453`（刻度 0.3056 µs，周期 20.00017 ms），不是早期手写驱动的 71/19999（决策 0022）；舵机五档脉宽已上板验收（§140），角度精度/到位抖动/回弹仍未采集；蜂鸣器 PB9 由 TIM3 更新中断翻转 GPIO（100–5000 Hz，回包频率是 10 µs 量化后的实际值）；`PWM sweep` 阻塞约 5.7 s，期间 live 页与 `hb` 暂停、结束后补发；
- 输入侧：PB14 只有一路数字量，"单路遮挡"与"全黑"同为 `raw=0`，固件不可区分；编码器 x4 计数为 32 位有符号，两个方向符号相反，但符号与物理顺时针/逆时针的对应**未目视确认**；`dir` 只在有效跳变时更新、停手后不归零（已定案，决策 0022）；PB15 按键已抓到按下与松开（`btn` 1→0→1，约 3.5 s，M8），极性为 `1`=松开、`0`=按下（PB15 上拉输入）；
- UART 是逐字节中断，没有 DMA + 空闲中断的接收路径；TX 环 512 B，超长回包靠"边发边等"（`tx_bytes_all`）；
- live 模式每 500 ms 推一页（130 B ≈ 12 ms @ 100 kHz I2C），整屏 8 页约 98 ms；live 期间静态命令（`clear`/`test`/`show`/`frame`）照常接管，3 s 后自动交回；
- **测距默认关闭、可编译启用**：默认 `PM_ENABLE_SONAR=OFF`，`DIST` 回 `DIST err=disabled`；`-DPM_ENABLE_SONAR=ON` 时 1 µs 时基换成 `DWT->CYCCNT`（不再占用 TIM2），`EXTI15_10` 优先级 5 低于编码器 `EXTI9_5` 的 4（决策 0020）。ON 构建的绝对距离**已由用户现场验收（2026-11-20 晚，“没有问题，可通过”）**，但未与物理尺子逐点对照、精度未量化；编码器与 sonar 并发也已由用户口述通过（无记录文件，§142）；
- 电机：TIM2 `PSC=0 / ARR=3599` → 20 kHz，只有一路通道出 PWM、另一路恒 0（DRV8833 单向驱动）；`duty 0` 与 `stop` 电气等价（两路 CCR=0、全低滑行），`dir` 字段只是状态记忆；`MOTOR brake` 是新增的独立命令（两路 1000‰ → CCR=3600/3600，低边双开），`stop` 仍是滑行、回包不变（M4 基线）；brake 后 `fwd/rev/duty` 会立即退出且不残留 1000‰；**"电机是否真的制动、制动多快"未验证**（需人看转子），`fwd/rev` 对应轴的哪个方向也未验证；电机未接时只能验证寄存器与回包；TIM2/TIM4 的 owner 与时基由 `pm_timer` claim 表仲裁，`PM_ERR_BUSY` 是模型外占用（板端不可修、别重试），`PM_ERR_CONFLICT` 是表内冲突（可修）；隐藏的 `MOTOR claims/conflict/busy/force` 只用于取证，不在 `HELP`、生产不依赖；
- 分层检查：本仓用 `tools/check_layering.py`（3.4.0，`tools/CHECKER.lock` 固定 `4da1a77`）扫描，当前 0 ERROR / 4 WARN（2026-11-20，`--glue-max-lines 86`；第 4 条为编码器 EXTI 钩子，与 UART 三条同类）；从 3.3.1 升到 3.4.0 只新增第 28 项 `feature_gate`；本仓已在 `.arch-roles` 声明 `feature sonar = off` 并挂 `pm_sonar_init` / `EXTI15_10_IRQn`，该项复跑报 0、无误报（INDEX §131），其余输出不变，升级细节、复跑结果与"是否继续跟随上游"记在 `TECH_DEBT.md` D25；
- 无行尾命令有 100 ms 判定窗口，碎片间隔小于它会被当成同一条命令的延续；
- 默认（OFF）构建里 `DIST` 在启动前就返回 `err=disabled`，不会进入测量。ON 构建的可达现象保留在案：自动测距结束后 60 ms 内手动 `DIST` 可能回 `err=timeout`，是 HC-SR04 余振，不是故障；输入侧抓包里未发任何 PWM/BUZ 命令也出现过 3.9–45.8 cm 短读数与一次 `err=timeout`（与手/黑物靠近超声视场时间重合，归因未确认，§31.6）；
- 没有 CMake presets；CI 只有静态分层检查一步（`.github/workflows/layering.yml`）；对象结构体布局不承诺 ABI 稳定。

## 第三方代码

`third_party/` 内为 ARM 的 CMSIS（Apache-2.0）与 ST 的 HAL 驱动子集（BSD-3-Clause），原样分发，版权归各自所有者。本仓库自带链接脚本与启动配置，未复制 ST 的链接脚本模板（其条款禁止再分发）。

## 许可

本仓以 MIT 许可证发布，全文见 [LICENSE](LICENSE)；`third_party/` 内第三方代码仍按其各自许可证分发（见上节）。
