# 技术债登记

因规范条款暂未满足或当次指令临时放宽而记账的条目。每条必须包含：日期、条款、位置、原因、计划修复轮次。

**2026-11-20 起："位置"一律按符号/文件定位，不再写行号**——行号随每次搬迁失效，符号不会。

## 2026-11-19 检查器升锁 3.3.1 建立的存量清单

- 来源：上游检查器 **3.3.1**，提交 `7facd2b0039acd407d91c96ee043ef95dfc4b446`，
  已写入 `tools/CHECKER.lock`（`--version --lock` 校验一致）。
- 建表时的扫描命令与 `CI` 一致：`python3 tools/check_layering.py .`（默认上限 200）。
- 建表时结果：**0 ERROR / 24 WARN**；其中 3 条是 3.2.0 就有的端口层 ISR 钩子，21 条与 `src/app/main.c` 相关
  （20 条为 3.3.1 新增语义规则的命中，1 条是第 10 项由 ERROR 降级为 WARN 的行数兜底）。
- 处置：不清零、不为真实越层加 `arch-allow`。下列条目按 `main.c` 拆分进度逐步消除，
  消除前保持 WARN 可见；CI 仍只以 ERROR 为失败判据。

### 2026-11-20（S10 收口）复核

- 命令 `python tools/check_layering.py . --glue-max-lines 86` → **0 ERROR / 3 WARN**，
  3 条均为 `src/port/stm32f1/pm_port_stm32f1.c` 的端口层 ISR 钩子（352/362/374），与拆分前逐条一致。
- `src/app/main.c` 检查器口径 **86 行**（S10 前 216，原始 1669；2026-11-20 复测 `--glue-max-lines 86` 为 0 ERROR，压到 85 时提示 86 行超限，CI 与本地闸门取 86，**无余量、再增一行即红**），胶水行数 WARN 已消失。
- 下表 D0–D19 全部消除；只剩 D21（行为偏差，非检查器命中）与 D22（`arch-allow` 登记）。

| # | 规则 | 位置（按符号） | 命中对象 | 归属 / 原因 | 状态 |
|---|---|---|---|---|---|
| D0 | 第 10 项（行数兜底，WARN） | `src/app/main.c`（全文件） | 检查器口径 **86 行**（S10 前 216，原始 1669；上限 200；2026-11-20 复测） | 3.3.1 起该规则降为 WARN，只作体积信号 | **已清除（S10）** |
| D1 | G1 胶水层实现外设驱动 | `src/port/stm32f1/pm_i2c1_stm32f1.c` | I2C1 总线修复的寄存器访问 | I2C1 驱动已下沉端口层 | **已清除（S4）** |
| D2 | G1 | `src/port/stm32f1/pm_i2c1_stm32f1.c` | 总线扫描的寄存器访问 | 同上 | **已清除（S4）** |
| D3 | G1 | `src/port/stm32f1/pm_i2c1_stm32f1.c` | AHT 触发后读的寄存器访问 | 同上 | **已清除（S4）** |
| D4 | G1 | `src/components/aht20_dev/aht20_dev.c` | AHT20 触发与重试 | 已下沉组件层 | **已清除（S5）** |
| D5 | G1 | `src/components/sonar_dev/sonar_dev.c` | 测距启动与作业推进 | 已下沉组件层 | **已清除（S6b）** |
| D6 | G1 | `src/components/oled_ch1116/oled_ch1116.c` | OLED I2C 等待 | 已下沉组件层 | **已清除（S3）** |
| D7 | G1 | `src/components/oled_ch1116/oled_ch1116.c` | OLED 帧写 | 同上 | **已清除（S3）** |
| D8 | 第 18 项（阻塞延时） | `src/port/stm32f1/pm_i2c1_stm32f1.c` | 总线等待里的 `pm_osal_delay_ms()` | 延时随 I2C 下沉，层内允许 | **已清除（S4）** |
| D9 | 第 18 项 | `src/port/stm32f1/pm_i2c1_stm32f1.c` | 同上 | 同上 | **已清除（S4）** |
| D10 | 第 18 项 | `src/components/oled_ch1116/oled_ch1116.c` | 帧间等待 | 同上 | **已清除（S3）** |
| D11 | 第 18 项 | `src/components/oled_ch1116/oled_ch1116.c` | 同上 | 同上 | **已清除（S3）** |
| D12 | 第 18 项（忙等） | `src/port/stm32f1/pm_i2c1_stm32f1.c` | 空转等硬件标志 | 同上 | **已清除（S4）** |
| D13 | 第 18 项（忙等） | `src/components/aht20_dev/aht20_dev.c` | 原 80 ms 转换等待 | 改为 `pm_osal_now_ms()` 计时，非阻塞 | **已清除（S5）** |
| D14 | 第 18 项（忙等） | `src/port/stm32f1/pm_sonar_stm32f1.c`（`pm_sonar_pulse()` 的 TRIG 高电平等待，第 50 行附近） | 空转凑 10 µs 的 TRIG 脉宽 | 忙等代码仍在，没有改成非阻塞；只是 S6 把文件从 glue 迁到 bsp 后，第 18 项（只扫胶水层）不再覆盖它；且 `DIST_TIME_BASE_READY` 为 0，`sonar_dev_start()` 在调用前就返回，该函数当前不可达 | **规则口径外、代码不可达**（非已修复）；重开测距时须换成非阻塞实现 |
| D15 | 第 19 项（硬编码引脚） | `src/port/stm32f1/pm_adc1_stm32f1.c` | `GPIO_PIN_4` | 引脚映射归端口层 | **已清除（S7）** |
| D16 | 第 19 项 | `src/port/stm32f1/pm_adc1_stm32f1.c` | `GPIOA` | 同上 | **已清除（S7）** |
| D17 | G3 胶水层多分支判定 | `src/components/oled_ui/oled_ui_page.c` | 页面名映射分支 | 已下沉组件层 | **已清除（S8）** |
| D18 | G3 | `src/components/oled_ui/oled_ui_job.c` | 屏幕启动分支 | 同上 | **已清除（S8）** |
| D19 | 第 22 项（单函数 ≤ 80 行） | `src/components/oled_ui/oled_ui_job.c`（`oled_ui_cmd`） | 原 123 行命令处理 | 已拆到组件层 | **已清除（S8/S9）** |
| D21 | S4 已知行为偏差（非检查器命中） | `src/port/stm32f1/pm_i2c1_stm32f1.c` `aht_trigger`/`aht_read` | `AHTS`（rst=1）触发后立即释放总线，80 ms 后的读是普通 START，不再重复 START | AHT20 要等命令被 STOP 终止才开始转换；固定单参数 `pm_i2c1_ops_t` 无法保留拆前"握总线 80 ms"的线序（派发方 2026-11-19 裁决保留接口） | **仍开放**；代码已标注 `repeated_start` 不上线、无假分支；若将来确需真·重复 START，另立任务改接口 |
| D22 | `arch-allow` 登记 | `src/components/adc_dev/adc_dev.h`（4 处原型）、`src/app/app_wiring.c`（1 处调用） | `adc_dev_boot`/`adc_dev_cmd` 等 | 检查器把 `adc_` 前缀的本工程组件函数误判为 SPL 调用（前缀正则），不涉及真实越层 | **待检查器补组件名前缀白名单后撤销** |
| D23 | 主循环实时性（非检查器命中） | `main()` 主循环、`keypad_dev_service()`、`oled_ui_service()` | 每 500 ms 约 98 ms 被 OLED 整页 I2C 占住，期间按键零采样 | 短于约 100–118 ms 的点按可能整段丢失（实测两轮各丢 1–2 记）；建议 EXTI 双边沿采样或整页写拆半插采样 | **待处理，不在本轮范围**（详见下节） |
| D24 | 诊断命令副作用说明（非检查器命中，非缺陷） | `oled_ch1116_service()`、`oled_ch1116_set_col_offset()`、`OLED col` 处理 | 每次页写只从当前 offset 起写 128 字节，改变 offset 后窗口外的 RAM 列保留旧内容 | **触发条件是主动用 `OLED col n` 切档**；缺省 col=2 时窗口盖满玻璃 RAM2..129，不可能出现；用户已确认不算缺陷 | **已消除（2026-11-20）**：新增 `oled_ch1116_clear_all()`，`OLED col` 切档后先清全 RAM 再重画，竖边不再出现（用户目视确认） |
| D25 | 检查器版本漂移（工具链，不属代码缺陷） | `tools/CHECKER.lock`、`tools/check_layering.py` | 本仓锁已升到上游 `4da1a77`（checker-version 3.4.0），2026-11-20 同步；此前锁在 `7facd2b`（3.3.1） | 3.3.1→3.4.0 只新增第 28 项 `feature_gate`；本仓 `.arch-roles` 无 `feature` 声明，该项报 0；复跑 `--glue-max-lines 86` 仍 `0 ERROR / 3 WARN`，3 条 WARN 与升级前逐条相同（`pm_port_stm32f1.c:352/362/374`） | **已升级（2026-11-20）**：锁、文件、README 同源；是否继续跟随上游按需再定 |

### D23 按键轮询被 OLED 整页 I2C 阻塞（2026-11-20 登记，待处理）

- 条款/归属：非检查器命中，属"主循环实时性"行为缺陷记录（AGENTS 7 验证纪律产物）。
- 位置（按符号）：`src/app/main.c` 的 `main()` 主循环；`keypad_dev_service()`（`src/components/keypad_dev/keypad_dev.c`）；
  `oled_ui_service()`（`src/components/oled_ui/oled_ui_job.c`）。
- 现象：主循环每轮调 `keypad_dev_service(now)` 之后才调 `oled_ui_service()`；后者整页写 I2C，
  实测整页传输 **`ms=98`**（`OLED live ok ms=98`，`oled_ui_job.c` 注明该值为真实 8 页传输时间），
  live 模式每 500 ms 触发一次 → **每 500 ms 约 98 ms 主循环被占住，期间按键采样为零**。
- 消抖门槛：`KEY_DEBOUNCE_MS 20u`（`keypad_dev.c` 顶部宏），判据是"电平保持 ≥20 ms"，
  且需下一次采样到来才能确认 → 保证可捕获的按压下限约 `20 + 98 ≈ 118 ms`。
- 后果：**短于约 100–118 ms 的点按可能整段落进 I2C 窗口而完全不被捕获**。实测两轮：
  M1-e 第二轮人报 21 / 固件 20（丢 1），第三轮人报 20 / 固件 18（丢 2）；两轮丢失都落在
  1.7–2.2× 中位数的加宽间隔内，且无 <20 ms 伪事件（详见 `evidence/MANUAL-CHECKS.md` M1-e 节）。
- 建议修法（任选其一，需排期）：
  1. 按键改 **EXTI 双边沿中断**采样：ISR 只给边沿打时间戳入队，消抖与计数判决仍在主循环做；
  2. 把 OLED 整页写**拆成两半**，在两半之间插入一次按键采样。
- 状态：**待处理，不在本轮范围**；M1-e 已按"设计内限制"判 PASS（带限制），本条目用于后续排期。

### D24 `OLED col` 切档的副作用说明与补强（2026-11-20，非缺陷）

- 条款/归属：非检查器命中，也**不是缺陷**——是 `OLED col` 诊断命令的副作用说明（M9 目视验收时发现，用户确认不算缺陷）。
- 位置（按符号）：`oled_ch1116_service()`（`src/components/oled_ch1116/oled_ch1116.c`，页写 128 字节处）、
  `oled_ch1116_set_col_offset()`（同文件）、`OLED col` 处理（`src/components/oled_ui/oled_ui_job.c` 的 `COL` 分支）。
- 机制：每次页写都把列地址设到**当前** `col_offset`，随后固定写 **128 字节**；因此写入窗口是
  `[offset, offset+127]`，窗口左右两侧的 RAM 列永远不被清除。固件没有 132 列全 RAM 的清除路径。
  玻璃可见窗口是 RAM 2..129（`oled_ch1116.c` 的 `build_frame_page()` 注释），所以：
  - offset=2（缺省）时窗口正好盖满玻璃，**不可能出现残留**；
  - offset=4 时只覆盖 RAM 4..131，RAM 2..3 保持上一次内容；
  - offset=0 时只覆盖 RAM 0..127，RAM 128..129 保持上一次内容。
- 触发条件与现象：主动执行 `OLED col 2`（画 frame，左边框落在 RAM2）→ `OLED col 3`（RAM3 再落一列）→
  `OLED col 4` → `OLED live on`（live 页从 RAM4 起写）时，玻璃最左 2 px 会保留 frame 的边框竖线。
  2026-11-20 10:56:19 照片即此形态（左侧 2 px 竖线紧贴文字，8 行贯通）。
- 边界：**只在主动用过 `OLED col` 诊断之后出现**；缺省 offset=2 时窗口盖满玻璃 RAM2..129，不可能出现，
  正常上电运行不受影响。另：`OLED col 4` 下右边框落在 RAM131（玻璃外），右边框必然看不见，属同一根因。
- 补强方式（已实施，原列两条，取第 1 条）：
  1. `OLED col` 切档后先推两遍全黑页（offset 0 与 offset 4 各一次，合起来覆盖 RAM 0..131），再重绘目标画面；
  2. 让驱动支持 132 字节页写，一次清全 RAM。
- 文档已改（2026-11-20）：`evidence/MANUAL-CHECKS.md` M9 第 4 步原判据"col 0 与 col 4
  两极值首字符不贴边、末字符不出界"不成立（col 4 右边框必失、col 0 左边缘不在窗口内），
  有效 A/B 范围实际只有 0..2 附近。该步已按此改写为"0..2 为有效档、极端档只比左右位移
  方向"；当时的实测记录与照片文件名保持原样，只在记录旁追加了更正说明。
- **补强（2026-11-20）**：采用方式 1。新增 `oled_ch1116_clear_all()`（`src/components/oled_ch1116/oled_ch1116.c`），
  按 offset 0 与 offset 4 各推 8 页全黑，覆盖 RAM 0..131；`OLED col` 分支
  （`src/components/oled_ui/oled_ui_job.c` 的 `COL`）在 `oled_ch1116_set_col_offset()` 之后、重画之前调用它。
  不新增 job 状态、不改 `oled_ch1116_service()`，运行中的作业不受影响（它随后按新 offset 继续写，被子命令的重画覆盖）。
- 补强验证（2026-11-20）：编译通过（Flash 29096 B / RAM 5312 B）；3.3.1 分层检查 `0 ERROR / 3 WARN`（仍是原 3 条端口层 ISR 钩子）；
  `STM32_Programmer_CLI -v` 回读 `Download verified successfully`；
  `evidence/d24-col-clear.log`：`col 2 / 3 / 4 / show / dump / col 2` 全部 `ok`、`af=0 berr=0 to=0`、无 `err=io`，
  且 `OLED col` 响应从约 15 ms 变为 **205–206 ms**（3 次实测：205 / 206 / 206），与 16 页 ×12 ms 的清屏开销吻合。
- 目视确认（2026-11-20）：用户按 `OLED col 2 → OLED frame → OLED col 3 → OLED col 4 → OLED show`
  复核，确认最左侧贯 8 行的竖边**已消失**；`evidence/MANUAL-CHECKS.md` M9 按 PASS 收项。

### 附：边界与撤销条件

- D21 是行为记录而非告警，保持开放直到接口支持真·重复 START。
- D22 是本表唯一登记的 `arch-allow`；理由仅为前缀误判，撤销条件是检查器识别组件名前缀，
  或组件改名避开 `adc_` 前缀。新增任何 `arch-allow` 必须在本表补一行并写明理由与撤销条件。
- 检查器行数规则（第 10 项）不再靠 `--glue-max-lines` 放水：CI 与本地已按实测 **86 行**落闸门 **86**（无余量，再增一行即红），只降不升。

## 2026-11-20 `DIST` 读数差异复查（~99 cm → ~6 cm，待现场确认遮挡物）

**现象**：2026-11-19 的 TIM2 时基版本在同一块板上稳定读到 `DIST cm=99.0 us=5741`
（`dist-probe-20261119-*.log`）；2026-11-20 的 `PM_ENABLE_SONAR=ON` + DWT 时基版本
连续 20+ 次读到 `cm=6.2/7.1 us=358/414`、复测 `cm=5.9 us=340`，无 timeout。
两数相差约 16 倍，必须逐项排除固件侧原因，避免被误判为退化。

逐项排除：

1. **时基（已排除）**：SWD 在板运行中两次读 `DWT->CYCCNT`（0xE0001004）得
   43,164,435 与 719,847,614，差 676,683,179；按 72 MHz 折算 9.4 s，与 10 s 睡眠
   + 连接停顿吻合。若是 4.5 MHz 之类的时钟，同一间隔只会涨约 42 M。换算常数只有
   `SystemCoreClock/1000000 = 72` 一处，不存在 16 倍缩放。
   CYCCNT 59.65 s 回绕也不影响：一次回波宽 < 25 ms，`uint16` 截断在模 65536 下
   对 < 65.5 ms 的差值精确。
2. **换算（已排除）**：`sonar_dev.c` 的 `cm10 = (us*10 + 29) / 58` 未改；本轮对该
   文件只改了 `DIST_TIME_BASE_READY` 的条件编译与注释。
3. **引脚（已排除）**：TRIG=PA11、ECHO=PA10、`AFIO->EXTICR[2]` 的 EXTI10→PA、
   EXTI 双边沿、TRIG 推挽/ECHO 浮空，均与 TIM2 版逐字相同（`git diff eba1e16^`）。
4. **触发脉宽（已排除）**：旧版 10 个 TIM2 计数 @1 MHz = 10 µs；新版 10×72 个
   CYCCNT = 10 µs。在第 1 条成立的前提下两者都是 10 µs。
5. **回波捕获方式（未完全排除）**：ISR 结构相同（清 PR → 读 IDR 判升降 → 记时间），
   只换了时间戳来源；旧版 `dist-probe` 在同一硬件上记到 `e=2 d=0 o=0`（每次一升一降）。
   新构建没有边沿计数器，无法再次证明没有多脉冲吞并；间接证据是 20+ 次读数稳定在
   5.8–7.1 cm、无 timeout。若现场确认无近处遮挡物，下一步加边沿计数并用逻辑分析仪量
   ECHO 宽度复测。
6. **中断优先级 0→5（已排除）**：只改变 ISR 延迟，且上升沿/下降沿被延迟的程度相同，
   按 58 µs/cm 折算误差 < 0.1 cm。

**结论**：固件侧可复现的差异源已逐项排除，剩余最可能的原因是物理遮挡物或模块近场
自反射。**待现场确认遮挡物**；确认前 `DIST` 的绝对值不得当作已验收。
