# TIM2 资源争用记录：超声测距 vs 电机 PWM

> 记录日期：2026-11-19　芯片：STM32F103C8T6（72 MHz，可用 TIM1–TIM4）
> 用途：给模型库补契约条款、给检查器补规则；也可作为后续接手的背景。

## 1. 争用对象

| 项 | 内容 |
| --- | --- |
| 定时器 | **TIM2**（APB1，定时器时钟 72 MHz） |
| 同一颗定时器上的两个用途 | A：超声测距的时间戳源；B：电机双通道 PWM |
| 涉及的引脚 | A：PA11 TRIG / PA10 ECHO（不占用 TIM2 通道，只借 TIM2 计数器）；B：PA0 = TIM2_CH1、PA1 = TIM2_CH2 |

## 2. 两边分别怎么用

### A. 超声测距（HC-SR04）

- 配置：`PSC = 71` → 计数频率 **1 MHz**（1 µs/计数），`ARR = 0xFFFF` 自由运行，
  `CNT` 从 0 起一直跑，65.5 ms 回绕。
- 机制：上升沿/下降沿 EXTI 中断里读 `TIM2->CNT`，两值相减得到 ECHO 高电平宽度，
  再按 58 µs/cm 换算距离。
- 所以它要求：**1 MHz 计数、自由运行、周期尽量长**（量程 65 ms ≈ 11 m，实际模块 4 m）。

### B. 电机 PWM（DRV8833，IN1=PA0 / IN2=PA1）

- 配置：`PSC = 0`、`ARR = 3599` → 72 MHz / 3600 = **20 kHz**，占空比 0–100% 可调。
- 机制：CH1/CH2 输出比较 PWM，正转时 IN1 出 PWM、IN2 恒低；反转相反；两路都低为停止。
- 所以它要求：**固定 20 kHz 帧、ARR 固定**。

## 3. 为什么不能共存

TIM2 只有一套 `PSC/ARR/CNT`，两个用途必须共用同一时基，而两边的时基要求互斥：

| 若按 A 配置（PSC=71, ARR=65535） | 若按 B 配置（PSC=0, ARR=3599） |
| --- | --- |
| 电机 PWM 频率 = 72 MHz / (72 × 65536) ≈ **15.26 Hz**，落在音频段，电机每 65 ms 才换一次相，噪音大、低速抖动，且占空比分辨率只剩 1/65536 的时长 | 测距计数每 **50 µs** 回绕一次，ECHO 宽度上限 50 µs ≈ **0.86 cm**，量程直接失效 |

这不是初始化顺序或通道分配能绕开的问题：**同一 timer 的自由运行计时与固定周期 PWM
在硬件上互斥**。即使把测距改到输入捕获通道（CH3/CH4），也仍然共用 `PSC/ARR`，
结论不变。

## 4. 评估过但未采用的方案

| 方案 | 结论 |
| --- | --- |
| 测距改用 `DWT->CYCCNT`（72 MHz、32 位、59.65 s 回绕） | 技术上可行，能腾出 TIM2；本轮用户决定直接停用测距，不做迁移 |
| 换到 TIM1 / TIM3 / TIM4 | TIM1 的通道脚（PA8/PA9/PA10/PA11）已全部被编码器、超声、按键占用；TIM3 用于蜂鸣器、TIM4 用于舵机；PA0/PA1 在 F103C8 上只映射到 TIM2，没有第二选择 |
| 软件 PWM（GPIO + 定时器节拍翻转） | 占空比抖动与 CPU 占用不可控，不满足"占空比可调"的要求 |
| 电机只做方向、不做 PWM | 不能满足占空比需求 |

## 5. 本轮的处置

- **测距停用**：`DIST_TIME_BASE_READY = 0`，`DIST` 命令固定回 `DIST err=disabled`，
  OLED live 的自动测距静默跳过；TRIG/ECHO 引脚配置与 EXTI 边沿逻辑保留不删，
  重新启用前必须先确定独立计时源（见 §4 第一行）。
- **TIM2 整颗让给电机 PWM**：不再有任何代码写 `TIM2->PSC/ARR/CNT`。
- 相关代码：`src/port/stm32f1/pm_sonar_stm32f1.c`（`pm_sonar_init`/`pm_sonar_pulse`，S6 已从
  `main.c` 迁出）、`src/components/sonar_dev/sonar_dev.c`（`sonar_dev_start`）、
  `src/port/stm32f1/pm_motor_stm32f1.c`。

## 6. 建议检查器规则

1. 同一 timer 上出现"自由运行计数器"（`ARR` 为最大、`CNT` 只读）与"固定周期 PWM"
   （`ARR` 参与频率计算、写 `CCR`）两种配置时，报 **ERROR：时基互斥**。
2. 记录每个 `TIMx->PSC/ARR` 的写入者；同一寄存器出现两个以上模块的写入点，报 WARN。
3. 命令/驱动若声明"需要 µs 级时间戳"，必须绑定独立计时源，不得与 PWM 共用 timer。
4. 引脚复用检查：PA0/PA1 在 F103C8 上仅 TIM2_CH1/CH2，占用前先查 TIM2 是否已被计时类用途占用。

## 7. 证据

- 旧实现：`PATCHES.md` 测距相关小节（TIM2 1 MHz 自由运行、EXTI 读 CNT）。
- 本轮构建：Flash 25672 B / RAM 4864 B，`check_layering` 0 ERROR / 3 WARN。
- 停用后回归（待烧录）：`DIST` → `DIST err=disabled`，其余命令回包不变。
