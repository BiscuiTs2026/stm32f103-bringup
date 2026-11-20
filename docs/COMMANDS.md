# 命令表（解析器原文核对版）

本表**不是**从 `HELP`、`PROTOCOL.md`、注释或旧日志转述的，而是逐条对照
`src/app/app_wiring.c` 的注册表与各处理函数的解析代码整理。核对基线：work 仓
`17d4389`（解析逻辑与 `997b38a` 相同，仅 `HELP` 描述串有改动）。

读法约定：

- "关键字原文"列写的是**解析器实际比较的字符串**；`[前 N 字符]` 表示只比较前 N 个字符，
  后面跟什么都会被吞掉。
- 回包样例里的动态值用实际量级；字段名与拼接顺序与代码一致。
- 每行末尾给出核对位置（`文件:行`），便于复核。

---

## 0. 路由规则（先看这一段）

| 机制 | 命令 | 行为 |
| --- | --- | --- |
| `PM_CMD_EXACT` | `HELP` `PING` `STAT` `I2C` `AHT` `AHTS` `DIST` `ADC` `KEYS` | 整行大小写不敏感完全相等；多一个空格或参数都不匹配，最终回 `ERR unknown=<原行>` |
| `PM_CMD_TRY` | `PWM` `TRACK` `ENC` `RELAY` `MOTOR` | 先跳过行首空格，再比前 N 字符，要求第 N 个字符是空格或行尾；不认识时把行交还给下一个处理器 |
| `PM_CMD_PREFIX` | `OLED` | 必须从第 0 列开始比 `OLED`（不跳行首空格）；第 4 字符必须是空格或行尾；参数部分再去掉前导空格 |

所有关键字大小写不敏感。未知命令统一回：

```
ERR unknown=<原始行内容>
```

空行不执行、不回包（`pm_cmd_console.c`）。命令最长 47 字符，超出部分被丢弃
（`CMD_LINE_MAX=48`）。

---

## 1. 通用命令

| 关键字原文 | 参数 | 范围/单位 | 默认 | 合法回包样例 | 不合法回包 |
| --- | --- | --- | --- | --- | --- |
| `HELP`（整行） | 无 | — | — | 15 行，每行 `HELP <NAME>=<说明>`；当前 660 B（源码逐条核算） | `ERR unknown=HELP `（带尾随空格） |
| `PING`（整行） | 无 | — | — | `PONG` | 同上 |
| `STAT`（整行） | 无 | — | — | `STAT clk=72000000 up=12345 tick=1 key1=9 key2=7 uart_rx=123 uart_tx=456` | 同上 |
| `I2C`（整行） | 无 | — | — | `I2C found=0x70,0x7A` | `I2C found=none`（总线上没有器件） |
| `KEYS`（整行） | 无 | — | — | `KEYS k1=9 k2=7 last=k1`（`last=none\|k1\|k2`）；**读出后计数清零** | 同上 |

核对：`app_wiring.c:38-41,56`、`sys_cmds.c:29-75`、`keypad_dev.c:88-108`。

---

## 2. 传感器与显示

| 关键字原文 | 参数 | 范围/单位 | 默认 | 合法回包样例 | 不合法回包 |
| --- | --- | --- | --- | --- | --- |
| `AHT`（整行） | 无 | — | — | `AHT ok rh=40.8 t=20.4 raw=18 68 A7 F5 A3 11 rst=0` | `AHT err=busy pending`（80 ms 窗口内重复发）/ `AHT err=busy oled`（OLED 推送中）/ `AHT err=busy raw=…`（传感器 3 次仍未就绪）/ `AHT err=io raw=…` |
| `AHTS`（整行） | 无 | — | — | 同上，`rst=1` | 同上 |
| `DIST`（整行） | 无 | — | — | ON：`DIST cm=99.0 us=5822`；OFF：`DIST err=disabled` | `DIST err=timeout` / `DIST err=busy` / `DIST err=busy live` |
| `ADC`（整行） | 无 | — | — | `ADC pot=2124 pot_mv=1709 ntc=2293 ntc_mv=1846 vref=3300` | `ADC err=init` / `ADC err=timeout ch=pa5`（或 `ch=pa4`） |
| `OLED`（前缀） | 见下 | — | — | 见下 | `OLED err=usage` / `OLED err=usage col=0..4` / `OLED err=busy pending` / `OLED err=busy i2c` / `OLED err=io` |

### 2.1 `OLED` 子命令（全部 require 前缀 `OLED `）

| 关键字原文 | 参数 | 范围/单位 | 默认 | 合法回包样例 | 不合法回包 |
| --- | --- | --- | --- | --- | --- |
| `ON`（整段） | 无 | — | — | `OLED on ok display=on live=on af=0 berr=0 to=0` | `OLED err=io` |
| `OFF`（整段） | 无 | — | — | `OLED off ok display=off live=paused af=0 berr=0 to=0` | `OLED err=io` |
| `LIVE ON`（`LIVE ` 前缀 + `ON`） | 无 | — | — | `OLED live=on period=500ms` | `OLED err=usage` |
| `LIVE OFF` | 无 | — | — | `OLED live=off` | `OLED err=usage` |
| `CLEAR` / `TEST` / `FRAME` / `SHOW`（整段） | 无 | — | — | 异步：`OLED frame ok ms=12 col=2 live=on hold=3000ms af=0 berr=0 to=0`（`hold` 仅在 live 开启时出现；`clear/test` 同理） | `OLED err=busy pending` / `OLED err=io` |
| `COL` + 数字**可无空格** | `0..4` | 列偏移，整数 | — | `OLED col=2 ok redraw=live`（静态屏时 `redraw=<屏名>`） | `OLED err=usage col=0..4` |
| `DUMP`（整段） | 无 | — | — | `OLED dump mode=live screen=live`，随后 8 行 `OLED L0=…`～`OLED L7=…`（仅 live/show 时带 8 行） | — |

核对：`aht20_dev.c:52-106,108-137,139-174`、`sonar_dev.c:55-110`、
`adc_dev.c:36-140`、`oled_ui_job.c:220-377,400-434,488-510`、
`oled_ui_page.c:140-165`。

---

## 3. `PWM`（TIM4 CH3 舵机 + TIM3 蜂鸣器）

顶层：`PWM` + 空格/行尾；子命令**只比前缀，不检查边界**（见 §6 的坑）。

| 关键字原文 | 参数 | 范围/单位 | 默认 | 合法回包样例 | 不合法回包 |
| --- | --- | --- | --- | --- | --- |
| `SERVO`[5] | 角度整数 | 0..180，单位 ° | — | `PWM servo ok deg=90.0 pulse=1500us` | `PWM err=range deg=181` / `PWM err=usage`（缺参、负数、非数字）/ `PWM err=init` |
| `BUZ`[3] | `hz`，可选 `duty%` | hz 100..5000；duty 1..99 | duty **20** | `PWM buz ok freq=1000Hz duty=20%` | `PWM err=range hz=99` / `PWM err=range duty`（>99，不带数值）/ `PWM err=range`（duty=0 或端口拒绝）/ `PWM err=usage` / `PWM err=init` |
| `BUZ`[3] + `off`[3] | 无 | — | — | `PWM buz off ok` | `PWM err=init` |
| `SWEEP`[5] | 无 | — | — | 20 行：`PWM sweep deg=0.0 pulse=500us` ×18 + 终档 90° + `PWM sweep done`（阻塞约 5.7 s） | `PWM err=range` / `PWM err=init` |
| `DBG`[3] | 无 | — | — | `PWM dbg apb1enr=0x00260007 apb2enr=0x0000020D cr1=0x00000081 psc=0x00000015 arr=0x0000FFAD ccmr2=0x00000068 ccer=0x00000100 ccr3=0x0000132D cnt0=… cnt1=… dt=20ms crh=0x88888B2B mapr=0x00000000 fpwm=50.0Hz`（寄存器一律 `0x%08X`；`psc/arr` 由 `pm_pwm_timing_compute` 按 72 MHz/50 Hz 选定 = 21/65453，**不是**旧手写驱动的 71/19999；`ccr3` 随当前角度变） | `PWM err=init` |
| `TEST`[4] | 无 | — | — | `PWM test done 1Hz 10s pb8=af servo=90.0 pulse=1500us`（阻塞 10 s） | `PWM err=init` |
| `STAT`[4] | 无 | — | — | `PWM stat servo=90.0c pulse=1500us buz=off`（注意 `90.0c` 中间**无空格**）；蜂鸣器开时 `buz=1000Hz/20%` | `PWM err=init` |

核对：`pwm_dev.c:131-152,180-260,262-296,299-376,384-434`。

---

## 4. 输入与执行器

| 关键字原文 | 参数 | 范围/单位 | 默认 | 合法回包样例 | 不合法回包 |
| --- | --- | --- | --- | --- | --- |
| `TRACK`[5] 或 `TRACK STAT`[4] | 无 | — | — | `TRACK raw=1`（未遮挡）/ `TRACK raw=0`（遮挡） | `TRACK err=init` / `TRACK err=usage`（任何其它词，**包括 `raw`**） |
| `ENC`[3] 或 `ENC STAT`[4] | 无 | — | — | `ENC count=28 dir=cw a=1 b=1 btn=1` | `ENC err=init` / `ENC err=usage`（**没有 `count` 这个关键字**） |
| `ENC CLEAR`[5] | 无 | — | — | `ENC clear ok count=0`（方向保持原值） | `ENC err=init` / `ENC err=usage` |
| `RELAY ON`[2] | 无 | — | — | `RELAY on ok pb5=1` | `RELAY err=init` / `RELAY err=usage` |
| `RELAY OFF`[3] | 无 | — | — | `RELAY off ok pb5=0` | 同上 |
| `RELAY STAT`[4] | 无 | — | — | `RELAY stat pb5=0` | 同上；**`RELAY` 裸命令是 usage，不是 stat** |
| `MOTOR FWD`[3] | 可选 duty | 0..100，单位 % | **50** | `MOTOR fwd ok dir=fwd duty=60% hz=20000 arr=3599 ccr1=2160 ccr2=0` | `MOTOR err=range duty=101` / `MOTOR err=usage` / `MOTOR err=timer` / `MOTOR err=remap` |
| `MOTOR REV`[3] | 可选 duty | 0..100，单位 % | **50** | `MOTOR rev ok dir=rev duty=60% hz=20000 arr=3599 ccr1=0 ccr2=2160` | 同上 |
| `MOTOR STOP`[4] | 无 | — | — | `MOTOR stop ok dir=stop duty=0% hz=20000 arr=3599 ccr1=0 ccr2=0` | 同上 |
| `MOTOR BRAKE`[5] | 无 | — | — | `MOTOR brake ok dir=stop duty=0% hz=20000 arr=3599 ccr1=3600 ccr2=3600` | `MOTOR err=usage`（带多余参数）/ 同上 |
| `MOTOR DUTY`[4] | duty | 0..100，单位 % | — | `MOTOR duty ok dir=fwd duty=80% hz=20000 arr=3599 ccr1=2880 ccr2=0` | `MOTOR err=idle`（还没给过方向）/ `MOTOR err=range duty=<n>` / `MOTOR err=usage` |
| `MOTOR STAT`[4] | 无 | — | — | `MOTOR stat dir=fwd duty=60% hz=20000 arr=3599 ccr1=2160 ccr2=0` | 同上；**`MOTOR` 裸命令是 usage，不是 stat** |
| `MOTOR DBG`[3] | 无 | — | — | `MOTOR dbg apb1enr=… cr1=… ccer=… ccmr1=… arr=3599 psc=<n> cnt=…+…+… crl=…` | `MOTOR err=unsupported`（固件没绑实现）/ `MOTOR err=usage` |

核对：`track_dev.c:101-118`、`encoder_dev.c:154-176`、`relay_dev.c:116-135`、
`motor_dev.c:219-290`、`platform/stm32f103/pm_platform.c`（`MOTOR dbg` 实现）。

---

## 5. 回包里的异步与状态语义

- `AHT`/`AHTS`、`DIST`、`OLED` 是异步的：回包可能在后续命令之后才到，按前缀配对，不要按顺序。
- `PWM servo/sweep/test`、`MOTOR ...` 是同步的：处理函数返回前已写完回包。
- `ENC dir` 不是实时方向：它是"每次收到 `ENC` 时，与上一次采样相比的计数增量符号"；
  `=0` 时保持上一次方向；只有从未动过才是 `idle`；`ENC CLEAR` 不重置方向。
- `OLED DUMP` 不碰 I2C，推送中途也能回；其余 `OLED` 命令在整帧忙时回 `busy pending`。

---

## 6. 实现细节与坑（照敲前必看）

1. **只比前缀，不查边界**（`PWM` 子命令层）：
   `PWM statx`、`PWM dbgXYZ`、`PWM sweepjunk`、`PWM testfoo` 都会被当成对应命令执行；
   `PWM buz offx` 也会关蜂鸣器。反过来 `PWM buzzer 4000` 会被当成 `BUZ` + 参数 `zer 4000`，
   取数失败 → `PWM err=usage`。
2. **数字后面跟垃圾会被忽略**（`PWM servo/buz` 的 `take_u32`）：
   `PWM servo 90abc` 执行 90°；`PWM buz 4000junk` 执行 4000 Hz/20%。
   第二个参数不是数字时不报错，静默用默认 20%：`PWM buz 4000 abc`。
3. **`RELAY on/off/stat` 后面的多余参数被忽略**：`RELAY on x` 仍会吸合。
   但 `RELAY` 裸命令回 `RELAY err=usage`。
4. **`MOTOR` 的数字参数是严格的**：`MOTOR fwd 50 `（尾随空格）或 `MOTOR fwd 50x`
   都回 `MOTOR err=usage`，与 `PWM` 的宽松解析不同。
5. **余下参数严格**：`MOTOR stop/brake/stat/dbg` 带任何多余内容都回 usage。
6. **`OLED` 必须从第 0 列开始**：行首有空格就落到 `ERR unknown=`。
   但 `COL` 后面可以没有空格：`OLED col2` 合法。
7. **`EXACT` 类命令不容忍尾随空格**：`STAT `、`AHT ` 都会回 `ERR unknown=<原行>`。
8. `PWM`/`TRACK`/`ENC`/`RELAY`/`MOTOR` 的处理器会跳过行首空格，因此
   `  MOTOR stat` 仍然有效（`OLED` 除外）。
9. `PWM` 的 `STAT` 回包里角度与单位 `c` 之间**没有空格**（`servo=90.0c`），这是代码原样。
10. `PWM buz` 的 `duty>99` 回 `PWM err=range duty`，**不带数值**；`hz` 越界才带 `hz=<n>`。

---

## 7. "说明名 ≠ 解析器名"核对结果（只列，不改）

| # | 位置 | 文档写的 | 解析器实际 | 后果 |
| --- | --- | --- | --- | --- |
| 1 | `app_wiring.c:53` 的 `HELP` 行 | `ENC=count/clear, dir, a/b, button PB15` | 只有裸 `ENC`、`ENC stat`、`ENC clear`；没有 `COUNT` 分支 | 用户照 `HELP` 敲 `ENC count` 会得到 `ENC err=usage` |
| 2 | `app_wiring.c:52` 的 `HELP` 行 | `TRACK=raw level of PB14 line sensor` | 只有裸 `TRACK`、`TRACK stat`；没有 `RAW` 分支 | `TRACK raw` 会得到 `TRACK err=usage` |
| 3 | `PROTOCOL.md:378` | `RELAY` / `RELAY stat` 并列，暗示裸 `RELAY` 可用 | 裸 `RELAY` 直接落到 `RELAY err=usage` | 用户敲 `RELAY` 会得到 usage |
| 4 | `PROTOCOL.md:404` | `MOTOR` / `MOTOR stat` 并列，暗示裸 `MOTOR` 可用 | 裸 `MOTOR` 直接落到 `MOTOR err=usage` | 用户敲 `MOTOR` 会得到 usage |
| 5 | `app_wiring.c:51` 的 `HELP` 行 | 只列 6 个子命令 | 另有 `PWM test`（10 s 阻塞诊断） | 不是错误，但 `HELP` 不完整；`PROTOCOL.md:286` 有列 |
| 6 | `app_wiring.c:55` 的 `HELP` 行 | 只列 `.../duty/stat` | 另有 `MOTOR dbg` | `HELP` 不完整；`PROTOCOL.md:405` 有列 |
| 7 | `app_wiring.c:50` 的 `HELP` 行 | `OLED=...col/live/dump` | `live` 必须带 `on`/`off`；`col` 必须带 0..4 | 不是名字错，但 `HELP` 没写参数，裸 `OLED live` 回 usage |

未发现其它"说明名与解析器名"不一致：`HELP`/`PING`/`STAT`/`I2C`/`AHT`/`AHTS`/
`DIST`/`ADC`/`KEYS` 的说明名与注册名一致；`PWM` 的 `servo/buz/buz off/sweep/dbg/stat`
与 `PROTOCOL.md` 4.6 一致；`OLED` 的 `on/off/clear/test/frame/show/dump` 一致。
