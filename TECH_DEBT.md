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

- 命令 `python tools/check_layering.py . --glue-max-lines 87` → **0 ERROR / 3 WARN**，
  3 条均为 `src/port/stm32f1/pm_port_stm32f1.c` 的端口层 ISR 钩子（352/362/374），与拆分前逐条一致。
- `src/app/main.c` 检查器口径 **87 行**（S10 前 216，原始 1669），胶水行数 WARN 已消失。
- 下表 D0–D19 全部消除；只剩 D21（行为偏差，非检查器命中）与 D22（`arch-allow` 登记）。

| # | 规则 | 位置（按符号） | 命中对象 | 归属 / 原因 | 状态 |
|---|---|---|---|---|---|
| D0 | 第 10 项（行数兜底，WARN） | `src/app/main.c`（全文件） | 检查器口径 87 行（S10 前 216，原始 1669；上限 200） | 3.3.1 起该规则降为 WARN，只作体积信号 | **已清除（S10）** |
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
| D14 | 第 18 项（忙等） | `src/port/stm32f1/pm_sonar_stm32f1.c` | 回波等待 | 测距已停用（`DIST_TIME_BASE_READY` 0） | **已清除（S6）** |
| D15 | 第 19 项（硬编码引脚） | `src/port/stm32f1/pm_adc1_stm32f1.c` | `GPIO_PIN_4` | 引脚映射归端口层 | **已清除（S7）** |
| D16 | 第 19 项 | `src/port/stm32f1/pm_adc1_stm32f1.c` | `GPIOA` | 同上 | **已清除（S7）** |
| D17 | G3 胶水层多分支判定 | `src/components/oled_ui/oled_ui_page.c` | 页面名映射分支 | 已下沉组件层 | **已清除（S8）** |
| D18 | G3 | `src/components/oled_ui/oled_ui_job.c` | 屏幕启动分支 | 同上 | **已清除（S8）** |
| D19 | 第 22 项（单函数 ≤ 80 行） | `src/components/oled_ui/oled_ui_job.c`（`oled_ui_cmd`） | 原 123 行命令处理 | 已拆到组件层 | **已清除（S8/S9）** |
| D21 | S4 已知行为偏差（非检查器命中） | `src/port/stm32f1/pm_i2c1_stm32f1.c` `aht_trigger`/`aht_read` | `AHTS`（rst=1）触发后立即释放总线，80 ms 后的读是普通 START，不再重复 START | AHT20 要等命令被 STOP 终止才开始转换；固定单参数 `pm_i2c1_ops_t` 无法保留拆前"握总线 80 ms"的线序（派发方 2026-11-19 裁决保留接口） | **仍开放**；代码已标注 `repeated_start` 不上线、无假分支；若将来确需真·重复 START，另立任务改接口 |
| D22 | `arch-allow` 登记 | `src/components/adc_dev/adc_dev.h`（4 处原型）、`src/app/app_wiring.c`（1 处调用） | `adc_dev_boot`/`adc_dev_cmd` 等 | 检查器把 `adc_` 前缀的本工程组件函数误判为 SPL 调用（前缀正则），不涉及真实越层 | **待检查器补组件名前缀白名单后撤销** |

### 附：边界与撤销条件

- D21 是行为记录而非告警，保持开放直到接口支持真·重复 START。
- D22 是本表唯一登记的 `arch-allow`；理由仅为前缀误判，撤销条件是检查器识别组件名前缀，
  或组件改名避开 `adc_` 前缀。新增任何 `arch-allow` 必须在本表补一行并写明理由与撤销条件。
- 检查器行数规则（第 10 项）不再靠 `--glue-max-lines` 放水：CI 已按实测 87 行落闸门，只降不升。
