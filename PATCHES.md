# board-bringup 补丁说明

基线：`2026-11-18\peripheral-model\work`（复制时快照）
本目录：`2026-11-18\board-bringup\work`

复制时 `peripheral-model/work` 正被另一个会话修改（20:09:48 新增 `src/core/pm_spi.c`，
`CMakeLists.txt` 已引用但文件尚未落盘）。因此本副本只取 GPIO/UART 相关的稳定部分，
并做了下面第 1 条修正。将来并回主线时按顺序核对。

## 1. CMakeLists.txt：去掉 pm_spi.c

- 位置：`add_library(pm_core STATIC ...)`
- 变更：删除 `src/core/pm_spi.c` 一行
- 原因：复制快照里该文件不存在（并发编辑的中间态），且本任务不使用 SPI
- 并回主线时：若主线已有 `pm_spi.c`，**撤销此条**，不要删除

## 2. platform/stm32f103/pm_platform.c：补时钟树初始化

- 变更：新增 `pm_clock_init()`，在 `HAL_Init()` 之后调用
  - HSE 8 MHz → PLL ×9 → SYSCLK 72 MHz
  - AHB /1（72 MHz）、APB1 /2（36 MHz）、APB2 /1（72 MHz）
  - Flash 等待周期 `FLASH_LATENCY_2`
  - HSE 起振失败时直接返回，保持 HSI 8 MHz，不阻塞启动
- 原因：工程内 vendored 的 CMSIS `SystemInit()` 只设置向量表，不配置 PLL；
  原注释「Clock tree is configured by SystemInit()」与实际不符，原模型实际跑 HSI 8 MHz
- 影响面：只影响本平台层，`pm_*` 之上无感知；`HAL_RCC_ClockConfig()` 会重载 SysTick
- **已在板端验证通过**（2026-11-18，ST-LINK V2J48S7，目标电压 3.28 V）：
  - 读 `RCC_CFGR`（0x40021004）= `0x001D040A`：SWS=0b10（PLL）、PLLSRC=HSE、PLLMUL=×9、PPRE1=HCLK/2
  - 固件上电 banner 打印 `core clock = 72000000 Hz`
  - `hb` 每秒一行的实际间隔为 890–1024 ms（PC 侧时间戳），无累积漂移

## 3. platform/stm32f103/pm_platform.h：修正注释

- 仅注释：说明 `pm_platform_init()` 负责时钟树，不再声称由 SystemInit 配置

## 4. src/app/main.c：按键读取与串口输出

- 变更：
  - 新增 `s_key_cfg`（输入、内部上拉）——对应 HARDWARE.md 第 7 节第 1 条：
    KEY2(PB13) 无外部上拉，必须开内部上拉；KEY1 虽有外部 10 kΩ，同样开内部上拉保持一致
  - 新增 `key_service()`：20 ms 消抖，按下/松开时打印 `KEY1 pressed` 等
  - 绿色 LED(PA7) 镜像 KEY1，红色 LED(PB0) 镜像 KEY2（便于无串口时肉眼验证按键）
  - 上电打印 banner：核心时钟频率 + 串口参数
  - 保留蓝色 LED(PA6) 500 ms 心跳、每秒 `hb`、USART2 回显
  - 修正 `uart_service()`：读到 0 字节时退出循环，避免空转
- 原因：原 demo 只有 LED 心跳和回显，没有按键路径

## 5. src/port/stm32f1/pm_port_stm32f1.c：UART RX 引脚改为上拉输入

- 位置：`uart_gpio_init()`
- 变更：TX 保持 `GPIO_MODE_AF_PP`；RX 由 `GPIO_MODE_AF_PP` 改为
  `GPIO_MODE_INPUT` + `GPIO_PULLUP`（USART1/2/3 三个实例一并改）
- 原因：STM32F1 上把 RX 配成 AF 推挽会让 GPIO 输出驱动器一直占着引脚，
  对端 TX 无法把电平送进 USART。原代码三处 RX 都写成 AF_PP
- 故障现象：串口能发不能收，PC 发送无回显
- 取证（板上读寄存器）：
  - `GPIOA_CRL`（0x40010800）= `0x2244BB44`，PA3 nibble=`0xB`（AF 推挽输出）
  - `USART2_CR1`（0x4000440C）= `0x202C`，RE/RXNEIE/UE 均已使能
  - `USART2_SR`（0x40004400）= `0x00C0`，只有 TXE/TC，**RXNE 从未置位**
  - `USART2_BRR`（0x40004408）= `0x0138`，36 MHz / 115200 精确匹配，排除波特率
- **已在板端验证通过**（2026-11-18）：
  - 修复后读 `GPIOA_CRL` = `0x22448B44`：PA2 nibble=`0xB`（AF 推挽 TX），PA3 nibble=`0x8`（上拉输入 RX）
  - PC 发 `PING-123\r\n` → 原样收回 `PING-123\r\n`
  - PC 发 `hello-STM32!` → 原样收回 `hello-STM32!`
  - 期间 `hb` 不中断，主循环未被阻塞

## 6. 未改动项：板级映射表

`platform/stm32f103/board_f103c8.{c,h}` 现有映射与 HARDWARE.md 一致，未改：

| 逻辑名 | 引脚 | 依据（HARDWARE.md） |
| --- | --- | --- |
| LED_BLUE | PA6 | §3 第 15 条 彩色 LED 蓝 |
| LED_GREEN | PA7 | §3 第 15 条 彩色 LED 绿 |
| LED_RED | PB0 | §3 第 15 条 彩色 LED 红 |
| KEY1 | PB12 | §3 第 10 条 轻触按键 KEY1（外部 10 kΩ 上拉） |
| KEY2 | PB13 | §3 第 10 条 轻触按键 KEY2（无外部上拉） |
| UART_DEBUG（USART2） | PA2=TX / PA3=RX | §3 第 2 条 USB 串口，CH343P |

## 7. 板端验证记录：按键与消抖

- 记录文件：`key-capture2.log`（90 秒窗口，PC 侧时间戳，粒度约 25–40 ms）
- 共录到 6 组按下/松开（KEY2 四次、KEY1 两次），每次按压只产生一条 `pressed` 和一条
  对应的 `released`，按键名称无串，无孤立边沿
- 其中包含两次短敲（466 ms、357 ms），均被完整记录，说明消抖没有吞掉短按
- 绿/红 LED 的镜像写入与打印在 `key_service()` 同一分支内，串口日志本身不含 LED 行；
  LED 跟随按键的现象由用户目视确认
- **消抖有效性只到事件级**：未做 1 ms 级原始跳变采样，无法量化证明接触抖动被滤掉；
  当前结论仅为“在事件粒度上无重复触发、无漏触发”

## 8. 供电结论更正：5 V 供电合规，不做接线改动

- 原判断：依据 DX-BT24 **芯片**手册供电 2.8–3.6 V，认为板上 5 V 供电超限，建议改接 3.3 V
- 更正依据：用户查看模块**实物丝印**，Power 标识为 **3.6–6 V**（模块板自带稳压）
- 结论：**5 V 供电合规，撤销改线建议，不碰接线**
- 教训：芯片手册的供电范围 ≠ 模块板的输入范围，两者不可混用

## 9. USART3 蓝牙命令-上报通道 + I2C1 总线扫描

- 移除 `src/app/main.c` 里 60 s 的 PB11 位宽测量窗口（EXTI/DWT），USART3 固定 9600 8N1
- 新增共享命令解析器（`cmd_feed`/`cmd_execute`），USART2 与 USART3 同一套逻辑，
  回复原路返回；行尾 CR/LF/CRLF 均可，命令大小写不敏感，单行 `key=value`，`\r\n` 结尾
- 命令集：`HELP` / `PING` / `STAT` / `I2C` / `KEYS`，未知命令回 `ERR unknown=<原文>`
- `STAT`：`clk` 取 `pm_platform_core_clock()`，`up` 取 `pm_osal_now_ms()`，`tick=1`（1 ms），
  `key1/key2` 为消抖后的按下次数，`uart_rx/uart_tx` 为两路累计字节数
- `KEYS` 读取后清零计数与 `last`
- `I2C`：裸寄存器轮询扫描 I2C1（PB6=SCL / PB7=SDA，100 kHz @ APB1 36 MHz），
  报 8 位写地址；卡死时 `i2c1_bus_fix()` 用 SWRST + 9 个 SCL 时钟恢复
  （工程未引入 HAL I2C，也未改 CMake）
- 无动态分配：全部为静态缓冲，未引入 malloc/free
- **已在板端验证通过**（2026-11-18，ST-Link `V2J48S7`，验证后已释放）：
  ```
  TX: PING   → PONG
  TX: help   → HELP HELP=list commands / HELP PING=reply PONG /
                HELP STAT=clk/up/tick/keys/uart counters /
                HELP I2C=scan I2C1 bus, list 8-bit addresses /
                HELP KEYS=key press counters, clears them
  TX: STAT   → STAT clk=72000000 up=13310 tick=1 key1=0 key2=0 uart_rx=54 uart_tx=753
  TX: I2C    → I2C found=0x70,0x7A
  TX: KEYS   → KEYS k1=0 k2=0 last=none
  TX: foo123 → ERR unknown=foo123
  ```
- I2C1 扫描结果：**AHT20(0x70) 与 OLED(0x7A) 都在总线上应答**，与用户确认的板载器件一致
- 资源占用：FLASH 8224 → **10156 B（15.50%）**，RAM 2256 → **2752 B（13.44%）**

## 10. 回显字节一致性（`hello-bt` 的 0x2D 问题）

- 串口捕获 `bt-ascii2.log` 中，USART3 收到的 8 字节为 `68 65 6C 6C 6F 2D 62 74`，
  第 6 字节为 **0x2D**，4 次重复发送（共 32 字节）同样都是 0x2D
- 固件在该版本中逐字节原样回写，没有任何字符过滤/替换逻辑 → **固件不改字节**
- 页面侧收到的显示为 `0x20`，需用 HEX 模式发 `68 65 6C 6C 6F 2D 62 74` 做对照实验定位

## 11. 修复：长回包在 USART3(9600) 被截断

### 现象与字节账

页面实测：发 `HELP`（5 B），200 ms 后收到 **152 B**，末行停在 `HELP KEYS=key pre`。

当时 HELP 共 5 行，逐行字节数（含 CRLF）：

| 行 | 内容 | 字节 |
| --- | --- | --- |
| 1 | `HELP HELP=list commands` | 25 |
| 2 | `HELP PING=reply PONG` | 22 |
| 3 | `HELP STAT=clk/up/tick/keys/uart counters` | 42 |
| 4 | `HELP I2C=scan I2C1 bus, list 8-bit addresses` | 46 |
| 5 | `HELP KEYS=key press counters, clears them` | 43 |
| | **合计** | **178** |

- 前 4 行合计 135 B，第 5 行前 17 B 是 `HELP KEYS=key pre` → **135 + 17 = 152**，
  与页面收到的 152 B 逐字节吻合，说明截断点就在固件，不在模块或页面
- 缺失 = 178 − 152 = **26 B**（`ss counters, clears them` 24 字符 + CRLF 2 B）

### 根因

- 位置：`src/core/pm_uart.c` 的 `pm_uart_write()`（约 140–200 行）
- 行为：循环 `pm_rb_write` 往 TX 环塞，塞不下就 `pm_osal_delay_ms(1)` 重试；
  **到达 `timeout_ms` 后直接把剩余字节计入 `stats.tx_dropped` 并返回 `PM_ERR_TIMEOUT`**，
  未写出的尾部被静默丢弃（`timeout_ms == 0` 时同理，返回 `PM_ERR_AGAIN`）
- 诱因：旧 `tx_str()` 每次调用只发一次 `pm_uart_write()` 且**忽略返回值和 `written`**，
  于是超时即丢尾
- 为什么只有蓝牙中招：TX 环旧容量 **128 B**；USART3 跑 9600，约 **1042 µs/字节**
  （≈960 B/s），128 B 排空要 133 ms，等待窗口内环只能再腾出几十字节；
  USART2 跑 115200，约 **87 µs/字节**，快约 12 倍，同样 128 B 环足够周转，
  所以 USB 侧从没出现截断

### 修法

不缩短 HELP 文本，改发送路径本身：

1. `src/app/main.c`：`s_bt_tx_storage` / `s_tx_storage` 均改为 **512 B**（原 128 B）
2. 新增 `tx_bytes_all()`：循环调用 `pm_uart_write(..., 1000u)` 直到 `off == n`，
   以 `written` 累计实际接受量，只有「一次都没接受」才退出，避免死循环
3. `tx_str()` 改为走 `tx_bytes_all()`

改后任何长度超过环容量的回包都会被"边发边等"地完整送出，不再丢尾。

### 验证（COM11，115200）

```
TX: HELP → raw_bytes=274   reply_bytes_incl_crlf=266   reply_lines=7
HELP HELP=list commands
HELP PING=reply PONG
HELP STAT=clk/up/tick/keys/uart counters
HELP I2C=scan I2C1 bus, list 8-bit addresses
HELP AHT=read AHT20, rh and t in 0.1 units
HELP AHTS=same but read via repeated START
HELP KEYS=key press counters, clears them
```

- 266 B 与 7 行完全匹配（raw 274 = 266 + 两条 `hb\r\n` 8 B），末行完整到 `clears them`
- 资源：FLASH 10156 → **11192 B（17.08%）**，RAM 2752 → **3136 B（15.31%）**
- **蓝牙侧未复验**：需要从页面发 `HELP`，同时用 COM11 的 `BT rx` 镜像确认

## 12. AHT20 温湿度读取（I2C1，0x70）

### 实现

- `AHT`：START → 写 `0x70` → 写 `AC 33 00` → 等 BTF → **STOP** → 延时 80 ms →
  重新 START → 写 `0x71` → 读 6 B → 停止
- `AHTS`：同样触发，但**不发 STOP**，保持总线 80 ms 后直接 repeated START 读，
  用于对照（这就是此前只有 mock 覆盖的路径）
- 校验：`d[0] & 0x80` 判忙，忙则 20 ms 后重试，最多 3 次
- 换算（数据手册 6.2，全整数，不用浮点）：
  - `raw_h = (d1<<12)|(d2<<4)|(d3>>4)`，`RH% = raw_h/2^20*100`
  - `raw_t = ((d3&0x0F)<<16)|(d4<<8)|d5`，`T = raw_t/2^20*200 - 50`
  - 上报保留 1 位小数（内部按 0.1 单位整数运算，直接截断）

### 实测（COM11，连续 5 条）

```
TX: I2C  → I2C found=0x70,0x7A
TX: AHT  → AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0
TX: AHTS → AHT err=busy raw=98 68 A7 F5 A3 11
TX: AHT  → AHT ok rh=40.8 t=20.4 raw=18 68 A6 F5 A2 AE rst=0
TX: AHTS → AHT err=busy raw=98 68 A6 F5 A2 AE
```

第 1 组换算：

- `raw_h = 0x68A7F = 428671` → `428671/1048576×100 = 40.879 %` → 报 `40.8`
- `raw_t = 0x5A311 = 369425` → `369425/1048576×200−50 = 20.462 °C` → 报 `20.4`
- `d[0] = 0x18`：bit7 忙=0，bit3 校准位=1（已校准），数据有效
- 第 2 组 `428655 / 369326` → 40.878 % / 20.443 °C，两次一致

### 结论：repeated START 读到的是上一次结果

- `AHTS` 的 6 字节 = **前一条 `AHT` 的 6 字节**，仅 `d[0]` 由 `0x18` 变成 `0x98`（多出 bit7 忙位）
- 两组对照完全重复同一规律 → AHT20 **必须以 STOP 结束触发命令才开始测量**；
  不发 STOP 就读，拿到的是上一次转换的旧数据 + 忙标志
- I2C 层本身没有异常：repeated START 正常产生 SB/ADDR，6 字节完整读回，
  没有 AF/BERR（否则会返回 `err=io`）
- 结论：**AHT 路径（STOP + 80 ms + 新 START）可用；AHTS 保留作对照，不作正常读数使用**

## 13. `AHT` 无回包：命令没被解析，不是发送失败

### 日志对账（`bt-verify.log`）

| 时间 | 抓到的 USART3 字节 | 行尾 | 对应的 `BT tx` 镜像 |
| --- | --- | --- | --- |
| 21:28:13.306 | `48 45 4C 50 0A` (`HELP\n`) | 有 `0A` | 有，25/22/42/46 B 四条 |
| 21:28:24.809 | `48 45 4C 50 0A` (`HELP\n`) | 有 `0A` | 同上 |
| 21:28:47.457 | `41 48 54` (`AHT`) | **无任何行尾** | **无** |
| 21:28:53.922 | `41 48 54` (`AHT`) | **无任何行尾** | **无** |

- 两次 `AHT` 都只有 3 个可见字节，没有 `0D`/`0A`
- 全程 `hb` 每秒一行、无中断 → 主循环没被阻塞，`cmd_aht` 根本没运行

### 复现与定位（COM11）

```
TX 'AHT' 无行尾，等 1500 ms   → 只有 hb，无回包
TX 'STAT\r\n'                 → ERR unknown=AHTAHTSTAT
```

回包里出现了**两个** `AHT`：一个是本次的，一个是上一轮遗留在行缓冲里的。这同时证明两件事：

1. `cmd_feed()` 只在 `\r`/`\n` 时调用 `cmd_execute()`，没有行尾就一直躺在缓冲里；
2. 遗留的半行会**污染后续命令**（`AHT` + `STAT` 被拼成 `AHTSTAT`）

页面发 `HELP` 时带了 `0A`，发 `AHT` 时没带（页面行尾设置不一致），所以一个走通、一个静默。

### 修法

1. **空闲冲刷**：`cmd_flush_idle()`，任一端口缓冲里有半行且 **100 ms** 内没有新字节，就执行它。
   带行尾的命令仍然立即执行，只有"没行尾"的才等这 100 ms。
2. **AHT 改非阻塞状态机**：`aht_start()` 只做触发（I2C 写 3 字节，约 100 µs），
   80 ms 测量窗口由主循环里的 `aht_service(now)` 按 tick 判到期后再读，命令处理不再 `delay_ms`。
   失败重试最多 3 次，**任何分支都必回一条**：
   成功 `AHT ok rh=.. t=.. raw=.. rst=..`，失败 `AHT err=busy|io raw=..`，
   上一次测量未结束则 `AHT err=busy pending`。
3. **修好了镜像失真**：上一轮日志里的 `BT tx n=44B: 48 45hb` 是
   `uart_write_str()` 用 `PM_UART_TIMEOUT_NOW` 写 USART2 时把镜像行尾部丢掉造成的
   （4 条镜像 ≈600 B 撑满 512 B 环）。该函数超时改为 50 ms，镜像不再被截断。
   这属于**测量工具失真，不是蓝牙发送截断**——页面实收 253+13=266 B 是完整的。

### 验证（COM11，全部"无行尾"发送）

```
TX 'AHT'  → 0.196 s 后  AHT ok rh=40.7 t=20.5 raw=18 68 5A 25 A5 4F rst=0
TX 'AHTS' → 0.283 s 后  AHT err=busy raw=98 68 5A 25 A5 4F
TX 'PING' → 0.107 s 后  PONG
TX 'ZZZ'  → 0.110 s 后  ERR unknown=ZZZ
TX 'HELP' → 0.108 s 后  7 行 / 266 B 完整
```

- `AHT` 从发送到回包约 **200 ms** = 100 ms 空闲判定 + 80 ms 测量 + 读取，符合设计
- 期间 `hb` 仍每秒一行，主循环未被阻塞
- 资源：FLASH 11192 → **11488 B（17.53%）**，RAM 3136 → **3160 B（15.43%）**

## 14. 蓝牙侧实测对账 + 空闲冲刷的时序 bug + HC-SR04 `DIST`

### 14.1 `bt-verify2.log` 对账（页面 21:36 两次操作）

| 页面时间 | USART3 收到 | `BT tx` 镜像 | 合计 |
| --- | --- | --- | --- |
| 21:36:36.829~.830 | `48 45 4C 50`（4 次单字节读） | 36.922~37.014 共 7 段：25+22+42+46+44+44+43 | **266 B** |
| 21:36:45.299~.300 | `41 48 54`（`AHT`，无行尾） | 45.486~45.498 共 23 段 | **51 B** |

- `HELP`：266 B 与 USB 侧完全一致，末行 `HELP KEYS=key press counters, clears them`
  完整；页面显示成 253+13 两个气泡是 BLE 分包，**按总字节判读**
- `AHT`：45.300 收到末字节 → 45.486 发出首字节，**186 ms** =
  100 ms 空闲判定 + 80 ms 测量窗口 + 串口排队，与设计吻合
- 回包内容逐字节核对：
  `raw=18 76 29 A5 A8 B7` → `raw_h=0x7629A=483994`，`483994/2^20×100 = 46.157 %` → `46.1`；
  `raw_t=0x5A8B7=370871`，`×200/2^20−50 = 20.737 °C` → `20.7`，与页面一致

### 14.2 新发现的 bug：空闲冲刷会被"提前"触发

同一次捕获里 21:36:24.947~.950 出现：

```
BT rx n=1B: 48 / 45 / 4C    (H E L)
BT tx n=12B: "ERR unknown=" / "HEL" / CRLF
BT rx n=1B: 50              (P)
BT tx: "ERR unknown=" / "P" / CRLF
```

`HELP` 被拆成了 `HEL` 和 `P` 两条。根因在主循环时序：

- `now = pm_osal_now_ms()` 在**循环开头**采样；
- `bt_service()` → `cmd_feed()` 在里面把 `c->last_ms` 更新成**更晚**的时间；
- 随后 `cmd_flush_idle(&s_ble, now)` 用旧的 `now` 去算 `now - last_ms`，
  无符号相减**下溢成极大值**，`>= 100 ms` 成立 → 立刻冲刷

同理 `aht_service(now)` 也可能在同一毫秒边界上跳过 80 ms 等待。

- 修法：`bt_service()` 之后再取一次时间戳 `t`，`cmd_flush_idle` / `aht_service` /
  `dist_service` 统一用 `t`
- 复验：把 `HELP` 拆成 4 次单字节写入、间隔 20~30 ms、不带行尾 →
  合成一条完整 `HELP`（8 行 / 304 B），不再出现 `ERR unknown=HEL`

### 14.3 HC-SR04（`DIST`）

引脚依据（`HARDWARE.md` §3 第 6 条 + 附录引脚表）：

| 信号 | 引脚 | 板载排针 | 5 V 依据 |
| --- | --- | --- | --- |
| TRIG | PA11 | 4P 第 3 脚 | 推挽输出，2 MHz |
| ECHO | PA10 | 4P 第 2 脚 | **PA10 为 FT（5 V 容忍）**；且该排针第 4 脚供 3.3 V，配套模块是 HC-SR04P |

- 无分压：板上该接口直接给模块 3.3 V，HC-SR04P 的 ECHO 就是 3.3 V 电平；
  即便换成老 HC-SR04 的 5 V 输出，PA10 仍属 FT，耐 5 V
- 实现：PA11 推挽输出、PA10 浮空输入；TIM2 1 MHz 自由运行做 µs 时基；
  ECHO 走 EXTI10 双边沿，ISR 只记时间戳，**主循环不等待**
- `DIST` 触发后立即返回，回包由 `dist_service()` 在下降沿或 60 ms 超时后发出，
  必有一条：`DIST cm=<值> us=<脉宽>` 或 `DIST err=timeout`
- 换算 `cm = us / 58`（保留 1 位小数，整数运算）；`us < 100` 或 `us > 25000` 判为
  无回波（HC-SR04 无反射时 ECHO 会持续约 38 ms）

### 14.4 实测（COM11，连续 15 次）

```
DIST cm=57.1 us=3314
DIST cm=76.5 us=4435
DIST cm=57.0 us=3307
DIST cm=76.4 us=4434
DIST cm=76.4 us=4434
DIST cm=76.4 us=4434
DIST cm=76.5 us=4435
DIST cm=76.0 us=4407
DIST cm=52.5 us=3045
DIST cm=76.8 us=4455
DIST cm=76.4 us=4434
DIST cm=76.0 us=4407
DIST cm=76.4 us=4433
DIST cm=76.4 us=4434
DIST cm=76.4 us=4434
```

- 主读数 **4434 µs / 76.4 cm**，同一目标重复测量抖动仅 **±1 µs**，说明时基与边沿捕获正常
- 偶发 3045~3314 µs（52.5~57.1 cm）：更近的一个回波（桌面边缘或模块旁的物体），
  不是时基误差——连续两次同值时差 0~1 µs
- 资源：FLASH 11488 → **12064 B（18.41%）**，RAM 3160 → **3176 B（15.51%）**

## 15. 板载 OLED（CH1116，0x7A）+ `OLED` 命令

### 15.1 型号与地址依据

| 事实 | 来源 |
| --- | --- |
| 驱动芯片 CH1116 | `HARDWARE.md` §3 第 9 条、§7 第 8 条、清单第 3 页 |
| I²C 地址 0x7A | 同上；**这是 8 位写地址**，对应 7 位 **0x3D** |
| SCL = PB6 / SDA = PB7 | `HARDWARE.md` §3 第 9、16 条 + 附录引脚表 |
| 128×64、1.3" | `HARDWARE.md` §8.2 模块规格书 **ZJY130-2864KSWLG01**（"2864" = 128×64） |
| 控制字节 0x00 / 0x40 | CH1116/SSD1306 系 I²C 帧：Co=0、D/C#=0 命令流；Co=0、D/C#=1 数据流 |

上述地址由 `I2C` 命令实测复核：`I2C found=0x70,0x7A`（扫描本身就是 `7 位 << 1`，
所以 0x7A 是 8 位写地址，0x3D 是 7 位地址，两者不矛盾）。

实测交叉验证：上电初始化序列 24 B 全部收到 ACK（`af=0 berr=0 to=0`），
随后 8 页 × 128 B GDDRAM 写入无一次 NACK，说明 0x7A 上确有该控制器在响应。
CH1116 的 I²C 模式**不支持读回状态或 GDDRAM**（无读时序），所以无法像 SPI 那样
读寄存器确认型号；型号判定依据 = HARDWARE.md 记录 + 规格书 + 初始化序列全 ACK。
与 HARDWARE.md **无冲突**。

### 15.2 驱动

- `src/components/oled_ch1116/oled_ch1116.{h,c}`（组件层，无寄存器访问）
- 总线通过回调注入：`oled_ch1116_bus_fn(ctx, control, data, len)`；
  入口胶水层 `src/app/main.c` 的 `oled_bus_write()` 用 I2C1 裸寄存器实现该回调
- 能力：`init` / `display(on/off)` / `clear` / `stage_line`（一行 ASCII，指定页）/
  `start_pattern(0x00 清屏、0xAA 棋盘格、0xFF 全亮)` / `start_text(起页, 页数)` / `service`
- 字体：**5×7 点阵放在 6×8 格内**（96 个可打印 ASCII，`s_font5x7[96][5]`，480 B Flash），
  不引入字库文件，无动态分配，无帧缓冲（每页 128 B 现场生成）
- 寻址：页地址模式（`0x20 0x02`），每页先发 `0xB0|page`、`0x00`、`0x10` 再发 128 B 数据

### 15.3 非阻塞方式与阻塞时间

- **轮询读 SR1 标志**（不是中断状态机）：I2C1 没有接 IRQ，为单个器件改端口层不划算
- 命令处理只登记任务，立即回包由 `oled_service()` 在主循环发出
- 每次 `oled_service()` 只推**一页**：3 B 命令 + 1 B 地址 + 1 B 控制 + 128 B 数据 ≈ 130 B，
  100 kHz 下 **≈ 12 ms**；整屏 8 页分 8 次主循环完成
- 实测整帧耗时：`test`/`clear` **98 ms**、`show`（3 页）**37 ms**，
  期间 `hb` 未断
- 单次等待上限 `OLED_FRAME_SPIN = 50000` 次轮询（> 单字节 90 µs 的数十倍），
  超时计入 `to=`，不会无限挂死

### 15.4 命令与实测回包（COM11，115200）

```
OLED err=usage                          (TX "OLED"，无参数)
OLED err=usage                          (TX "OLED bogus")
OLED test ok ms=98 af=0 berr=0 to=0
OLED show ok ms=37 af=0 berr=0 to=0
OLED clear ok ms=97 af=0 berr=0 to=0
OLED off ok af=0 berr=0 to=0
OLED on ok af=0 berr=0 to=0
```

带真实距离的 `show`：

```
TX DIST        → DIST cm=79.0 us=4580
TX OLED show   → OLED show ok ms=37 af=0 berr=0 to=0
```

- 任何分支都有回包：成功 `<子命令> ok`，失败 `OLED err=io|usage|busy pending|busy i2c`
- `af` / `berr` / `to` 是累计计数，本轮全部为 **0**
- 上电日志：`OLED CH1116 addr=7A init ok, clearing` → `OLED ready (128x64, 6x8 font)`

### 15.5 屏上应有现象（需肉眼确认）

| 命令 | 屏幕 |
| --- | --- |
| 上电 | 全黑（init + 整屏清零） |
| `OLED test` | 整屏 1 像素棋盘格（黑/白交替），用于看坏点与列/页错位 |
| `OLED show` | 第 1 行 `UP=<秒>S`，第 2 行 `K1=<n> K2=<n>`，第 3 行 `DIST=<cm>CM`（无读数则 `DIST=NONE`） |
| `OLED clear` | 全黑 |
| `OLED off` | 立即全黑（面板关闭，GDDRAM 内容保留） |
| `OLED on` | 恢复关闭前的内容 |

### 15.6 资源

- FLASH 12064 → **14664 B（22.38%）**，RAM 3176 → **3400 B（16.60%）**
  （含 HELP 新增 1 行 36 B；OLED 本身约 +2.56 KB Flash / +224 B RAM）
- 动态分配：**无**（无 malloc/free，全部静态缓冲）

## 16. OLED 列偏移修正（132 列显存 / 128 列玻璃）

### 16.1 现象与判断

照片：`UP=525S` 的 `U` 左竖笔画缺失（像 `JP`），右侧边缘一条淡竖带；三行文字本身正确。
判断：写入起始列是 0，但玻璃只露出 128 列，可见窗口不是 0..127。

### 16.2 CH1116 手册依据（V0.2，取自 HARDWARE.md 第 246 行链接的 PDF，`pymupdf` 抽取）

| 手册条目 | 内容 |
| --- | --- |
| Features / General Description | 132×64 SRAM，132 个 segment、64 个 common，最大 132×64 |
| Block Diagram | 段输出 `SEG0 … SEG131`，行输出 `COM0 … COM63` |
| Command 1 / 2 | `Set Lower Column Address (00H–0FH)`、`Set Higher Column Address (10H–1FH)`；列地址计数器"incremented during each access until address 131" |
| 上电缺省（p24） | `SEG0 is mapped to column address 00H and COM0 mapped to row address 00H` |
| Command 12 | `Set Segment Re-map (A0H–A1H)`；`ADC = "L"` 正常方向（POR），`ADC = "H"` 反向 |
| Command 19 | `Set Common Output Scan Direction (C0H–C8H)` |

结论：**列地址范围是 0..131，不是 0..127**。这块模块的 128 列玻璃接在
`SEG2..SEG129` 上，所以可见窗口是 **RAM 列 2..129**；`A0/A1` 只反转左右顺序，
不改变可见集合（`RAM n → SEG(131-n)`，`SEG2..129` 仍对应 `RAM 2..129`）。
这就解释了"整体左移 2 像素"。

### 16.3 当前初始化序列逐条对账

| 字节 | 含义 | 判定 |
| --- | --- | --- |
| `AE` | 显示关 | 正常 |
| `D5 80` / `A8 3F` / `D3 00` / `40` | 时钟、1/64 multiplex、偏移 0、起始行 0 | 正常 |
| `8D 14` | 电荷泵开 | 正常 |
| `20 02` | 页地址模式 | 正常（实测逐页写入有效） |
| `A1` | ADC=1，段反向 | 保留：文字方向正确，说明模块装配方向要 A1 |
| `C8` | COM 反向扫描 | 保留：画面上下方向正确 |
| `DA 12` / `81 CF` / `D9 F1` / `DB 40` / `A4` / `A6` / `2E` / `AF` | COM 引脚、对比度、预充、VCOMH、正常显示、关滚动、开显示 | 正常 |
| **列起始 `00` + `10`** | 写 RAM 列 0 | **错**，改成 `02` + `10`（列 2） |

`A0/A1`、`C0/C8` 都不是本例的问题：它们只影响镜像/上下翻转，而照片里文字方向是对的。

### 16.4 改动

- 组件新增 `col_offset`（默认 `OLED_CH1116_COL_OFFSET = 2`），
  `oled_ch1116_service()` 的列地址命令改为 `0x00|(col&0x0F)`、`0x10|((col>>4)&0x0F)`
- 新增 `OLED frame`：1 px 外框 + 每 16 列一条竖线（页 0/7 全亮做上下边）
- 新增诊断 `OLED col <0..4>`：运行期改列起始，用于同一次烧录里做 A/B，不必重烧
- `OLED` 回包格式不变，`frame` 也走异步任务、完成时回包

### 16.5 实测回包（COM11）

```
OLED frame ok ms=98 af=0 berr=0 to=0      ← col=2（默认）
OLED col=0 ok
OLED frame ok ms=98 af=0 berr=0 to=0      ← 复现旧故障
OLED col=2 ok
OLED frame ok ms=98 af=0 berr=0 to=0      ← 修正后
OLED show  ok ms=37 af=0 berr=0 to=0
OLED err=usage col=0..4                   ← "OLED col"（缺参数）
OLED err=usage col=0..4                   ← "OLED col 9"（越界）
```

### 16.6 右侧淡竖带的来源

不是玻璃边缘，也不是残像，而是 **RAM 列 128、129 从没被写过**：
本轮所有绘图都从列 0 写 128 B，只覆盖 RAM 0..127，而可见窗口是 2..129，
于是 128/129 保持上电后的随机内容，在右边缘显示成一条淡淡的点状竖带
（`OLED test` 的棋盘格也没写到这里，因为它同样只写 128 B）。
改成列 2 后写入范围是 RAM 2..129，正好覆盖可见窗口，这条带子应当消失——
**这是可判定的验证点**：若改完仍在，说明还有别的原因，不是列偏移。

### 16.7 资源

- FLASH 14664 → **15076 B（23.00%）**，RAM 3400 B 不变（16.60%）
- 动态分配：无

## 17. `OLED frame` 画面不干净：根因与确定性修复

### 17.1 三条问题的代码证据

**Q1 `frame` 是否先清屏，page 0 / page 7 的 0xFF 是否发出并 ACK**

- `frame` 没有单独的"清屏命令"，但每一页的 128 B 缓冲都从 `memset` 重建后整页下发
  （`oled_ch1116.c`：`build_frame_page()` 中页 0/7 `memset(out,0xFF,128)`，
  其余页先 `memset(out,0,128)` 再置竖线；`oled_ch1116_service()` 每页固定发
  `B0|page` + 列地址 + 128 B 数据），所以屏上结果与"先清屏再画"完全等价。
- page 0 / page 7 的 0xFF 都走同一个 `oled_bus_write()`（`main.c` L846–884）：
  `START → 0x7A → ADDR(从机 ACK 清 ADDR 位) → 控制字节 0x40 → 128 B 逐字节等 TXE
  → 等 BTF → STOP`；任何一步 AF/BERR/超时都会把 `rc` 置负数并经 `o->err` 上报。
  实测 `af=0 berr=0 to=0`，即 8 页（含首末页）全部被 ACK，无失败。

**Q2 文字为什么还在**

- 不是自动刷新：全工程只有 `cmd_oled()` 里 `OLED show` 会调用 `oled_stage_show()`
  （`main.c` L1045），主循环只调用 `oled_service()` 推页，不重新排字。
- 真因是当时的 `show` 只写 page 0..2（3 页），`frame` 写的 page 3..7 原封不动留下来了。
  照片正好是"page 0..2 文字 + page 3..6 竖线 + page 7 实心带"。
  本轮把 `show` 改为写满 8 页（page 3..7 写空），`main.c` L1046：
  `oled_ch1116_start_text(&s_oled, 0u, OLED_CH1116_PAGES)`。

**Q3 竖线 8 条而不是 9 条**

- `build_frame_page()` 的坐标没有越界、没有取整：竖线列只有
  `x=0`、`x=127` 与 `x%16==0` 的 `16/32/48/64/80/96/112`，共 9 条。
- col=2 时 `x=0 → RAM 2`、`x=127 → RAM 129`，都落在可见窗口 2..129 内，9 条都应可见。
- col=0 时 `x=0 → RAM 0` 在窗口之外，只剩 8 条 —— 照片正好是 8 条。
  上一轮 `OLED col 0` / `col 2` 的 A/B 回包里没有打印 col，没法从日志判定
  那张照片到底对应哪个偏移；本轮把 `col=<n>` 加进了所有 OLED 完成回包，
  这个歧义以后不会再出现。

**Q4 顶部没有实心边**

- 不是页循环从 1 开始：`oled_ch1116_start_frame()` 是 `next_page=0`、`end_page=8`。
- 是 `show` 只写 page 0..2，page 0 被文字覆盖。改为 8 页后，`show` 不再留残影。

### 17.2 改动

| 文件 | 改动 |
| --- | --- |
| `src/components/oled_ch1116/oled_ch1116.c` | `build_frame_page()` 注释写清每页布局；逻辑保持"逐页重建缓冲 → 整页下发"，8 页一次 job |
| `src/app/main.c` | `OLED show` 写满 8 页（page 3..7 清空） |
| `src/app/main.c` | `OLED col <0..4>` 设置后自动重画当前图案，回包追加 `redraw=<name>` |
| `src/app/main.c` | 所有 OLED 完成回包追加 ` col=<n>` |
| `src/app/main.c` | `oled_boot()` 清屏后置 `s_oled_last = CLEAR`，`col` 的 A/B 在开机后即可用 |

"图案模式标志位"没有加：本固件不存在周期性文字刷新，`show` 只在收到命令时执行，
且任何 OLED 命令在 `s_oled_req != NULL` 时直接回 `OLED err=busy pending`
（`main.c` L1056–1059），绘图期间插不进第二条命令。加一个没有消费者的标志位
只会变成死代码。

### 17.3 `frame` 位图布局（每页 128 B，字节 bit0 在上）

| 页 | 内容 |
| --- | --- |
| 0 | 128 列全 `0xFF` → 顶部 8 行实心带 |
| 1..6 | 仅 `x = 0,16,32,48,64,80,96,112,127` 为 `0xFF`，其余 0 → 9 条通高竖线 |
| 7 | 128 列全 `0xFF` → 底部 8 行实心带 |

col=2 时写入 RAM 2..129，覆盖全部 128 列可见玻璃。预期：上下各一条实心带，
中间 **9 条竖线**（左/右边框 + 每 16 列 1 条）。

### 17.4 实测（COM11，115200，2026-11-18）

```
board-bringup
core clock = 72000000 Hz
OLED CH1116 addr=7A init ok, col=2, clearing
OLED ready (128x64, 6x8 font)

OLED frame ok ms=98 col=2 af=0 berr=0 to=0
OLED show  ok ms=98 col=2 af=0 berr=0 to=0
OLED frame ok ms=97 col=2 af=0 berr=0 to=0
OLED col=0 ok redraw=frame
OLED redraw ok ms=98 col=0 af=0 berr=0 to=0
OLED col=2 ok redraw=frame
OLED redraw ok ms=98 col=2 af=0 berr=0 to=0
OLED test  ok ms=98 col=2 af=0 berr=0 to=0
OLED clear ok ms=97 col=2 af=0 berr=0 to=0
```

- `show` 由 37 ms 变为 98 ms（3 页 → 8 页）
- 最后补发一条 `OLED frame`，屏上停在纯图案（无文字），便于拍照判定

### 17.5 资源

- FLASH 15076 → **15224 B（23.23%）**，RAM 3400 → **3408 B（16.64%）**
- 动态分配：无
- 烧录：`STM32_Programmer_CLI -c port=SWD mode=UR -w pm_f103_demo.hex -v -rst`，
  `Download verified successfully`，随后 `-rst` 释放探针

## 18. `OLED live on|off`：500 ms 动态刷新

### 18.1 实现

- 新增命令 `OLED live on` / `OLED live off`；bare `OLED live` 回 `OLED err=usage`
- `live on` 后每 `OLED_LIVE_PERIOD_MS = 500` ms 起一个分页作业，屏上四行：

| 页 | 内容 |
| --- | --- |
| 0 | `UP=<秒>S` |
| 1 | `K1=<n> K2=<n>` |
| 2 | `DIST=<cm>.<0.1cm>CM`，无有效测距时 `DIST=NONE` |
| 3 | `I2C af=<n> berr=<n>` |
| 4..7 | 空（整屏 8 页重写，避免残留） |

- 作业走现有 `oled_ch1116_start_text(0, 8)`，主循环每次只推 1 页（128 B ≈ 12 ms），
  命令处理里不死等；整屏 8 页 ≈ 98 ms
- 周期调度：`oled_live_tick()` 在主循环里；`s_oled_req != NULL` 时直接返回，
  **跳过该周期，不排队**；下一个周期从当前时刻重新计时（`next = now + 500`），
  不会因为忙而补发一串
- 每次刷新完成回一条 `OLED live ok ms=<实测> col=<n> af=.. berr=.. to=..`

### 18.2 与图案模式 / show 的互斥

- `frame` / `test` / `clear` 到达时先停 live（`s_oled_live = 0`）再起图案作业，
  完成回包带 `live=off`，例：`OLED frame ok ms=98 col=2 live=off af=0 berr=0 to=0`
- 如果命令正好落在 live 作业的中途，取"页边界中止"：`oled_ch1116_abort()` 清掉 job，
  丢掉的只是那次周期刷新的完成回包（它不是用户命令），新作业会重写全部 8 页。
  命令解析与 `oled_service()` 都在主循环里，同一时刻只有一个在跑，所以不会把某次
  I2C 传输切一半，也不会出现两个任务抢总线
- `live` 开启期间 `OLED show` 回 `OLED err=busy live`（不自动转 live）：
  live 已经显示同样的三项数据外加 I2C 计数，`show` 没有增量信息；
  直接拒绝是确定行为，也避免两条命令争同一个 `s_oled_req`
- `OLED col <n>` 在 live 期间照常可用：回 `OLED col=<n> ok redraw=live`，
  随后 `OLED redraw ok ...`，并把下个周期重新对齐

### 18.3 实测（COM11，115200，2026-11-18）

```
OLED live=on period=500ms
OLED live ok ms=98 col=2 af=0 berr=0 to=0
OLED err=busy live                  ← live 期间 show
OLED err=usage                      ← bare live
DIST cm=75.2 us=4361
OLED live ok ms=98 col=2 af=0 berr=0 to=0
OLED live ok ms=97 col=2 af=0 berr=0 to=0
...（10 s 窗口，每 1 s 发一次 DIST，共 22 次刷新）
OLED col=2 ok redraw=live
OLED redraw ok ms=98 col=2 af=0 berr=0 to=0
OLED live=off
OLED frame ok ms=98 col=2 live=off af=0 berr=0 to=0
```

- 刷新耗时：**22 次全部 97 ms 或 98 ms**（8 页 × 128 B @100 kHz），没有更长尾
- 10 s 内 `af=0 berr=0 to=0` 全程不变，**无累积上升**
- 距离随 `DIST` 变化：75.2 / 74.7 / 74.4 / 74.7 / 75.2 / 75.1 → 75.2 cm
  （手在探头前移动时读数跟着变，屏上在 ≤500 ms 内更新）
- 中途接管：`live on` 后 40 ms（live 作业进行到一半）发 `OLED frame` / `OLED clear`，
  没有出现 `busy pending`，直接回 `live=off` 并显示图案；随后 1.2 s 内没有任何
  live 刷新行
- `live off` 后 `OLED show` 正常：`OLED show ok ms=97 col=2 live=off af=0 berr=0 to=0`

### 18.4 注意

- live 只显示**最近一次** `DIST` 的结果，距离刷新仍要靠外部发 `DIST`；
  live 本身不触发测距（避免与命令通道抢同一个 HC-SR04 时序）
- 被接管中止的那次周期刷新没有完成回包，这是有意的：它不是命令

### 18.5 资源

- FLASH 15224 → **16232 B（24.77%）**，RAM 3408 → **3416 B（16.68%）**
- `HELP` 9 行 / **354 B**（OLED 行改为 `on/off/clear/test/show/frame/col/live`）
- 动态分配：无
- 烧录：`STM32_Programmer_CLI -c port=SWD mode=UR -w pm_f103_demo.hex -v -rst`，
  `Download verified successfully`；实测结束后屏上停在 live（`OLED live on`）

## 19. live 模式自带 1 Hz 测距

### 19.1 触发怎么挂进去

- `dist_start()` 增加一个参数：`dist_start(cmd_port_t *c, uint8_t auto_live)`。
  手动 `DIST` 传 0，live 传 1，**只有一份 HC-SR04 时序**（10 µs TRIG + EXTI 捕获 +
  TIM2 计宽 + 60 ms 超时），没有复制第二份
- 新函数 `dist_live_tick(now)`（主循环里 `dist_service()` 之后调用）：
  `s_oled_live != 0` 且满 1 s 才触发一次；上一发还在进行就跳过本周期
- 测量完成走原来的 `dist_service()`，结果写 `s_dist_last_cm10 / s_dist_last_ok`；
  下一次显示刷新（≤500 ms 后）自动把新距离排进 page 2，不需要额外代码路径
- live 模式下的测量回包带前缀：`DIST live cm=.. us=..` / `DIST live err=timeout`，
  与手动 `DIST cm=..` 一眼可分

### 19.2 与手动 DIST 的互斥

- `dist_start()` 里 `s_dist.active != 0` 时：
  - 自动触发（auto_live=1）→ 静默返回，跳过该周期
  - 手动 `DIST` → 若当前是从 live 发起的测量，回 `DIST err=busy live`；
    否则维持原来的 `DIST err=busy`
- 用户命令因此一定回包，且不会和自动测量并发同一个 HC-SR04 时序
- `live off` 后 `dist_live_tick()` 直接返回，不再自动触发；OLED 停止刷新，
  屏上保留最后一帧

### 19.3 周期选择（要求 5）

保留 **500 ms 显示 + 1 s 测量**，让测量起点比显示周期**偏移半个周期（250 ms）**：

- 显示作业占 98 ms（8 页），两次刷新之间的空窗约 400 ms；测量在 250 ms 处触发，
  10 µs 脉冲与 ≤23 ms 的回波窗口都落在空窗里，不会插在某一页传输中
- 两者各自计时（`s_oled_live_next` / `s_dist_live_next`），互不等待
- 主循环单次阻塞仍是"一页 I2C ≈ 12 ms"；触发本身只是 10 µs 忙等，没有新增阻塞点
- 没有选"把显示改成 1 s"：1 s 刷新对动态显示太慢，且 250 ms 偏移已经足够隔离

### 19.4 实测（COM11，115200，2026-11-18，静置 5 s，零命令）

| 时刻 ms | 回包 |
| --- | --- |
| 32 | `OLED live=on period=500ms` |
| 103 / 615 / 1114 / 1612 / 2110 / 2607 / 3104 / 3602 / 4101 / 4601 / 5113 | `OLED live ok ms=97..98 col=2 af=0 berr=0 to=0` |
| 259 / 1268 / 2264 / 3261 / 4272 | `DIST live cm=75.2 / 75.2 / 74.3 / 23.3 / 65.3 us=...` |

- 5 s 内没有发过任何命令，测距仍按 **≈1005 ms** 自主触发 5 次
- 显示 **≈500 ms** 一次，耗时 97–98 ms，与上一轮一致
- 全程 `af=0 berr=0 to=0`，无累积；`hb` 未中断

### 19.5 `busy live` 命中实测

用 Stopwatch 忙等把 `DIST` 精确压在 live 触发后 +251 / +252 ms（回波窗口内）：

```
--- DIST at +251 ms after live on ---
DIST err=busy live
DIST live cm=4.3 us=247
--- DIST at +252 ms after live on ---
DIST err=busy live
DIST live cm=4.3 us=247
```

同时确认 `live` 期间 `OLED show` 仍回 `OLED err=busy live`；
`live off` 后 2.2 s 内只有 `hb`，没有任何 OLED / DIST 行。

### 19.6 硬件注意（实测发现，不是本次缺陷）

- 在 +253..+256 ms 再发手动 `DIST` 时，回 `DIST err=timeout`：自动测量刚结束、
  模块仍在余振，HC-SR04 对 60 ms 内的第二次 ping 不返回有效回波
- live 的 1 Hz 间隔（1000 ms）远大于 60 ms，不受影响；只有"手动紧贴自动测量"才会遇到

### 19.7 资源

- FLASH 16232 → **16484 B（25.15%）**，RAM 3416 → **3424 B（16.72%）**
- `HELP` 9 行 / **356 B**（DIST 行改为 `one shot, cm and us; live=1Hz`）
- 动态分配：无
- 烧录：`STM32_Programmer_CLI -c port=SWD mode=UR -w pm_f103_demo.hex -v -r` +
  verify 通过 → `MCU Reset`；实测结束屏上停在 live

## 20. 静态分层检查接入 CI（检查器拷贝 + 角色清单 + 两处小步修正）

### 20.1 检查器来源

- 文件：`tools/check_layering.py`，拷自 `firmware-four-layer-architecture` skill（v3），
  **代码逐字未改**，只在文件头加了来源注释；
  文件头记录源路径、取用日期与 SHA-256（`2D3C638D…E14132A8`，为源文件原文校验值）
- **来源提交哈希：无**。该 skill 目录不是独立 git 仓库，工程根仓 HEAD
  `63ad2f6e10a8a946d31036e491282d160555ec4d` 也不跟踪它（根 `.gitignore` 第 12 行
  `2026-11-18/*/work/` 把它排除），没有哈希可引用，不编造

### 20.2 首次裸跑（还没有 `.arch-roles`）：76 ERROR / 459 WARN

- 76 条 ERROR **全部**来自 `src/app/main.c`：目录名 `src/app` 让它被判为 app 层，
  于是 1 条平台头泄漏（`stm32f1xx.h`）+ 75 条「越层访问寄存器」
- 459 条 WARN 都是未分层提示：`third_party/` 的 HAL/CMSIS、`src/core/`、`src/port/`、
  `platform/`、`include/pm/` 都还没登记角色（共 142 个未分层文件，其中 94 个含平台调用）

### 20.3 判断：`main.c` 是入口胶水，不是 app 层

**选择：如实承认 `src/app/main.c` 为 glue，不改代码把它拆成组件。**理由：

1. 它干的正是胶水的活：时钟、按键轮询、两路串口命令分发、I2C1 裸寄存器轮询、
   HC-SR04 的 EXTI+TIM2 计时、OLED 调度。这些**已经上板验证过**（§9–§19），不是草稿
2. 按检查器定义，app 层禁止碰 HAL/寄存器。要让 `main.c` 真算 app 层，就得把 I2C1 位操作、
   EXTI/TIM2 计时、OLED 调度整体下沉到 `src/port/stm32f1/`（bsp）或新组件——400+ 行搬迁，
   属于跨层重构，违反「一轮只动一层」，且搬完必须重新上板验证，本轮没有烧录授权
3. 反过来把 `main.c` 登记成 app 或 bsp 来消错是**错的**：前者要给每行贴 `// arch-allow`，
   后者等于把业务逻辑塞进"可碰一切"的角色，检查器就白接了
4. 因此 `.arch-roles` 里只有它一条 glue，其余按真实目录登记
   （`third_party/`、`platform/`、`src/port/` = bsp；`src/osal/` = osal；
   `src/core/`、`src/components/` = components；`include/pm/` = common）

代价：glue 不受寄存器/反向依赖检查约束，只剩行数上限。为防它继续膨胀，CI 用
`--glue-max-lines 1280` 作**棘轮**：当前 **1277 行代码**，只留 3 行余量，再长就红。
放宽必须改 workflow 那一行，会出现在 diff 里。

### 20.4 逐条处理剩下的 ERROR

| 首跑 ERROR | 处理 | 理由 |
| --- | --- | --- |
| `include/pm/pm_uart.h:61 反向依赖：common/ 调用上层函数 pm_uart_write` | **改代码**：把跨行原型改成单行 | 误报。检查器只把单行原型识别成声明（`DECL_ONLY_LINE`），跨行写法的后半截签名被当成调用。单行化零语义变化 |
| `platform/stm32f103/pm_platform.c:43 反向依赖 + BSP 越权调用 OSAL：pm_osal_init` | **改代码**：`pm_osal_init()` 从 `pm_platform_init()` 移到 `main()` 里紧跟其后 | 真越层。bsp 只提供硬件底座，1 ms 时基属入口层职责；移动后调用顺序不变，行为等价 |
| `platform/stm32f103/pm_platform.c:57 反向依赖：bsp/ 调用 pm_osal_tick_inc` | **行尾 `// arch-allow`** 并写明理由 | 检查器第 17 项明确允许 bsp 调 `*_tick_inc`，第 4 项反向依赖却没有同样的例外——两条规则打架。SysTick ISR 留在平台层结构正确，不改结构，只按检查器自己的豁免机制放行，把矛盾写进注释 |
| `src/app/main.c:1 入口胶水层 1276 行（上限 200）` | 见 20.3，走棘轮 | — |

处理后：**0 ERROR / 3 WARN**。

### 20.5 3 条 WARN（保留，不消除）

`src/port/stm32f1/pm_port_stm32f1.c:352/362/374`：端口层 ISR 调用
`pm_uart_hw_rx_byte` / `pm_uart_hw_tx_complete` / `pm_uart_hw_error`。
这是本仓 UART 的实际结构——ISR 在端口层，只把字节与事件递给 `src/core/pm_uart.c` 的钩子；
检查器第 4 项对 `*_hw_*` 钩子给 WARN 而非 ERROR，正是为这种情况留的口子。CI 只打印不判失败。

### 20.6 验证

- 检查器：`python tools/check_layering.py . --glue-max-lines 1280` → `合计 0 ERROR / 3 WARN`，退出码 0
- 构建：`cmake -S . -B build-ci --fresh -G Ninja -DCMAKE_TOOLCHAIN_FILE=<绝对路径>/cmake/arm-gcc-toolchain.cmake`
  + `cmake --build build-ci` → 成功（无新增编译警告）
- 尺寸：FLASH 16484 → **16480 B（−4 B，25.15%）**，RAM **3424 B 不变（16.72%）**。
  −4 B 来自 `pm_osal_init()` 调用点从 `pm_platform_init()` 挪到 `main()`（少一次调用层级的重定位），
  行为不变
- 主机单测：重跑，`96 checks, 0 failure(s)`（`tests/host` 只编译 `src/core`，`pm_uart.h` 的单行化被覆盖）
- **未上板验证**：本轮不烧录，`pm_osal_init()` 移动后的板端行为没有复测；
  按结构等价推断为无变化，但不写成"已验证"

## 21. 检查器升级到上游 af88491（3.2.0）+ 锁文件 + CI 两步校验

### 21.1 取件方式与来源

- 上游仓：`2026-11-18/repo-hardening/work/firmware-four-layer-architecture`（本次它已是 git 仓库）
- 用 `git worktree add --detach %TEMP%\wt-checker-af88491 af88491` 取**干净内容**，
  没有拷正在被编辑的工作副本；源文件 blob `0c16afe694f2b3b1b5cbd211d1d055db9cb1af9d`
- 覆盖 `tools/check_layering.py`，**代码逐字未改**；只按上游 `docs/integration.md` §2.1 的规定
  把文件头 `SOURCE_COMMIT` 从 `"self"` 改成 `"af884914c4cdc23472a20382a32df4a9e8305ce5"`
- 旧版文件头里"源目录无 git、无提交哈希"的说法已随本次升级删除；身份改由 `tools/CHECKER.lock` 记录

**与任务描述的一处差异（如实记录）**：本地上游仓的 `main` 实际是 `51d8b09`，不是 `af88491`。
`git diff --stat af88491 51d8b09` 显示该提交只改了上游自己的 `CMakeLists.txt` 与 `README.md`，
**没有改动 `check_layering.py`**，所以按任务指定的 `af88491` 同步，脚本内容与 `main` 相同。
锁文件写 `af88491`，与拷来的内容严格对应；将来上游再动脚本时 `--lock` 会报警。

### 21.2 锁文件

`tools/CHECKER.lock`：

```
checker-version = 3.2.0
source-repo     = https://github.com/BiscuiTs2026/firmware-four-layer-architecture
source-commit   = af884914c4cdc23472a20382a32df4a9e8305ce5
source-blob     = 0c16afe694f2b3b1b5cbd211d1d055db9cb1af9d
synced-at       = 2026-11-18
local-patches   = none（只按上游规定改了 SOURCE_COMMIT 一行）
```

- 前三项由检查器 `--version --lock` 自行比对；`source-blob` / `synced-at` / `local-patches`
  检查器不读，供人工核对（脚本的 `parse_lock` 会跳过未知键）
- 正例：`python tools/check_layering.py --version --lock tools/CHECKER.lock` → 打印三项 + `[LOCK] 一致`，退出码 0
- 负例：拿一份写成 `3.1.0` / 全零提交 / `source-repo = x` 的锁文件，三条全部报不一致，**退出码 1**
  → CI 第一步是真门禁，不是摆设

### 21.3 CI 两步

`.github/workflows/layering.yml`：

1. `python3 tools/check_layering.py --version --lock tools/CHECKER.lock`（身份/版本漂移）
2. `python3 tools/check_layering.py . --glue-max-lines 1280`（扫描，ERROR 必须 0）

两步任一步非零即失败；WARN 只打印。

### 21.4 角色清单：用 ignore 取代 third_party 的 bsp 登记

```diff
-third_party/*        = bsp
+ignore = third_party/
+ignore = build-*/
```

- 只忽略这两类，**没有扩大范围**（`platform/`、`src/port/`、`src/core/`、`src/components/`、
  `include/pm/`、`src/app` 照旧全扫）
- 代价：厂商宏不再进宏索引，第 14 项"上层使用下层硬件宏"对纯 HAL 宏会漏；
  本仓 components 层目前没有任何 HAL 宏用法，因此本轮判定不变
- 顺带解决本地跑时 `build-*/CMakeFiles/.../CMakeCCompilerId.c` 的未分层 NOTE

### 21.5 复测：判定没变

| 组合 | ERROR | WARN | 耗时（同机、同树） |
| --- | --- | --- | --- |
| 3.0（旧）无 ignore | 0 | 3 | 24.1 s |
| 3.2.0 无 ignore | 0 | 3 | 22.8 s |
| 3.2.0 + ignore | 0 | 3 | **0.19 s** |

- 逐条对比旧/新的 ERROR+WARN 行：`Compare-Object` 结果 `IDENTICAL`（旧 3 条、新 3 条，内容一致）
- 3 条 WARN 仍是 `pm_port_stm32f1.c:352/362/374` 调 `pm_uart_hw_*` 钩子，未变
- `--glue-max-lines 1280` 棘轮行为一致：3.2.0 对 glue 仍只查行数（`check_glue_length` 未改），
  `main.c` 仍 1277 行，上限 1280 通过；实测把上限压到 1276 →
  `[ERROR] src/app/main.c:1 入口胶水层 1277 行代码（上限 1276）`，退出码 1，棘轮确实咬得住
- 未启用 `layers` 指令：它只影响"没有 components 层时 APP 可直调 BSP"的放行，
  本仓有真实 components 层，声明与否判定相同
- 本轮**没有改任何源码**，因此没有重新构建，固件尺寸仍是 16480 B / 3424 B

## 22. 2026-11-19 上板复测与器件清点

背景：`pm_osal_init()` 从平台层移到 `main()`（§21 的附带改动）之后，代码没有在硬件上跑过；
本节补上板验证，并把“哪些器件真的在线”一次说清。原始串口日志见 `evidence/retest-20261119*.log`
（登记在 `evidence/INDEX.md` 第 10、11 项）。

### 22.1 复测结果（已实测）

- 构建 `build-retest`：FLASH **16480 B**、RAM **3424 B**，与 README 一致；
  `STM32_Programmer_CLI -w ... -v -rst` → `Download verified successfully` + `Software reset`
- tick / 时基：`STAT up=72369` → `82794`，Δup=**10425 ms**，墙钟 10433 ms（+8 ms/10.4 s）；
  `tick=1`，`hb` 每秒一行不中断 → `pm_osal_init()` 移动后时基正常
- 命令通道：`PING`→`PONG`；`HELP` 9 行 356 B；`I2C found=0x70,0x7A`；
  `AHT ok rh=46.6 t=20.1 raw=18 77 61 55 9C 6E rst=0`；`AHTS` 回上一条 + 忙位；
  `DIST cm=99.4 us=5767`；背靠背两条 `DIST` → `DIST err=busy` + 一条有效值
- OLED：`OLED live ok ms=97..98` 每 500 ms 一次、1 Hz `DIST live`；live 期间 `OLED show`
  → `OLED err=busy live`；`OLED off`/`test`/`clear` 均有 `ok` 回包
- 未烧板端行为回归：本轮**没有**改源码，只做了构建、烧录与命令复测

### 22.2 `DIST err=timeout`：现场见到过，但不是“必然”行为

- **首轮现场（未落盘，控制台观测）**：`OLED live on` 之后，同步到每条 `DIST live` 立刻手动 `DIST`，
  12 轮全部返回 `DIST err=timeout`，当时 live 测距值约 99 cm
- **复抓两次（已归档）**：同样方法 12 轮（`retest-20261119.log`）与 24 轮
  （`retest-20261119-sweep.log`），**36/36 全部拿到有效回波**，0 timeout、0 busy
- 结论：`timeout` 路径在硬件上**确实可达**（已观测），但它不是“紧随 live 就必然超时”的确定性行为；
  更可能的条件是**当时没有有效回波**（ECHO 保持高约 38 ms 或根本没有上升沿），
  而不是单纯的时间窗口。目标回波强弱、距离变化（本轮同一会话里出现过 99 cm 与 39.8 cm 两种读数）
  都会影响结果
- 待办：`docs/PROTOCOL.md` 第 5 节把“手动 DIST 在自动测距结束后 60 ms 内会回 timeout”写成必然现象，
  与本次 36 次复抓不符，应按“条件性现象”改写；**本轮没有改 PROTOCOL.md**，等确认
- 尚未构造的确定性实验（需要动手）：把 HC-SR04 指向空旷方向、或用纸板完全遮住探头，
  再做单次 `DIST`，预期 `DIST err=timeout`——只有这一种构造能把 timeout 固化进归档日志

### 22.3 器件清点（对照 `HARDWARE.md` §3）

| 类别 | 器件 | 证据 |
| --- | --- | --- |
| 确认在线 | CH343 USB 串口（USART2，PA2/PA3） | COM11 115200 双向收发，`hb` 每秒一行 |
| 确认在线 | AHT20 0x70（PB6/PB7） | `AHT ok rh=46.6 t=20.1`，两次读数不同 |
| 确认在线 | CH1116 OLED 0x7A（PB6/PB7） | `I2C found` 含 0x7A；`OLED test/clear/show/live` 均 `ok`，`ms=97..98` |
| 确认在线 | HC-SR04（TRIG PA11 / ECHO PA10） | `DIST cm=99.4 us=5767`、`cm=39.8 us=2310`，随前方物体变化 |
| 未验证 | 蓝牙模块（USART3，PB10/PB11） | 见 22.4，本轮没有蓝牙侧数据 |
| 无法探测 | 舵机 PB8、循迹 PB14、电机 PA0/PA1、编码器 PA8/PA9/PB15、NTC PA4、电位器 PA5、蜂鸣器 PB9、继电器 PB5、LED PA6/PA7/PB0 | 当前固件没有对应命令，**不写成“未检测到”** |
| 不在清单里 | 无 | I2C 扫描只有 0x70、0x7A，没有出现清单外地址 |

按键 PB12/PB13 有 `KEYS` 计数通道，但需要人按下去才有增量；本轮开始时 `k1=0 k2=0`，
说明复位后没有人按过，不能据此判断按键好坏。

### 22.4 两条必须写死的“别误读”结论

1. **蓝牙通道本轮未验证，但不是链路故障**。该模块一直走**网页串口助手（Web Bluetooth）**，
   不走 PC 的 SPP 端口；本轮试开的 COM5/6/7/8/9/10（系统里的“蓝牙链接上的标准串行”）
   打开或写入超时，**只说明 PC 侧没有已连接的 SPP 通道**，与模块、USART3、固件都无关。
   复验方法：用户重连 Web Bluetooth 页面后，从 USART2 侧抓固件的 `BT rx` / `BT tx` 镜像
   （即以往的 `bt-*.log` 方式）对本轮收发对账。**在拿到这份日志之前，蓝牙只能写“未验证”。**
2. **ST-Link 热连接读到的 GPIO 电平是复位默认值，不代表运行态**。
   `STM32_Programmer_CLI -c port=SWD mode=normal -r32` 连接时目标已被复位，读到
   `GPIOA_CRL=0x44444444`、`GPIOA_CRH=0x88844444`、`GPIOA_IDR=0x00000000`、
   `GPIOA_ODR=0x0000A000`、`GPIOB_CRL=0x44484444`、`GPIOB_IDR=0x00000000`、`GPIOB_ODR=0x00000010`
   ——全是复位默认值 + JTAG 脚配置，固件的输出/上拉设置一条都没体现。
   所以这批数值**不能**用来判断“LED 没亮”“按键没接”之类的结论；要读运行态必须 halt 后
   在固件初始化完成的状态下读 IDR/ODR，本轮没有可用 GDB server，未做。
   读取后已 `-rst`，固件恢复（`STAT up=6263` 重新计数）。

### 22.5 本轮未做

- 没有改任何源码，因此没有重新构建以外的验证；固件尺寸与 README 一致
- 没有构造“无回波”条件，归档日志中没有真实的 `DIST err=timeout`
- 没有运行态 GPIO 快照、没有蓝牙侧抓包、没有 `KEYS` 按键对账（等用户按键后补）
- 没有修改 `docs/PROTOCOL.md`（发现的口径问题记在 22.2，等确认后再改）

## 23. 2026-11-19 按键对账、屏不亮定位与 timeout 再复抓

原始日志：`evidence/retest-20261119-key-oled.log`（90 行，19:03:35 起 15.7 s，
登记在 `evidence/INDEX.md` 第 12 项）。本轮**没有改源码**，只发命令抓包。

### 23.1 按键对账：计数不是 1/1，原因可定位

- 原始回包：`STAT clk=72000000 up=453219 tick=1 key1=9 key2=7 uart_rx=372 uart_tx=45726`；
  `KEYS k1=9 k2=7 last=k1`
- `up=453219 ms`（约 7.5 min）说明固件自 18:5x 那轮 sweep 之后**没有复位**，
  而 §22 那轮全程没发过 `KEYS`，所以 9/7 是**复位以来从未清零的累计值**，
  包含用户这轮按键之前的所有按动（§22 记录过当时 `k1=0 k2=0`，此后累计 16 条 press 边沿）
- `last=k1`：最后一条 press 边沿是 KEY1，与“两个键都按过”不矛盾
- 结论：这份数据**只能**证明按键边沿被正常计数、最后一次是 KEY1；**不能**证明
  “一次按键只计一次”。要得到干净的 1/1，必须先用 `KEYS` 清零、再请人各按一次、再读一次
- 不写成“消抖失效”：20 ms 消抖在 §7 已用 6 组事件验证过；9/7 与“之前按过多次”同样吻合，
  两种解释目前无法用这份日志区分

### 23.2 屏不亮：软件侧已排除，原因是上一轮收尾的 `OLED off`

- 两份归档日志（§22 的 retest 与 sweep）**最后一条动作都是 `OLED off`**，
  回包 `OLED off ok af=0 berr=0 to=0`；此后 live 刷新仍在继续（日志尾部还有 `OLED live ok`），
  即“显示关、数据照刷”，屏自然是黑的
- 更正一条早先的说法：§22 复测期间屏上确实停在静态页，但那是在 `OLED off` 之前；
  以两份日志的结尾状态为准，本轮开始时**屏本来就应该是不亮的**，不是新故障
- 本轮实际发送与回包原文：
  - `OLED live off` → `OLED live=off`（先停 live，否则 `show` 会被判忙）
  - `OLED on` → `OLED on ok af=0 berr=0 to=0`（显示开，I2C 帧 ACK）
  - `OLED show` → `OLED show ok ms=97 col=2 live=off af=0 berr=0 to=0`（整页 97 ms 画完）
  - `I2C` → `I2C found=0x70,0x7A`（0x7A 仍在总线上）
- 软件侧排除到这一步：0x7A ACK、`af`/`berr`/`to` 全 0、页面刷新 97 ms 正常完成、
  固件自 18:5x 未复位。**若屏仍不亮，问题在硬件侧**（供电、排线、背光/对比度），
  需要人检查接插件；本轮不猜型号、不动接线
- 抓包结束时屏处于“显示开 + 最后一页 live 内容”状态（`OLED live off` 只停刷新，不关显示）

### 23.3 `DIST err=timeout`：累计 44 次手动测距仍未复现，但看到了“读数变短”

- 本轮 8 轮“收到 `DIST live` 回包后立刻手动 `DIST`”，**8/8 有效回波，0 timeout、0 busy**；
  与 §22 的 36 次合计 **44/44 未复现**
- 新现象：多轮手动 `DIST` 读到 **61–79 cm**，而同秒的 `DIST live` 是 **96–99 cm**。
  同一目标、同一秒内相差这么多，不像目标移动；更符合“上一次回波还没结束就发下一次触发，
  宽度从错误的边沿起算”，与 §22.2 的“余振”机制一致——同一机制在这批样本里表现为
  **读数被截短**，而不是超时
- 归档里仍然**没有一条真实的 `DIST err=timeout`**。想把它固化进证据，只能构造“无回波”：
  把探头指向空旷方向或用硬纸板完全贴住探头，再做单次 `DIST`。本轮未做，等用户配合

### 23.4 本轮未做

- 没有清零后重新按键，所以没有干净的 `k1=1 k2=1` 证据
- 没有物理确认屏是否点亮（只能由人看；日志只覆盖 I2C 层）
- 没有构造“无回波”，归档中仍无真实 timeout
- 没有改 `docs/PROTOCOL.md` 第 5 节的口径（同 §22.2 待办）

## 已知遗留

- 链接时出现 `_write/_read/_close/_lseek not implemented` 警告，来自 nano.specs + nosys.specs，
  当前未使用 printf，无影响。
- 判读规则：BLE 侧一个长回包可能显示成多个气泡，**只看总字节数，不看气泡个数**。
  `HELP` 完整 = **356 B（9 行，COM11 实测）**。
- `DIST` 的 timeout 分支（无回波、>4 m）只做了代码路径，**没有制造出真实现场验证**。
- 异步命令（`AHT`/`AHTS`/`DIST`）的回包可能晚于其后一条命令的回包（最多晚 60~80 ms）。
- OLED：已验证到"初始化 + 8 页 GDDRAM 写入全部 ACK、AF/BERR 为 0"，
  **屏上画面是否正常需要肉眼确认**；CH1116 的 I²C 模式不能读回状态/GDDRAM，
  型号无法用读寄存器再交叉验证一次。
