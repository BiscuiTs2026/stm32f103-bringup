# S10：检查器 §8 十一条 glue 命中汇总（3.3.1 复扫）

复扫命令：`python tools/check_layering.py . --glue-max-lines 87`
结果：**0 ERROR / 3 WARN**（WARN 全部是 `src/port/stm32f1/pm_port_stm32f1.c` 的端口层 ISR 钩子），
第 18–21 项零命中。`src/app/main.c` 检查器口径 **87 行**（物理 156）。

`src/app/main.c` 硬事实（Python 精确计数，区分大小写）：
`GPIO_PIN_`=0、`GPIOA`=0、`HAL_`=0、`while`=0；`pm_osal_delay_ms` 仅 1 处，位于
`error_loop()` 的启动失败死循环（非外设时序）。

| # | 原现象（3.3.0 在 main.c 的命中） | 清除步骤 | 现状证据 |
|---|---|---|---|
| 1 | 胶水层阻塞延时：I2C 总线恢复位时序 | S4 | 位时序在 `src/port/stm32f1/pm_i2c1_stm32f1.c`；第 18 项零命中 |
| 2 | 同上 | S4 | 同上 |
| 3 | 同上 | S4 | 同上 |
| 4 | 同上 | S4 | 同上 |
| 5 | 忙等：I2C STOP 超时自旋 | S4 | STOP 等待在端口层；`main.c` 无 `while` |
| 6 | 忙等：AHT 读结束路径的 STOP 自旋 | S4 | 同上 |
| 7 | 硬编码引脚 `GPIO_PIN_4 \| GPIO_PIN_5` | S7 | `main.c` 中 `GPIO_PIN_`=0；引脚表在 `pm_adc1_stm32f1.c` |
| 8 | `HAL_GPIO_Init(GPIOA, &g)` 与 HAL 头泄漏 | S7 | `main.c` 中 `GPIOA`=0、`HAL_`=0；不再 include HAL |
| 9 | 忙等：HC-SR04 TRIG 10 µs 空转 | S6 | 脉冲在 `pm_sonar_stm32f1.c` 的 `pm_sonar_pulse()` |
| 10 | `cmd_oled` 123 行（上限 80） | S9 | 命令族在 `src/components/oled_ui/`；`main.c` 无 `cmd_oled` |
| 11 | `cmd_execute` 8 个字符串 `else-if` | S3/S10 | 分派改为 `pm_cmd_dispatch()` 遍历 `s_cmds`（`src/app/app_wiring.c`） |

## 证据文件

- `s10-layering.log`：上面的复扫原文（含 3 条 WARN 的路径与行号）。
- `s10-stale-scan-1.log` / `s10-stale-refs-1.txt`：修前清单（28 条 STALE）。
- `s10-stale-scan-2.log` / `s10-stale-refs.txt`：修后清单（**STALE=0**）。
- `split-s10-flash.log`：回读校验成功。
- `split-s10-board.log`：板端横幅 7 行、`DIST err=disabled`、`HELP` 15 行（与 s6b 逐行一致）、
  `OLED dump` 8 行、live 持续。
