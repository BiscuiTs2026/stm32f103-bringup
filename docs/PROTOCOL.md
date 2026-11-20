# 命令协议（board-bringup 固件）

面向以后接手的人或另一个 AI。本文描述的是**实际烧在板上的行为**（2026-11-18 版本），
与 `PATCHES.md` 里的上板实测一致。改协议时必须同步改这份文件。

实现位置：`src/app/main.c` 的 `cmd_feed()` / `cmd_execute()` / `cmd_flush_idle()`；
`PWM` 分支在 `cmd_execute()` 末尾交给 `src/components/pwm_dev/pwm_dev.c` 的
`pwm_dev_cmd()`，胶水层只做一次登记与转发。

---

## 1. 传输与链路

| 链路 | 外设 | 引脚 | 参数 | 角色 |
| --- | --- | --- | --- | --- |
| USB 串口（调试） | USART2 | PA2=TX / PA3=RX（CH343P） | 115200 8N1 | 主调试口，`hb` 每秒一行 |
| 蓝牙透传 | USART3 | PB10=TX / PB11=RX（DX-BT24） | 9600 8N1 | 手机/网页侧命令口 |

- 两路**共用同一个解析器**，命令集完全相同，**两路等价**：从哪路进来，回包就从哪路回去。
- 蓝牙侧页面必须发 **ASCII**，行尾随意（`\r`、`\n`、`\r\n` 都行，也可以不带）。
- 只接受 `0x20..0x7E` 的可打印字符；其它字节被静默丢弃。
- 单行最长 **47 字符**（`CMD_LINE_MAX = 48`，含结尾 `\0`），超出部分被丢弃且不报错。

**注意镜像**：USART3 的每一条回包，固件会额外在 USART2 打一行

```
BT tx n=<字节数>B: <hex...>
```

这是给调试口看的单向镜像，不是一条命令的回包，判读抓包时不要把它当成 `ERR`/`OK`。

上电 banner（仅 USART2，约 300 ms 内输出，115200）：

```
board-bringup
core clock = 72000000 Hz
USART2 115200 8N1, command channel ready
BT USART3 9600 8N1, command channel ready
OLED CH1116 addr=7A init ok, col=2, clearing
OLED ready (128x64, 6x8 font)
```

---

## 2. 分帧规则

1. **带行尾**：收到 `\r` 或 `\n` 立即执行（空行不执行、不回包）。
2. **不带行尾**：该端口最后收到一个字节后**空闲 100 ms**，自动把缓冲区内容当一条命令执行。
   这样网页端忘记加行尾也能用。
3. 空闲计时**每收到一个字节就重置**；带行尾的命令不会等这 100 ms。

### 100 ms 的含义与取舍

9600 波特下 1 字节 ≈ 1.04 ms，**100 ms ≈ 96 字节**。也就是说：

- 一条命令的两个片段如果间隔 **< 100 ms**，会被当成**同一条命令的延续**拼在一起。
- 如果页面把 `AHT` 和后面的 `STAT` 分成两个 BLE 包、间隔小于 100 ms 发出，
  固件会看到 `AHTSTAT`，回 `ERR unknown=AHTSTAT`。
- 反过来，一条没有行尾的命令，最坏要等 100 ms 才执行（蓝牙页面常见现象）。

为什么是 100 ms 而不是更长/更短：

- **不能更短**：DX-BT24 按 BLE 包转发，一个短命令可能被拆成几包，包间隔几十毫秒；
  阈值太短会把一条命令劈成两条，产生 `ERR unknown=...` 和残留半行。
- **不能更长**：无行尾命令的响应延迟直接等于该阈值，再长人机交互会觉得"没反应"；
  100 ms 是"能容忍 BLE 分包"和"人还感觉不到卡"之间的折中。
- 这是**协议级限制**，不是可以靠加缓冲解决的 bug：没有行尾就无法区分"分包"与"两条命令"。

---

## 3. 响应约定

- 一行一条回包，以 `\r\n` 结尾；字段用空格分隔，形式为 `KEY=value`。
- 成功回包以命令名开头（`AHT ok ...`、`OLED frame ok ...`），失败回包以
  `AHT err=<原因>` / `OLED err=<原因>` 的形式给出原因，**原因永远是单词**。
- **任何命令都必须有回包**。未知命令回：

  ```
  ERR unknown=<原始行内容>
  ```

- `busy` 类错误表示"这条命令现在不能执行"，不是硬件坏：

  | 回包 | 含义 | 什么时候出现 |
  | --- | --- | --- |
  | `AHT err=busy pending` | 上一次 AHT 测量还没结束 | 80 ms 窗口内重复发 `AHT` |
  | `AHT err=busy oled` | 一次 OLED 整屏推送正在进行 | 98 ms 窗口内发 `AHT`（AHT 触发会重配 I2C1，插进去会把屏写坏） |
  | `AHT err=busy` | AHT20 自己没测完（状态字节 bit7=1），已重试 3 次 | 上电初期、总线异常 |
  | `DIST err=busy` | 上一次手动测距还在进行 | 60 ms 内重复发 `DIST` |
  | `DIST err=busy live` | live 模式自动测距正在进行 | live 开启时手动 `DIST` 撞上 |
  | `DIST err=disabled` | 测距已停用，没有 µs 计时源 | 每次都回（TIM2 已划给电机 PWM） |
  | `OLED err=busy pending` | 上一次 OLED 分页作业还没推完 | 98 ms 窗口内连发 OLED 命令 |
  | `OLED err=busy i2c` | AHT 正在占用 I2C1 | AHT 的 80 ms 窗口内发 OLED |
  | `OLED err=io` | I2C 写失败（AF/BERR/超时） | 见回包里的 `af/berr/to` 计数 |

- 异步命令（`AHT` / `AHTS` / `DIST` / `OLED ...`）的回包会**晚于**命令到达时间，
  可能晚于其后一条命令的回包（最多 60~98 ms）。按前缀匹配，不要按顺序配对。

---

## 4. 命令表

大小写不敏感。下表"改变状态"指是否影响设备行为或保存的数据，不只是回包。

### 4.1 通用

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `HELP` | 无 | 15 行 `HELP ...`，共 **638 B**（见下） | — | 否 |
| `PING` | 无 | `PONG` | — | 否 |
| `STAT` | 无 | `STAT clk=72000000 up=<ms> tick=1 key1=<n> key2=<n> uart_rx=<n> uart_tx=<n>` | — | 否 |
| `I2C` | 无 | `I2C found=0x70,0x7A`（按升序列出 8 位写地址） | `I2C found=none` | 否（只扫描） |
| `KEYS` | 无 | `KEYS k1=<n> k2=<n> last=none\|k1\|k2` | — | **是**：读出后清零计数 |

`HELP` 全文（顺序固定）：

```
HELP HELP=list commands
HELP PING=reply PONG
HELP STAT=clk/up/tick/keys/uart counters
HELP I2C=scan I2C1 bus, list 8-bit addresses
HELP AHT=read AHT20, rh and t in 0.1 units
HELP AHTS=same but read via repeated START
HELP DIST=disabled, TIM2 given to motor
HELP ADC=pot PA5 and NTC PA4, raw and mV
HELP OLED=on/off/clear/test/show/frame/col/live/dump
HELP PWM=servo 0..180 deg, buzzer 100..5000 Hz, sweep
HELP TRACK=raw level of PB14 line sensor
HELP ENC=count/clear, dir, a/b, button PB15
HELP RELAY=on/off/stat, PB5 relay coil
HELP MOTOR=fwd/rev/stop/duty/stat, PA0/PA1 TIM2 PWM
HELP KEYS=key press counters, clears them
```

`up=` 是开机毫秒数；`key1/key2` 是消抖后的按下次数；`uart_rx/uart_tx` 是两路累计字节数。

> 实现位置（S10 起）：`HELP`/`PING`/`STAT`/`I2C` 在 `src/components/sys_cmds/`，
> 命令表与适配器在 `src/app/app_wiring.c`。命令、回包文本与顺序均未变。

### 4.2 AHT20 温湿度（I2C1，0x70）

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `AHT` | 无 | `AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0` | `AHT err=busy pending` / `AHT err=busy oled` / `AHT err=busy raw=...` / `AHT err=io raw=...` | 触发一次测量 |
| `AHTS` | 无 | 同上，`rst=1` | 同上 | 触发一次测量（repeated START 路径，仅作对照） |

- `rh`/`t` 单位是 0.1（`rh=40.8` 即 40.8 %RH，`t=20.4` 即 20.4 °C）。
- `raw=` 是读回的 6 个原始字节，十六进制、空格分隔，失败时也照打。
- `rst=0` 表示"STOP 结束触发 + 重新 START 读"，`rst=1` 表示触发后不发 STOP 直接 repeated START 读。
- `AHTS` 读到的是**上一次**结果（`d[0]` bit7=1 即忙），这是 AHT20 的语义，不是驱动 bug。

### 4.3 HC-SR04 测距（PA11=TRIG / PA10=ECHO）——已停用

> **2026-11-19 起停用**：测距的 1 µs 时间戳源是 TIM2，而 TIM2 已整颗分配给电机
> PWM（PA0/PA1），两者共用 `PSC/ARR` 无法共存。`DIST` 现在固定回
> `DIST err=disabled`；引脚配置与边沿逻辑保留，重新启用前必须先确定独立计时源。
> 详见 `docs/TIM2-CONFLICT.md`。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `DIST` | 无 | 停用期间不会出现 | `DIST err=disabled`（当前固定）/ `DIST err=timeout` / `DIST err=busy` / `DIST err=busy live`（重新启用后才可能出现） | 触发一次测距 |

- `cm` 保留 1 位小数（内部按 0.1 cm 整数运算），`us` 是回波宽度（1 MHz 计时）。
- `timeout` 覆盖两种情况：60 ms 内没有回波（超过约 4 m 或没有反射面），
  以及回波宽度不在 100..25000 µs 的合理区间。
- live 模式自动触发的测量回包带 `live` 前缀：`DIST live cm=.. us=..` /
  `DIST live err=timeout`，与手动命令区分。

### 4.4 OLED（CH1116，0x7A，128×64，I2C1）

**默认行为：复位/上电后自动进入 live（500 ms 刷新），不需要发任何命令。**
`live` 只在 `OLED off`、`OLED live off` 或 live 作业出错时停止。静态图案命令
（`clear`/`test`/`frame`/`show`）在 live 期间照常执行并临时接管屏幕，
画完后保持 **3 s** 再自动交回 live，回包里用 `hold=3000ms` 告知停留时间。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `OLED on` | 无 | `OLED on ok display=on live=on af=0 berr=0 to=0` | `OLED err=io ...` / `OLED err=busy pending` | **是**：显示开并立即恢复 live |
| `OLED off` | 无 | `OLED off ok display=off live=paused af=0 berr=0 to=0` | 同上 | **是**：显示关，暂停 live 刷新与 1 Hz 自动测距（显存保留） |
| `OLED clear` | 无 | `OLED clear ok ms=98 col=2 live=on hold=3000ms af=0 berr=0 to=0` | `OLED err=io ...` / `err=busy pending` | **是**：整屏黑，3 s 后回 live |
| `OLED test` | 无 | `OLED test ok ms=98 col=2 live=on hold=3000ms ...` | 同上 | **是**：整屏棋盘格，3 s 后回 live |
| `OLED frame` | 无 | `OLED frame ok ms=98 col=2 live=on hold=3000ms ...` | 同上 | **是**：边框 + 9 条竖线，3 s 后回 live |
| `OLED show` | 无 | `OLED show ok ms=98 col=2 live=on hold=3000ms ...` | `OLED err=busy pending` / `err=busy i2c` | **是**：把 live 页内容画成静态帧，3 s 后回 live |
| `OLED col <0..4>` | 列偏移 | `OLED col=2 ok redraw=<图案名>` | `OLED err=usage col=0..4` | **是**：改列偏移，整块 RAM 清黑后立即重画（见下方 col 说明） |
| `OLED live on` | 无 | `OLED live=on period=500ms` | `OLED err=busy pending` / `err=busy i2c` | **是**：进入周期刷新 + 1 Hz 自动测距 |
| `OLED live off` | 无 | `OLED live=off` | — | **是**：停住一帧静态画面，屏保留最后一帧 |
| `OLED dump` | 无 | `OLED dump mode=<live\|static\|off> [left=<s>s] screen=<页名>` + 8 行 `OLED L<n>=<21 字符>` | — | 否（只读显存文本，不发 I2C） |
| `OLED live` | 缺参数 | — | `OLED err=usage` | 否 |
| 其它子命令 | — | — | `OLED err=usage` | 否 |

说明：

- `display=` / `live=paused` / `hold=` 是后加的状态字段，只出现在对应回包里；
  原有 `ms=` / `col=` / `af=` / `berr=` / `to=` 字段与顺序不变。
- 静态命令在 `live off`（或 `OLED off`）状态下执行时，回包为 `live=off` 且**不带**
  `hold=`——此时没有 live 可交回，画面就一直停在该图案。
- `ms=` 是整屏 8 页的实际传输耗时（约 97–98 ms），`col=` 是当前列偏移，
  `af/berr/to=` 是 I2C 错误累计计数（从开机起，不清零）。
- `frame` 布局：page 0/7 全亮（上下 8 行实心带），page 1..6 在
  x = 0,16,32,48,64,80,96,112,127 处各一条竖线（共 9 条）。
- live 页固定 **8 行 × 21 列**（6x8 字库），字段宽度固定、数值变化不跳位：

  ```
  MODE=LIVE      UP=123S
  NOW : DIST MEAS
  LAST: DIST ok 99.0
  RH=47.3 T=19.8C
  NTC=1792 POT=2048
  K1=    0 K2=    0
  I2C af= 0 berr= 0
  LIVE ms= 98
  ```

  - `MODE`：`LIVE` / `STAT 3S`（静态页接管中，数字是剩余 hold 秒）/ `OFF`。
  - `NOW`：此刻真正在跑的后台作业——`IDLE`、`DIST MEAS`、`AHT READ`、
    `OLED PUSH`；来自运行状态，不是写死的。
    `DIST`/`AHT` 启动时只重画这一行（单页 ≈12 ms），所以 20/80 ms 的短作业
    也能在屏上看到；这条单页写只发生在命令路径，不占 500 ms 刷新节拍。
  - `LAST`：最近一条副作用的命令与结果摘要（最多 15 字符，超长截断），
    失败时是错误原因，如 `LAST: AHT err=busy`。
  - `RH`/`T` 来自后台 AHT：**每 2 s 触发一次**，只取最近一次合格结果；
    还没读到过显示 `--.-`，之后读失败保留上一次的值，不会显示 0、也不会
    卡住刷新节拍。
  - `NTC`/`POT` 每帧现读（两次单次转换，成本低），读失败显示 `----`。
  - `LIVE ms=` 是最近一次整屏推送耗时，反映 I2C 健康度；还没推过显示 `---`。
- `live` 期间每 500 ms 刷新一次显示，每 1000 ms 自动发一次 `DIST`；
  测量起点比显示周期偏移 250 ms，两者互不阻塞。
- 静态图案的 3 s 停留期间（以及 `OLED off` 期间）两者都暂停，恢复 live 后
  立刻重画一帧（实测 `OLED on` 后 98 ms 内出现首条 `OLED live ok`）。
- `OLED col` 在 live 期间可用，回 `redraw=live` 并立即重画。
- `OLED col` 的时序（2026-11-20 起）：命令先按 offset 0 与 offset 4 各推 8 页全黑，
  覆盖 RAM 全部 132 列，再重画当前画面。因此该命令的响应从约 15 ms 变为约 **205 ms**
  （16 页 × 约 12 ms），这是预期值，不是异常；`ms=` 仍只统计重画那一遍的 8 页。

### 4.5 ADC（PA5 电位器 / PA4 NTC）

ADC1，单次软件触发，ADCCLK = PCLK2/6 = 12 MHz，采样时间 55.5 周期。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `ADC` | 无 | `ADC pot=<0..4095> pot_mv=<mV> ntc=<0..4095> ntc_mv=<mV> vref=3300` | `ADC err=init` / `ADC err=timeout ch=pa5` / `ADC err=timeout ch=pa4` | 否 |

说明：

- `pot` = PA5 = ADC1_IN5，`ntc` = PA4 = ADC1_IN4；两次转换是先后单次触发，不是扫描，
  两路之间相差约几十 µs。
- `*_mv` = `(raw * 3300 + 2047) / 4095`（四舍五入），与外设模型仓 `pm_adc_to_mv()` 相同。
- 失败路径：ADC1 初始化或校准失败回 `err=init`；某一路 `HAL_ADC_PollForConversion`
  5 ms 超时回 `err=timeout ch=pa5|pa4`，不会静默返回旧值。
- **NTC 是分压低端（接 GND），10 kΩ 在上端接 3.3 V**：升温 → 阻值下降 → `ntc` 下降。
  本板实测 25 ℃ 附近 `ntc≈2293`（1848 mV），手指捏住后 90 s 内降到 1792（Δ≈−491）。
- 电位器是本轮唯一没有转动验证的通道：手边没有十字螺丝刀，只记录了静态值
  （30 次 2124–2126，极差 2 LSB）。

### 4.6 PWM（PB8 舵机 / PB9 无源蜂鸣器）

PB8 = TIM4_CH3，固定 **50 Hz** 帧（1 µs 刻度，ARR=19999）；PB9 不是定时器通道，
由 TIM3 更新中断翻转 GPIO 生成，10 µs 相位分辨率。两者同属 TIM4 的引脚冲突
已按"TIM4 只给舵机、蜂鸣器走 TIM3 中断"解决，见 `PATCHES.md` §30.1。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `PWM stat` | 无 | `PWM stat servo=<deg>c pulse=<us>us buz=off` 或 `... buz=<Hz>Hz/<duty>%` | `PWM err=init` | 否 |
| `PWM servo <0..180>` | 整数角度 | `PWM servo ok deg=90.0 pulse=1500us` | `PWM err=range deg=<n>`（>180）/ `PWM err=usage`（缺参数、负数、非数字）/ `PWM err=init` | **是**：CCR3 改脉宽 |
| `PWM buz <100..5000> [duty]` | 频率 Hz，占空比 1..99（默认 20） | `PWM buz ok freq=1000Hz duty=20%` | `PWM err=range hz=<n>` / `PWM err=range duty`（>99）/ `PWM err=range`（duty=0）/ `PWM err=usage` / `PWM err=init` | **是**：开蜂鸣器并改频率 |
| `PWM buz off` | 无 | `PWM buz off ok` | `PWM err=init` | **是**：停音，PB9 拉低 |
| `PWM sweep` | 无 | 20 行：`PWM sweep deg=<deg> pulse=<us>us` ×18 + 终档 90° + `PWM sweep done` | `PWM err=range` / `PWM err=init` | **是**：0→180→0 两轮，每档 300 ms，结束回 90° |
| `PWM dbg` | 无 | 1 行：`PWM dbg apb1enr=… apb2enr=… cr1=… psc=… arr=… ccmr2=… ccer=… ccr3=… cnt0=<n> cnt1=<n> dt=20ms crh=… mapr=… fpwm=<Hz>` | `PWM err=init` | 否（只读，中间 `delay_ms(20)`） |
| `PWM test` | 无 | `PWM test done 1Hz 10s pb8=af servo=<deg> pulse=<us>us` | — | **是**：PB8 切普通 GPIO 推 1 Hz 方波 10 s，结束后恢复 TIM4 复用并回 90° |
| 其它子命令 | — | — | `PWM err=usage` | 否 |

说明：

- 舵机脉宽映射 `pulse = 500 + deg*2000/180`，即 0°=500 µs、90°=1500 µs、
  180°=2500 µs；`deg=` 固定一位小数，`pulse=` 是实际写入的 µs 值。
  设定后由硬件按同一 CCR3 持续输出，中间没有软件定时器重新装载脉宽；
  实际角度与抖动是否合格需目视，本轮未采集（`PATCHES.md` §30.3）。
- `PWM servo 181` 回 `PWM err=range deg=181`；`PWM servo -5` 回 `PWM err=usage`
  ——负号不是数字，先被参数解析拒绝，所以不是 `range`。
- 蜂鸣器 `freq=` 回的是**实际**频率：`period = 100000/hz` 取整后再反算
  （如 2700 Hz → 37 个 10 µs 刻度 → `100000/37 = 2702 Hz`），与请求值可能差 1–2 Hz。
  `duty=` 回的是设定占空比；默认 20%，HARDWARE.md §3 第 20 行建议无源蜂鸣器
  不要给 100%（直流不发声）。
- 蜂鸣器关闭后 `PWM stat` 回 `buz=off`；重新 `PWM buz <hz>` 不保留上次占空比，
  不给第二参数时回到默认 20%。
- `PWM sweep` 用于肉眼确认舵机是否真的在转：0→45→90→135→180→135→90→45→0
  连走两轮，每档停 300 ms，最后回 90°。18 个档位 + 终档共 20 行回包在命令结束时
  **一次性发出**（整个命令阻塞约 5.4 s），串口时间戳只能证明总时长与顺序，不能
  证明每一行是实时逐档出现的；舵机是否转动必须目视。
- `PWM dbg` 是只读快照，不改变任何配置：`psc/arr/ccmr2/ccer/ccr3` 来自 TIM4，
  `crh` 是 F103 的 `GPIOB->CRH`（PB8 在低 4 位），`mapr` 是 `AFIO->MAPR`，
  `cnt0/cnt1` 是相隔约 20 ms 的两次 `TIM4->CNT` 采样；`fpwm` 由 `PSC/ARR`
  反算（0.1 Hz 单位），用于一眼看出预分频对不对。`cnt1-cnt0` 含调度抖动，
  不能当精确计时。
- `PWM test` 是诊断命令：把 PB8 从 TIM4_CH3 切成普通推挽 GPIO，高 500 ms /
  低 500 ms 共 10 个周期（10 s），结束后恢复复用并回到 90°。串口调试时它会
  阻塞 10 s。该命令已编译进固件，但**没有上板运行过**（`PATCHES.md` §32.4）。
- 慢扫期间主循环停摆约 5.4 s：OLED 刷新、`DIST live`、`hb` 都暂停，结束后 `hb`
  会补发几条（同一秒内到达），这是预期现象，不是卡死。
- `PWM err=init` 只在 `pm_pwm_init()` 失败时出现；当前实现恒成功，该分支没有
  现场回包证据（`PATCHES.md` §30.3）。
- 舵机"有动作"已由用户目视确认（`PATCHES.md` §32.4），但角度精度、到位抖动/回弹、
  带负载表现与蜂鸣器听感**不在串口可证范围内**，回包只说明配置值与脉宽。

### 4.7 输入侧（PB14 循迹 / PA8-PA9 编码器 / PB15 按键）

PB14 是 TCRT5000 模块的数字输出（模块自带比较器），上拉输入；PA8/PA9 是 EC11 的
A/B 相，EXTI8/EXTI9 双边沿 + 软件四倍频（x4）；PB15 是编码器按键，上拉输入。
三个引脚都不占用 TIM3/TIM4（已给蜂鸣器与舵机），也不与 USART1/TIM1 冲突，
引脚核对见 `PATCHES.md` §31.1。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `TRACK` / `TRACK stat` | 无 | `TRACK raw=1`（未遮挡/白底）或 `TRACK raw=0`（黑物/遮挡） | `TRACK err=init` | 否（只读） |
| `ENC` / `ENC stat` | 无 | `ENC count=<i32> dir=cw\|ccw\|idle a=<0\|1> b=<0\|1> btn=<0\|1>` | `ENC err=init` | 否（只读） |
| `ENC clear` | 无 | `ENC clear ok count=0` | `ENC err=init` | **是**：计数清零 |
| 其它子命令 | — | — | `TRACK err=usage` / `ENC err=usage` | 否 |

说明：

- `TRACK raw=` 给的是**原始电平**，不是布尔化后的"黑/白"：高电平（`raw=1`）是模块
  比较器输出未触发，低电平（`raw=0`）是检测到反射面变化（黑线/遮挡）。判据是模块
  的数字输出，不是固件里的 ADC 阈值。
- 只有一路 PB14，所以"单路遮挡"和"全黑"在电气上是同一个 `raw=0`，固件无法区分；
  上板只验证过"盖黑物→0、拿开→1"两个状态（`PATCHES.md` §31.3）。
- `count=` 是 32 位有符号累计值，x4 解码：A/B 每产生一次合法跳变计 ±1，
  一个机械格通常 4 个计数，手停在半格时会出现非 4 的倍数（实测 25/39/89）。
  方向由 A/B 相位关系决定（A 领先 B 为 `cw` 即正），**符号与物理转向的对应需目视确认**。
- `dir=` 是最近一次有效跳变的方向，不是本次查询时的运动方向：手停下后仍保持
  `cw`/`ccw`，只有从未动过才是 `idle`。
- `a=`/`b=` 是查询瞬间 PA8/PA9 的电平，`btn=` 是 PB15：`1`=松开，`0`=按下。
- `ENC clear` 只清计数，不清 `dir`（下一格转动前仍显示上次方向）与 a/b/btn。
- `TRACK err=init` / `ENC err=init` 只在端口层初始化失败时出现；当前实现恒成功，
  没有现场回包证据。
- 串口只能证明电平与计数；轮子是否打滑、转向是否与计数符号一致，不在串口可证范围内。

### 4.8 继电器（PB5）

PB5 → R9 3 kΩ → Q3 SS8050 → U7 SRD-05VDC-SL-C 线圈，D2 FR107 续流；板上自带驱动，
MCU 只出电平。**假设高电平吸合**（NPN 低边驱动），复位后 PB5 默认低、线圈释放；
该极性尚未上板实测，串口只能证明引脚电平，吸合声与触点通断需人工确认。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `RELAY on` | 无 | `RELAY on ok pb5=1` | `RELAY err=init` | **是**：PB5 置高 |
| `RELAY off` | 无 | `RELAY off ok pb5=0` | `RELAY err=init` | **是**：PB5 置低 |
| `RELAY` / `RELAY stat` | 无 | `RELAY stat pb5=<0\|1>` | `RELAY err=init` | 否（只读） |
| 其它子命令 | — | — | `RELAY err=usage` | 否 |

- `pb5=` 是**从输出寄存器读回**的实际电平，不是请求值；`on` 之后若读到 0，
  说明写引脚失败或被别处改写。
- PB5 在 F103C8 上可被部分重映射成 TIM3_CH2，但本工程 `AFIO->MAPR=0`、
  TIM3 只用作蜂鸣器时基不输出通道，且 PB5 配置为普通推挽输出，不冲突。

### 4.9 电机（PA0/PA1，DRV8833）

PA0/PA1 → 驱动板 PWM1/PWM2（排针脚 4 / 脚 3，丝印 A0/A1，即 IN1/IN2）。
TIM2 整颗归电机：`PSC=0`、`ARR=3599` → **20 kHz**。控制方式是一路出 PWM、
另一路恒低；两路全低为滑行。`fwd/rev` 只表示选了哪一路电气通道，
**对应轴的哪个机械方向未验证**。

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `MOTOR fwd [0..100]` | 占空比，缺省 50 | `MOTOR fwd ok dir=fwd duty=60% hz=20000 arr=3599 ccr1=2160 ccr2=0` | `MOTOR err=range duty=<n>` / `MOTOR err=usage` / `MOTOR err=timer` / `MOTOR err=remap` | **是**：CH1 出 PWM、CH2 低 |
| `MOTOR rev [0..100]` | 同上 | 同上，`dir=rev`、`ccr1=0 ccr2=<n>` | 同上 | **是**：CH2 出 PWM、CH1 低 |
| `MOTOR stop` | 无 | `MOTOR stop ok dir=stop duty=0% hz=20000 arr=3599 ccr1=0 ccr2=0` | 同上 | **是**：两路全低（滑行） |
| `MOTOR duty <0..100>` | 占空比 | `MOTOR duty ok dir=fwd duty=80% ...` | `MOTOR err=idle`（还没给过方向）/ 同上 | **是**：保持方向改占空比 |
| `MOTOR` / `MOTOR stat` | 无 | `MOTOR stat dir=<fwd\|rev\|stop> duty=<n>% hz=<n> arr=<n> ccr1=<n> ccr2=<n>` | 同上 | 否（只读） |
| `MOTOR dbg` | 无 | `MOTOR dbg apb1enr=<hex> cr1=<hex> ccer=<hex> ccmr1=<hex> arr=<n> psc=<n> cnt=<a>+<b>+<c> crl=<hex>` | `MOTOR err=usage`（带多余参数）/ `MOTOR err=unsupported`（固件未绑实现） | 否（只读） |
| 其它子命令 | — | — | `MOTOR err=usage` | 否 |

- `hz` 由 `PSC/ARR` 现算、`arr` 与 `ccr1/ccr2` 从 `TIM2` 寄存器读回，不是回显请求值；
  `duty=` 是最后一次被接受的方向/占空比请求，两者不一致即说明寄存器被别处改过。
- `duty=100` 时 CCR = 3600 = `ARR+1`，PWM 模式 1 下输出恒有效（满速），不是错误。
- **`duty 0` 与 `stop` 电气等价**：都是两路 CCR=0、两路全低滑行。区别只在
  `dir` 字段的记忆（`fwd 0` 记住方向，之后 `duty 60` 直接续跑）；驱动板行为相同。
- `MOTOR err=timer` 的判据是 `TIM2->CR1.CEN` 已置位（别人已在计数）；
  `MOTOR err=remap` 是 `AFIO->MAPR.TIM2_REMAP` bit0 已置位（CH1/CH2 被移出 PA0/PA1）。
  不看任何软件占用标记，因此不会因状态变量失步而误报。
- 测距停用期间 TIM2 不再有第二个使用者；重新启用 HC-SR04 前必须先解决计时源
  （`docs/TIM2-CONFLICT.md`）。

#### `MOTOR dbg`（临时诊断，不进 HELP）

定位用命令，只回一行原始寄存器快照，不改变任何状态。**不在 `HELP` 列表里**，
是刻意保留的排查入口，不是通用接口。实现在端口层
（`platform/stm32f103/pm_platform.c` 的 `pm_platform_motor_dbg()`），
组件层 `motor_dev` 只持有一个 `const char *(*)(void)` 钩子，由入口胶水层绑定：
`pm_motor` 契约不含板级寄存器细节。

字段按回包顺序：

| 字段 | 来源 | 判读 |
| --- | --- | --- |
| `apb1enr` | `RCC->APB1ENR` | bit0 = `TIM2EN`；为 0 说明时钟没开，后面所有 TIM2 值都不可信 |
| `cr1` | `TIM2->CR1` | `CEN`(bit0)=1 计数器在跑；`=0x81` 即 CEN + ARPE |
| `ccer` | `TIM2->CCER` | `CC1E`/`CC2E` 决定两路输出是否使能；`=0x11` 为两路都开 |
| `ccmr1` | `TIM2->CCMR1` | `OC1M`/`OC2M` 应在 PWM 模式 1（110），并带预装载位 |
| `arr` / `psc` | `TIM2->ARR` / `TIM2->PSC` | 正常为 3599 / 0 → 20 kHz |
| `cnt=<a>+<b>+<c>` | `TIM2->CNT` 三次采样 | 见下条"采样口径" |
| `crl` | `GPIOA->CRL` | PA0/PA1 的 nibble 应为 `0xB`（复用推挽 50 MHz） |

- **`cnt` 采样口径**：三个值之间有两次固定 6000 次的 `__NOP()` 空转，不是瞬时连读。
  20 kHz 下一个 ARR 周期只有 50 µs，连读很可能三次落到同一个计数值而看不出计数器
  是否在动；空转把两次采样拉开到约 0.4 ms（>8 个周期）。判读方法是看三值是否递增：
  全相等说明计数器停着（查 `cr1.CEN`），递增说明在跑。
- **阻塞时长有界**：空转按反汇编是 `nop/subs/bne` 三条指令，Cortex-M3 上约 5 周期/次，
  6000 次约 0.4 ms，两次合计约 0.8 ms，加上格式化整条回包 < 1 ms。**这是推算值，
  未用示波器或计时器实测**；两次循环都是有限次数，计数器停转也不会挂住主循环。
  实测回包 `cnt=2701+3023+2069` 与此相符：`CNT` 只有 0–3599，0.4 ms 已跨 8 个周期，
  按 29122 计数（≈404 µs）折算 `2701+29122 mod 3600 = 3023`，看不出误差。
  即便如此它也不是实时命令：采样点之间有约 0.8 ms 的空档，用来测占空比会失真。
- `MOTOR err=unsupported` 只在入口层没有绑定钩子时出现（组件被别处复用、或板级实现
  被裁掉）；正常固件里不会看到。

---

## 5. 判读陷阱（实测教训）

1. **BLE 会把一条回包拆成多个气泡**。手机页面按"总字节数"判读，不要数气泡个数。
   当前 `HELP` 是 **15 行 638 B**（含每条 CRLF）。
2. **`AHT err=busy` 不是驱动故障**：AHT20 在 repeated START 路径下会返回上一次的
   旧数据并置忙标志；`AHTS` 读到 `raw=98 ...`（bit7=1）属于预期语义，
   见外设模型契约 §12.3。正常读数请用 `AHT`。
3. **手动 `DIST` 在自动测距结束后 60 ms 内会回 `err=timeout`**：HC-SR04 余振，
   模块对紧邻的第二次 ping 不返回有效回波。live 的 1 s 间隔不受影响。
4. **`ERR unknown=AHTAHTSTAT` 之类**：两个命令被拼成一条，通常是前一条没有行尾
   且间隔 < 100 ms，见第 2 节。
5. **`BT tx n=..B: ...` 不是命令回包**，只是 USART3 回包在 USART2 的镜像。
6. **异步回包会乱序**：`AHT`/`DIST`/`OLED` 的完成回包可能排在其后一条命令的回包之后，
   按前缀匹配。

---

## 6. 已知限制

- 上电默认 live，屏会自己按 500 ms 刷新；"静止不动"只可能是 `OLED off`、
  `OLED live off` 或 live 作业出错。`OLED show` 在 live 期间不再被拒（旧行为是
  回 `OLED err=busy live`），而是临时接管 3 s 后回 live。
- 无行尾的命令有 100 ms 判定窗口：最坏 100 ms 才执行；片段间隔 < 100 ms 会被拼接。
- 单行 47 字符上限，超出部分静默丢弃，没有报错。
- 空行（只有 `\r`/`\n`）不执行也不回包。
- CH1116 的 I²C 模式读不回状态和 GDDRAM，屏幕内容只能靠肉眼或
  `OLED frame` / `OLED test` 这类确定性图案验证。
- `AHTS` 只作对照路径保留，不建议作为正常读数接口。
