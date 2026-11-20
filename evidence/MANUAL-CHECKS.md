# 人工验收作业单（需要人按 / 听 / 看 / 动手的项）

用途：`SPLIT-PLAN.md` §5.0 的 **H 组**统一登记在这里。机器项（编译、扫描、烧录校验、
串口回文）不写在本表，见 `evidence/INDEX.md` 与各 `split-s*.log`。

固件：`build-split-s9/pm_f103_demo.hex`（S9b 提交，`git log -1` 可查），已 `-v` 回读校验通过。
（M1–M8 原登记时对应 `build-split-s8`；S9a/S9b 未改按键、继电器、电机、蜂鸣器、ADC、蓝牙代码。）
条件：板子通电、ST-Link 已断开（或至少不占用）、串口 **COM11 @ 115200 8N1**，
串口工具的 DTR/RTS 保持无效（避免打开端口即复位）。

状态取值：`PENDING`（未做，默认值）/ `PASS`（有证据）/ `FAIL`（有证据，需回退）。
**写 PASS 必须同时填"证据"列**（日志文件名 + 行号或字节数），否则仍算 PENDING。
判"未验证"的项不得写成通过。

## 0. 通用准备与记录

1. 打开 COM11，保存原始日志到 `evidence/manual-<项>.log`（UTF-8，带毫秒时间戳）。
2. 按键/字节类测试先发 `OLED off` 关掉 live 刷屏，减少串口噪声；做完发 `OLED live` 恢复。
3. 每条命令以 `\r\n` 结尾，命令大小写不敏感。
4. 判读陷阱（来自 `docs/PROTOCOL.md` §5）：BLE 回包会被拆成多个气泡，按**总字节数**判，
   不数气泡个数；`DIST` 现在恒回 `DIST err=disabled`（TIM2 已划给电机）；`PWM sweep`
   会阻塞约 5.4 s，期间 `hb`/OLED 停摆属预期。

## 1. 总表（做完逐行填）

| # | 项目 | 方法 | 出处 | 状态 | 证据 / 备注 |
| --- | --- | --- | --- | --- | --- |
| M1 | KEY1/KEY2 按键五项 | 手按 | SPLIT-PLAN §3 S8、`docs/PROTOCOL.md` §2 | PENDING | — |
| M2 | 蓝色心跳灯 1 Hz | 目视 | SPLIT-PLAN §5.3 | PENDING | — |
| M3 | 继电器吸合/释放声 | 耳听 | `docs/PROTOCOL.md` §4.8 | PENDING | — |
| M4 | 电机转向与转速 | 目视 | `docs/PROTOCOL.md` §4.9 | PENDING | — |
| M5 | 蜂鸣器听感 | 耳听 | `docs/PROTOCOL.md` §4.6 | PENDING | — |
| M6 | 电位器响应（十字螺丝刀） | 手转 + 看数 | `docs/PROTOCOL.md` §4.5、SPLIT-PLAN S7 判据 | PENDING | — |
| M7 | 蓝牙双向镜像（S2 欠账 + S9c 控制台搬迁） | 手机 BLE 发字节 | SPLIT-PLAN §5.4、§8.3 核账 S2 行、§3 S9c | PENDING | — |
| M8 | 编码器按键与手感 | 手转 / 手按 | `docs/PROTOCOL.md` §4.7 | PENDING | — |
| M9 | OLED 面板图案与对齐 | 目视 | SPLIT-PLAN §3 S9、`HARDWARE.md` 第 9 条 | PENDING | — |
| M10 | OLED 面板是否点亮（背光/供电） | 目视 | 2026-11-20 09:35 复位抓包 `split-s6a-oled.log` | PENDING | 串口侧正常，屏不亮待目视 |

---

## M1 按键 KEY1/KEY2（S8 待补的"按键四项"+ LED 镜像）

硬件：KEY1、KEY2 为低有效（上拉），按下时对应 LED 点亮。串口先 `OLED off`。

### M1-a 单按文本与字节数

1. 发 `STAT`，记下 `uart_tx=` 的初始值（可选辅助判据）。
2. 按 KEY1 约 0.5 s 后松开。
3. 期望串口依次出现两行（**含 CRLF 共 14 B / 15 B**）：

```
KEY1 pressed
KEY1 released
```

4. 同样按一次 KEY2，期望 `KEY2 pressed` / `KEY2 released`。
5. 辅助判据：抓包里这两行的字节数必须正好 14 与 15。用 `STAT uart_tx` 差值判时，
   须扣除每秒 `hb`（4 B）与 `STAT` 回包自身字节，1 s 内完成测量可近似核对 +14/+15。

判据：文本逐字正确；字节数 14/15；按下时 LED 亮、松开灭（目视）。
证据：日志文件名 + 这两行的字节数。

### M1-b KEYS 读数与清零

1. 先发两次 `KEYS`，确认都是 `KEYS k1=0 k2=0 last=none`。
2. 按一次 KEY1，发 `KEYS` → 期望 `KEYS k1=1 k2=0 last=k1`。
3. **立刻**再发 `KEYS` → 期望 `KEYS k1=0 k2=0 last=none`（读后清零，含 `last`）。
4. 按一次 KEY2，重复第 2–3 步 → `k2=1 last=k2`，再读回 `0 / none`。

判据：计数与 `last` 正确，且第二次读一定清零。

### M1-c OLED 第 5 行同步

1. `OLED live` 状态下按一次 KEY1，1 s 内发 `OLED dump`。
2. 期望第 5 行（5 位右对齐定宽，注意空格数）：

```
OLED L5=K1=    1 K2=    0
```

3. 按 KEY2 → `OLED L5=K1=    0 K2=    1`；再 `KEYS` 清零后回到 `K1=    0 K2=    0`。

判据：数值与 `KEYS` 一致、宽度不跳变（数字变宽时空格数减少、总宽不变）。

### M1-d 长按 3 s 只计 1 次

1. `KEYS` 清零。
2. 按住 KEY1 持续 3 s 以上再松开。
3. 发 `KEYS` → 期望 `k1=1`（不是 2 以上）。

### M1-e 快速点 20 次

1. `KEYS` 清零。
2. 以约每秒 2 次的节奏点 KEY1 共 20 次（消抖窗口 20 ms，允许丢 2 次）。
3. 发 `KEYS` → 期望 `k1` 落在 `[18,20]`。

判据：`k1∈[18,20]`；低于 18 记 FAIL（消抖或轮询间隔有问题）。

---

## M2 蓝色心跳灯

1. 上电后目视蓝色 LED。
2. 期望：约 500 ms 亮 / 500 ms 灭，稳定 10 s 不卡顿。

出处：SPLIT-PLAN §5.3（`hb` 1 Hz 的目视对照）。`s_led_cfg` 属禁止搬迁项，
本项用于确认拆 S8 后心跳灯未被改动。

---

## M3 继电器（PB5）

1. 发 `RELAY stat` → 期望 `RELAY stat pb5=0`（复位后释放）。
2. 发 `RELAY on` → 期望 `RELAY on ok pb5=1`，**耳听**线圈吸合"咔哒"一声。
3. 发 `RELAY stat` → `RELAY stat pb5=1`。
4. 发 `RELAY off` → `RELAY off ok pb5=0`，再听一声释放"咔哒"。
5. 连续 `on` / `off` 各 5 次，确认每次都响、不粘连。

判据：回包与吸合声同步；`pb5=` 是从输出寄存器读回的实际电平。
出处：`docs/PROTOCOL.md` §4.8（极性假设高电平吸合，本次即为首次上板确认）。

---

## M4 电机（PA0/PA1，DRV8833，TIM2 20 kHz）

1. `MOTOR stat` → 期望 `dir=stop duty=0% hz=20000 arr=3599 ccr1=0 ccr2=0`。
2. `MOTOR fwd 30` → 回包 `dir=fwd ... ccr1=<非0> ccr2=0`；**目视**轴开始转，
   记下 `fwd` 对应的实际机械方向（顺/逆时针）。
3. `MOTOR duty 80` → 转速明显加快，方向不变。
4. `MOTOR rev 50` → 轴**反向**转。
5. `MOTOR stop` → 轴滑行停止（不是急刹），回包 `ccr1=0 ccr2=0`。
6. 听运转声音：无尖锐啸叫、无明显抖动。

判据：转向随 fwd/rev 改变、转速随 duty 单调变化、stop 后停转。
出处：`docs/PROTOCOL.md` §4.9（"fwd 对应哪个机械方向未验证"，本次补齐）。

---

## M5 蜂鸣器（PB9，无源）

1. `PWM buz 1000` → 回包 `PWM buz ok freq=1000Hz duty=20%`，听 1 kHz 常音。
2. `PWM buz 2000 50` → 音调升高，音量变大。
3. `PWM buz 2700` → 回包 `freq=2702Hz`（取整后反算，允许 ±2 Hz），音调继续升高。
4. `PWM buz off` → 静音，回包 `PWM buz off ok`。
5. `PWM stat` → `buz=off`。

判据：能听到与 `freq=` 对应的音调、off 后完全静音；不得出现"只有直流不发声"
（duty 默认 20%，不要给 100%）。
出处：`docs/PROTOCOL.md` §4.6。

---

## M6 电位器（PA5，需十字螺丝刀）

1. 不动电位器，连发两次 `ADC`，记下 `pot=` / `pot_mv=` 静态值。
2. 用十字螺丝刀把电位器逆时针转到底，发 `ADC`；再转到中间、顺时针到底，各发一次。
3. 期望：`pot` raw 随角度单调变化，覆盖大部分 0–4095；`pot_mv` 满足
   `pot_mv == (pot*3300 + 2047) / 4095`（整数运算，四舍五入；允许 ±1 LSB）。
4. `OLED dump` 第 4 行 `POT=<raw>` 应与最近一次 `ADC` 的 `pot=` 差 ≤32。
5. 记录可调范围（最小/最大 raw）与是否有跳变、死区。

判据：能连续调节、数值单调、换算公式成立。
出处：`docs/PROTOCOL.md` §4.5（此前"未转动验证"）、SPLIT-PLAN S7 裁决的换算口径。

---

## M7 蓝牙双向镜像（S2 欠账 + S9c 控制台搬迁，需人拿手机点一次）

覆盖两处出处，一次测完：
- S2：蓝牙侧回复在调试口的 `BT tx` 镜像等价（SPLIT-PLAN §5.4、§8.3 核账表 S2 行）；
- S9c：控制台搬到 `pm_cmd_console` 后，`BT rx` 镜像仍在该口回包之前（SPLIT-PLAN §3 S9c）。

前置：手机装任意 BLE 串口 App（或 Chrome Web Bluetooth 页面），板子 USART3 9600 8N1；
COM11 开抓包（115200）。蓝牙侧固件是 `build-split-s9`（S9c 提交）。

1. 手机连 DX-BT24，打开收发页面；COM11 开始抓包。
2. 手机发 **4 字节 `PING`**（不带 CR/LF）。
3. 期望 COM11 先出现
   `BT rx n=4B: 50 49 4E 47`
   紧接着出现
   `BT tx n=6B: 50 4F 4E 47 0D 0A`（即 `PONG\r\n` 的镜像）。
   顺序必须是 `BT rx` 在前、同一次命令的 `BT tx` 在后；手机端同时收到 `PONG`。
4. 手机发 `HELP`：手机端收 15 行、共 638 B；COM11 出现对应的 `BT tx` 镜像行，
   字节数与手机端一致。
5. 可选加测：手机发 `AHT`（不带 CR/LF）→ 100±20 ms 内手机端回包；
   COM11 先出现 `BT rx n=3B: 41 48 54`。

判据：
- `BT rx` 行存在，`n=` 与 hex 内容同手机实际发送一致；
- 该行严格位于同一次命令的 `BT tx` 回包镜像之前（S9c 判据）；
- `HELP` 的 `BT tx` 字节数与手机端一致（S2 判据）。

证据：手机侧日志存 `manual-bt-phone.log`，COM11 侧存 `manual-bt-uart2.log`；
无日志不得只凭记忆判 PASS。
出处：SPLIT-PLAN §5.4、§8.3 核账表 S2 行、§3 S9c。

**当前状态：PENDING。** 2026-11-20 S9c 上板时本机无 BLE 对端（只有经典蓝牙 SPP 口），
该项未验证；S9c 其余板端项已通过，见 `split-s9c-frames.log`。

---

## M8 编码器（PA8/PA9 旋转 + PB15 按键）

1. 上电后发 `ENC` → 记基线 `count=`。
2. 顺时针转 5 格，发 `ENC` → `count` 增加约 5×4=20（x4 四倍频），`dir=cw`，`a/b` 有变化。
3. 逆时针转 5 格 → `count` 回落，`dir=ccw`；停手 1 s 后 `dir=idle`。
4. 按住编码器按键 → `ENC` 回 `btn=1`；松开 → `btn=0`。
5. `ENC clear` → `ENC clear ok count=0`。
6. 手感记录（主观，只写观察，不作通过/失败判据）：每格是否有清晰卡位、
   按键段落感是否正常、旋转有无丢步（对比转 20 格后的 count 偏差）。

判据：count 方向与幅度正确、btn 跟随按压、clear 生效。
出处：`docs/PROTOCOL.md` §4.7。

---

## M9 OLED 面板图案与对齐（S9a/S9b 搬迁后人眼看）

前置：串口先 `OLED off` 停止刷屏，做完 `OLED live` 恢复。

1. `OLED test` → 屏上应出现 0xAA 测试花屏（棋盘/竖条），无整屏空白、无半屏残留。
2. `OLED clear` → 整屏熄黑。
3. `OLED show` → 8 行文本齐全：`MODE=LIVE`、`NOW`、`LAST`、`RH/T`、`NTC/POT`、
   `K1/K2`、`I2C`、`LIVE ms`；行间不重叠、右侧字符不被裁掉。
4. `OLED col 0` / `col 4` 各发一次，比较同一页文字的左边缘位置：列偏移应改变，
   且两个极值下首字符都不贴边、末字符不出界。
5. `OLED frame` → 屏幕边框图案完整。
6. 3 s 无命令后应自动回到 live 页（`dump` 显示 `mode=live`）。

判据：图案/文字与上述一致，肉眼无花屏、无错位、无残影到下一屏。
证据：照片或视频文件名；无照片则记 `PENDING`，不得只凭串口 `dump` 判 PASS。

---

## M10 OLED 面板是否点亮（硬件侧排查）

背景：2026-11-20 上午现场报告"OLED 不亮"。09:35 用 `-rst` 复位并抓 COM11
（`evidence/split-s6a-oled.log`），固件侧完全正常：

- 第 14–15 行：`OLED CH1116 addr=7A init ok, col=2, clearing` → `OLED ready (128x64, 6x8 font)`；
- 第 16 行起每 500 ms 一条 `OLED live ok ms=98 col=2 af=0 berr=0 to=0`（I2C 写无仲裁失败、无总线错误、无超时）。

即：**固件认为屏在正常收帧**。屏不亮只能是硬件侧，按下列顺序看：

1. 目视机头：屏幕玻璃是否全黑（背光也没了）还是有暗字。
2. 量 OLED 排针 1 脚 VCC 与 2 脚 GND：应为 3.3 V。
3. 拔插 I2C 四线（VCC/GND/SCL/SDA），确认没有半插、错位。
4. 若 VCC 有电且线正常，换一块 CH1116 模块对比（`OLED test` 出花屏即模块故障）。

判据：屏上出现 `OLED test` 的图案或 live 页文本即 PASS；换模块才亮记 FAIL（原模块坏）。
证据：照片/视频文件名 + 万用表读数。未目视不得写 PASS。

---

## 收工

1. 回填第 1 节总表状态与证据列。
2. 所有 `FAIL` 项：记录回包原文与现象，回到对应提交排查（S10 最终验收前必须清零）。
3. 所有 `BLOCKED` 项：写明缺什么（工具 / 页面 / 配件），交派发方安排。
4. 本文件与 `evidence/manual-*.log` 一起提交，并在 `evidence/INDEX.md` 追加条目。
