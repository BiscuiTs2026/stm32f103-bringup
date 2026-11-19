# 技术债登记

因规范条款暂未满足或当次指令临时放宽而记账的条目。每条必须包含：日期、条款、位置、原因、计划修复轮次。

## 2026-11-19 检查器升锁 3.3.1 建立的存量清单

- 来源：上游检查器 **3.3.1**，提交 `7facd2b0039acd407d91c96ee043ef95dfc4b446`，
  已写入 `tools/CHECKER.lock`（`--version --lock` 校验一致）。
- 扫描命令与 `CI` 一致：`python3 tools/check_layering.py .`（**不传 `--glue-max-lines`**，默认阈值 200）。
- 结果：**0 ERROR / 24 WARN**；其中 3 条是 3.2.0 就有的端口层 ISR 钩子，21 条与 `src/app/main.c` 相关
  （20 条为 3.3.1 新增语义规则的命中，1 条是第 10 项由 ERROR 降级为 WARN 的行数兜底）。
- 处置：**不清零、不加 `arch-allow`**。下列条目按 `main.c` 拆分进度逐步消除，
  消除前保持 WARN 可见；CI 仍只以 ERROR 为失败判据。

| # | 规则 | 位置 | 命中对象 | 归属 / 原因 | 计划 |
|---|---|---|---|---|---|
| D0 | 第 10 项（行数兜底，WARN） | `src/app/main.c:1` | 全文件 1532 行（上限 200） | 3.3.1 起该规则降为 WARN，只作体积信号 | 随 S2–S10 拆分逐块下降，不设豁免 |
| D1 | G1 胶水层实现外设驱动 | `src/app/main.c:373` | `i2c1_bus_fix`，13 处寄存器访问 | I2C1 驱动未下沉到端口层 | 拆分完成后按外设分 2–3 轮下沉，每轮上板验 I2C |
| D2 | G1 | `src/app/main.c:418` | `i2c1_scan`，9 处 | 同上 | 同上 |
| D3 | G1 | `src/app/main.c:487` | `i2c1_read_after_trigger`，10 处 | 同上 | 同上 |
| D4 | G1 | `src/app/main.c:527` | `aht20_trigger`，5 处 | AHT20 触发未下沉 | 同上（I2C 轮） |
| D5 | G1 | `src/app/main.c:980` | `dist_start`，4 处 | 测距已停用，代码仍在胶水层 | 与 TIM2/测距重启用同轮处理，或随 S9 清理 |
| D6 | G1 | `src/app/main.c:1129` | `oled_i2c_wait`，3 处 | OLED I2C 等待未下沉 | 同上（I2C 轮） |
| D7 | G1 | `src/app/main.c:1155` | `oled_bus_write`，8 处 | OLED I2C 写未下沉 | 同上（I2C 轮） |
| D8 | 第 18 项（阻塞延时） | `src/app/main.c:387` | `pm_osal_delay_ms()` | 运行时等待未改非阻塞 | 随 S2 输出层/S9 调度块下沉 |
| D9 | 第 18 项 | `src/app/main.c:389` | `pm_osal_delay_ms()` | 同上 | 同上 |
| D10 | 第 18 项 | `src/app/main.c:392` | `pm_osal_delay_ms()` | 同上 | 同上 |
| D11 | 第 18 项 | `src/app/main.c:394` | `pm_osal_delay_ms()` | 同上 | 同上 |
| D12 | 第 18 项（忙等） | `src/app/main.c:448` | `while` 空转等硬件标志 | I2C 等待未下沉 | 同上（I2C 轮） |
| D13 | 第 18 项（忙等） | `src/app/main.c:523` | `while` 空转等硬件标志 | 同上 | 同上 |
| D14 | 第 18 项（忙等） | `src/app/main.c:1015` | `while` 空转等硬件标志 | 测距回波等待，已停用 | 随 D5 同轮 |
| D15 | 第 19 项（硬编码引脚） | `src/app/main.c:772` | `GPIO_PIN_4` | 引脚映射未归端口层 | 随 S9 清理迁移到端口层 |
| D16 | 第 19 项 | `src/app/main.c:774` | `GPIOA` | 同上 | 同上 |
| D17 | G3 胶水层多分支判定 | `src/app/main.c:1346` | `oled_screen_name`，5 个 switch 分支 | 页面名映射应下沉组件层 | 随 S8 OLED 块下沉 |
| D18 | G3 | `src/app/main.c:1388` | `oled_start_screen`，4 个 switch 分支 | 同上 | 同上 |
| D19 | 第 22 项（单函数 ≤ 80 行） | `src/app/main.c:1434` | `cmd_oled`，123 行 | 命令处理函数过长 | 随 S8 OLED 块拆出组件或改表驱动 |
| D20 | 第 21 项（字符串 else-if 命令链） | `src/app/main.c:1737` | `cmd_execute`，8 个分支（阈值 4） | 命令分发未改注册表 | 随 S10 分发块处理 |

### 附：这轮不修的边界

- 7 条 G1（D1–D7）**本轮不修**，等 `main.c` 拆分全部做完后单独开一轮，
  按外设分 2–3 轮下沉，每轮上板验 I2C。
- 其余 13 条随对应拆分步骤（S2–S10）自然消除；拆分过程中只搬不改，
  不允许为了消债在拆分轮里顺手改实现。
- 未登记任何 `arch-allow` 豁免；若后续确需豁免，必须在本表补一行并写明理由与撤销条件。
