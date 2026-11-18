# 第三方代码说明

本目录只包含编译所需的最小集合，文件内容未做修改。

| 目录 | 来源 | 版本 | 许可 |
| --- | --- | --- | --- |
| `cmsis/include` | CMSIS Core（ARM） | 随 STM32CubeF1 1.8.0 分发 | Apache License 2.0 |
| `cmsis/device` | CMSIS Device STM32F1xx | 同上 | Apache License 2.0 |
| `stm32f1xx_hal` | STM32F1 HAL Driver | 1.1.10（CubeF1 1.8.0） | BSD-3-Clause |

完整许可矩阵见 [ST_LICENSE.md](ST_LICENSE.md)。

未包含的内容：

- ST 的链接脚本模板（其条款禁止再分发，本工程使用自行编写的 `platform/stm32f103/stm32f103c8.ld`）；
- 未使用的 HAL 模块（SPI/I2C/TIM/ADC 等，按需在第二阶段加入）。

替换或升级 HAL 时，请同步更新上表与 `src/port/stm32f1/` 的适配代码。
