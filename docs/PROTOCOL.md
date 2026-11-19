# 命令协议（board-bringup 固件）

面向以后接手的人或另一个 AI。本文描述的是**实际烧在板上的行为**（2026-11-18 版本），
与 `PATCHES.md` 里的上板实测一致。改协议时必须同步改这份文件。

实现位置：`src/app/main.c` 的 `cmd_feed()` / `cmd_execute()` / `cmd_flush_idle()`。

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
  | `AHT err=busy` | AHT20 自己没测完（状态字节 bit7=1），已重试 3 次 | 上电初期、总线异常 |
  | `DIST err=busy` | 上一次手动测距还在进行 | 60 ms 内重复发 `DIST` |
  | `DIST err=busy live` | live 模式自动测距正在进行 | live 开启时手动 `DIST` 撞上 |
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
| `HELP` | 无 | 10 行 `HELP ...`，共 **398 B**（见下） | — | 否 |
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
HELP DIST=one shot, cm and us; live=1Hz
HELP OLED=on/off/clear/test/show/frame/col/live
HELP KEYS=key press counters, clears them
```

`up=` 是开机毫秒数；`key1/key2` 是消抖后的按下次数；`uart_rx/uart_tx` 是两路累计字节数。

### 4.2 AHT20 温湿度（I2C1，0x70）

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `AHT` | 无 | `AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0` | `AHT err=busy pending` / `AHT err=busy raw=...` / `AHT err=io raw=...` | 触发一次测量 |
| `AHTS` | 无 | 同上，`rst=1` | 同上 | 触发一次测量（repeated START 路径，仅作对照） |

- `rh`/`t` 单位是 0.1（`rh=40.8` 即 40.8 %RH，`t=20.4` 即 20.4 °C）。
- `raw=` 是读回的 6 个原始字节，十六进制、空格分隔，失败时也照打。
- `rst=0` 表示"STOP 结束触发 + 重新 START 读"，`rst=1` 表示触发后不发 STOP 直接 repeated START 读。
- `AHTS` 读到的是**上一次**结果（`d[0]` bit7=1 即忙），这是 AHT20 的语义，不是驱动 bug。

### 4.3 HC-SR04 测距（PA11=TRIG / PA10=ECHO）

| 命令 | 参数 | 成功回包 | 失败回包 | 改变状态 |
| --- | --- | --- | --- | --- |
| `DIST` | 无 | `DIST cm=75.2 us=4359` | `DIST err=timeout` / `DIST err=busy` / `DIST err=busy live` | 触发一次测距 |

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
| `OLED show` | 无 | `OLED show ok ms=98 col=2 live=on hold=3000ms ...` | `OLED err=busy pending` / `err=busy i2c` | **是**：静态三行状态，3 s 后回 live |
| `OLED col <0..4>` | 列偏移 | `OLED col=2 ok redraw=<图案名>` | `OLED err=usage col=0..4` | **是**：改列偏移并立即重画 |
| `OLED live on` | 无 | `OLED live=on period=500ms` | `OLED err=busy pending` / `err=busy i2c` | **是**：进入周期刷新 + 1 Hz 自动测距 |
| `OLED live off` | 无 | `OLED live=off` | — | **是**：停住一帧静态画面，屏保留最后一帧 |
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
- `live` 四行（page 0..3，page 4..7 清空）：

  ```
  UP=<秒>S
  K1=<n> K2=<n>
  DIST=<cm>.<0.1cm>CM     （无有效测距为 DIST=NONE）
  I2C af=<n> berr=<n>
  ```

- `live` 期间每 500 ms 刷新一次显示，每 1000 ms 自动发一次 `DIST`；
  测量起点比显示周期偏移 250 ms，两者互不阻塞。
- 静态图案的 3 s 停留期间（以及 `OLED off` 期间）两者都暂停，恢复 live 后
  立刻重画一帧（实测 `OLED on` 后 98 ms 内出现首条 `OLED live ok`）。
- `OLED col` 在 live 期间可用，回 `redraw=live` 并立即重画。

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

---

## 5. 判读陷阱（实测教训）

1. **BLE 会把一条回包拆成多个气泡**。手机页面按"总字节数"判读，不要数气泡个数。
   当前 `HELP` 是 **10 行 398 B**（含每条 CRLF）。
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
