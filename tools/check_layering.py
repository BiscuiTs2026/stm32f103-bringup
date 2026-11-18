#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# 本文件是拷贝，不是本仓原创代码。代码部分逐字未改，只在文件头加了这段来源注释
# （所以下面的 SHA-256 是"拷来那一刻"的原文校验值，不是本文件现在的校验值）。
#
# 来源仓 / skill：firmware-four-layer-architecture（四层架构检查器 v3）
#   源路径（本地副本）：
#     d:/All the work/TRAEwork/AI-STM32F1-1/2026-11-18/repo-hardening/work/
#     firmware-four-layer-architecture/skill/scripts/check_layering.py
#   来源提交哈希：无。该 skill 目录不是独立 git 仓库；工程根仓
#     HEAD 63ad2f6e10a8a946d31036e491282d160555ec4d 也不跟踪它
#     （根 .gitignore 的 `2026-11-18/*/work/` 把它排除了），
#     所以不存在可引用的提交哈希——这里不编造。
#   取用校验：SHA-256 2D3C638DB71536F4502253018A3594325EE65BDA97394FA4B1710E35E14132A8
#   取用日期：2026-11-18
#
# 升级方式：整文件覆盖本文件，重新记录上面的 SHA-256 与日期，
#   然后 `python tools/check_layering.py . --glue-max-lines 1280` 复跑，
#   确认 ERROR 仍为 0、WARN 变化可解释（详见 README.md「静态分层检查」）。
# ---------------------------------------------------------------------------
"""check_layering.py — 四层架构启发式静态检查（v3）。

用法：
    python3 check_layering.py <工程根目录> [选项]

选项：
    --roles PATH        角色清单文件，默认在工程根目录找 .arch-roles / arch-roles.txt
    --glue-max-lines N  入口胶水文件行数上限，默认 200
    --max-per-file N    未分层文件每文件最多提示条数，默认 5
    --no-linker         跳过链接脚本固定地址段检查

两类工程形态都能扫：
    目录模式：路径里含 bsp/ osal/ components/ app/ 即按目录判层，无需任何配置。
    清单模式：工程是扁平 src/ 时，用角色清单把文件映射到层，例如
              src/i2c_soft.* = bsp
              src/telemetry.* = components
              src/main*.c   = glue

角色：
    bsp / osal / components / app —— 四层。
    common —— 纯类型与常量（err_t、公共枚举），任何层可包含它，它不参与依赖排序。
    glue —— 入口胶水（main.c、startup_*、*_irq.c 等），允许碰寄存器与 HAL，
            但只允许做初始化与中断分发，行数超限即判为"业务逻辑上移失败"。

检查项：
     1. 越层调用平台 API（app/ components/ 里出现 HAL/LL/SPL/RTOS、寄存器、SysTick/SCB、
        RCC->APBxENR 或裸地址 MMIO）→ ERROR
     2. 平台头泄漏（非 bsp 层的 .c/.h 出现芯片头或 RTOS 头）→ ERROR
     3. 厂商类型泄漏（非 bsp 层的 .h 签名与 .c 代码里出现厂商类型）→ ERROR
     4. 反向依赖（下层 #include 上层头、本地 extern 声明上层函数、直接调用上层函数）→ ERROR；
        bsp 包含 osal 头放行（符号级限制见第 17 项）；bsp 包含 components 的仅类型/钩子头 → WARN；
        bsp 调用 components 的 *_hw_* 钩子 → WARN；函数原型不计为调用
     5. 跨模块全局变量（非 bsp 层 .c 文件作用域可写变量）→ ERROR
     6. extern 跨模块变量（非 bsp 层 .c 与 .h 里的 extern 变量声明）→ ERROR
     7. 决策型 if 嵌套超过 3 层 → ERROR（单分支守卫不计，避免字模/像素循环误报）
     8. 未分层文件的平台调用 → WARN
     9. APP 直调 BSP 或越权调用 OSAL → ERROR
    10. 入口胶水层超过 --glue-max-lines → ERROR
    11. ISR 内调用阻塞/格式化/动态内存函数 → ERROR（含 SysTick_Handler 等 CMSIS 内核中断）
    12. 阻塞延时：bsp 之外的 HAL_Delay → ERROR；app/组件层裸 delay_ms 等 → WARN
    13. 固定地址段：同址、未 4 字节对齐、固定段内使用通配符、C 段未在 .ld 显式定义 → ERROR；
        声明了长度（/* size: 0xNN */）时区间重叠 → ERROR；未标 (NOLOAD) → WARN
    14. 宏间接越层（上层使用下层定义的硬件宏，或上层本地宏展开到下层调用/寄存器）→ ERROR
    15. 唯一入口：出现第二个 main.c → ERROR
    16. 工程完全没有分层目录或角色清单 → ERROR（可用 --allow-unlayered 显式关闭）
    17. BSP 越权调用 OSAL → ERROR（仅允许 *critical_enter / *critical_exit / *tick_inc）

豁免：
    - 行尾 `// arch-allow` 跳过该行全部检查；
    - 行尾 `// arch-allow:snapshot` 额外允许该行的全局变量声明（固定地址快照块）；
    - `SystemCoreClock` 属 CMSIS 必需符号，默认豁免。

退出码：0 = 无 ERROR；1 = 存在 ERROR（含"没有任何分层信息"）；2 = 用法或路径错误。
"""
from __future__ import annotations

import argparse
import fnmatch
import os
import re
import sys

ROLES = ("bsp", "osal", "components", "app", "glue", "common")
LAYER_DEPTH = {"common": -1, "bsp": 0, "osal": 1, "components": 2, "app": 3}
LAYERS = set(LAYER_DEPTH)
UPPER_LAYERS = {"app", "components"}
NON_BSP_LAYERS = {"app", "components", "osal", "common"}
APP_OSAL_ALLOWED = {"osal_sleep_ms", "osal_tick_ms", "osal_now_ms"}
BEGINNER_FILES = ("main.c", "main.cpp")

# 入口胶水文件名特征：这些文件允许碰寄存器和 HAL
GLUE_NAME = re.compile(r"(^main\.c$|^startup_.*|^system_.*|.*_it\.c$|.*_irq\.c$|.*_irq_handler\.c$)")

CMSIS_WHITELIST = {"SystemCoreClock", "uwTick", "uwTickPrio", "uwTickFreq"}

ROLE_FILE_NAMES = (".arch-roles", "arch-roles.txt")

PLATFORM_HEADER = re.compile(
    r'#\s*include\s*[<"]('
    r'stm32\w*_hal\.h|stm32\w+xx\.h|cmsis_os2?\.h|FreeRTOSConfig\.h|FreeRTOS\.h'
    r'|task\.h|queue\.h|semphr\.h|timers\.h|event_groups\.h|stream_buffer\.h|portmacro\.h'
    r'|rtthread\.h|gd32\w*\.h|nrfx?_\w*\.h|ti_msp_dl_config\.h|msp430\w*\.h'
    r')'
)

VENDOR_TYPE = re.compile(
    r"\b(SPI_HandleTypeDef|GPIO_TypeDef|TIM_HandleTypeDef|UART_HandleTypeDef|ADC_HandleTypeDef"
    r"|I2C_HandleTypeDef|DMA_HandleTypeDef|CAN_HandleTypeDef|RTC_HandleTypeDef"
    r"|HAL_\w+_TypeDef|I2C_TypeDef|USART_TypeDef|SPI_TypeDef|TIM_TypeDef|ADC_TypeDef"
    r"|nrfx_\w+_t|dl_\w+)\b"
)

HAL_CALL = re.compile(r"\b(?:__)?HAL_(?!Delay)[A-Za-z0-9_]+\s*\(")
LL_CALL = re.compile(r"\b(?:__)?LL_[A-Za-z0-9_]+\s*\(")
SPL_CALL = re.compile(
    r"\b(?:gpio|rcu|usart|spi|i2c|adc|tim|dma|exti|crc|rtc|pmu|fwdgt|wwdgt|bkp|can|i2s|sdio|usbd)"
    r"_[a-z][a-z0-9_]*\s*\("
)
RTOS_CALL = re.compile(
    r"\b(vTaskDelay|vTaskDelayUntil|vTaskSuspend|vTaskStartScheduler|osDelay|osDelayUntil"
    r"|xSemaphoreTake|xSemaphoreGive|xQueueReceive|xQueueSend|xQueueReceiveFromISR"
    r"|xQueueSendFromISR|taskENTER_CRITICAL|taskEXIT_CRITICAL|portENTER_CRITICAL"
    r"|portEXIT_CRITICAL|ulTaskNotifyTake)\s*\("
)

REG_ACCESS = re.compile(
    r"->\s*(ODR|IDR|MODER|OTYPER|OSPEEDR|PUPDR|AFR[HL]?|BSRR|BRR|CR[HL]?|SR|DR|EGR"
    r"|CCR|ARR|PSC|CNT|DIER|SMCR|CFGR|PLLCFGR|BDTR|RCR"
    r"|APB[12]ENR|AHBENR|VAL|LOAD|CALIB|CTRL|VTOR|AIRCR|SCR|CCR2?|CPUID|ICSR"
    r"|SHPR[0-9]?|MSP|PSP|PRIMASK|FAULTMASK|BASEPRI|CONTROL|FPCCR|FPCAR|FPDSCR)\d?\b"
)

# 裸地址 MMIO：*(volatile uint32_t *)0x4001080C 之类的直接寄存器访问
MMIO_CAST = re.compile(
    r"\(\s*(?:volatile\s+)?(?:const\s+)?"
    r"(?:u?int(?:8|16|32|64)_t|uintptr_t|unsigned\s+(?:int|long|char))"
    r"\s*\*\s*\)\s*0x[0-9A-Fa-f]{5,}\b"
)

SCALAR = (
    r"(?:unsigned\s+|signed\s+|const\s+|volatile\s+)*"
    r"(?:u?int(?:8|16|32|64)_t|char|short|int|long|float|double|bool|size_t"
    r"|[A-Za-z_]\w*_t|[A-Za-z_]\w*TypeDef)"
)
GLOBAL_VAR = re.compile(
    r"^(?:extern\s+)?(?:static\s+)?"
    r"(" + SCALAR + r")\s+\**\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;\s*$"
)
EXTERN_VAR = re.compile(r"^extern\s+(?!.*\()\w.*;\s*$")

BSP_PREFIX_CALL = re.compile(r"\b(bsp|osal)_[a-z][a-z0-9_]*\s*\(")

# BSP 允许使用的 OSAL 符号：仅临界区进入/退出与 tick 递增（issue #4）
OSAL_CALL = re.compile(r"\b((?:[A-Za-z_]\w*_)?osal_[A-Za-z_]\w*)\s*\(")
BSP_OSAL_ALLOWED = ("critical_enter", "critical_exit", "tick_inc")

# ISR 内禁止出现的调用
ISR_BLOCKING = re.compile(
    r"\b(HAL_Delay|osDelay|osDelayUntil|vTaskDelay|vTaskDelayUntil"
    r"|xQueueReceive|xSemaphoreTake|osMutexWait"
    r"|delay_ms|delay_us|mdelay|udelay"
    r"|printf|fprintf|sprintf|snprintf|vsnprintf"
    r"|malloc|calloc|realloc|free)\s*\("
)
ISR_NAME = re.compile(
    r"(IRQHandler$|_isr$|_ISR$|^isr_|_isr_|_IRQHandler"
    r"|^SysTick_Handler$|^NMI_Handler$|^HardFault_Handler$|^MemManage_Handler$"
    r"|^BusFault_Handler$|^UsageFault_Handler$|^SVC_Handler$|^DebugMon_Handler$"
    r"|^PendSV_Handler$)"
)

EXTERN_FUNC = re.compile(r"\bextern\s+[^;{}()]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;")
DEFINE_LINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")
CALL_NAME = re.compile(r"\b([A-Za-z_]\w*)\s*\(")

# 只有函数原型的行：不是调用（避免把上层函数声明误判成反向调用）
DECL_ONLY_LINE = re.compile(
    r"^\s*(?!(?:return|if|else|for|while|switch|do|case|default|goto|sizeof)\b)"
    r"(?:[A-Za-z_]\w*[\s*]+)+[A-Za-z_]\w*\s*\([^;{}]*\)\s*;\s*$"
)

SECTION_ATTR = re.compile(r'__attribute__\s*\(\s*\(\s*section\s*\(\s*"([^"]+)"')
LD_SECTION = re.compile(
    r"(?m)^[ \t]*(\.[A-Za-z0-9_.]+)[ \t]+(0x[0-9A-Fa-f]{6,})[ \t]*(\(NOLOAD\))?[ \t]*:[ \t\r\n]*\{"
)
LD_WILDCARD = re.compile(r"\*\(\s*(\.[A-Za-z0-9_.*?\[\]]+)\s*\)")
LD_SIZE_DIRECTIVE = re.compile(r"\.\s*=\s*\.\s*\+\s*(0x[0-9A-Fa-f]+|\d+)")
LD_SIZE_COMMENT = re.compile(r"size\s*[:=]\s*(0x[0-9A-Fa-f]+|\d+)", re.I)
LD_RANGE_COMMENT = re.compile(r"0x([0-9A-Fa-f]{6,})\s*\.\.\s*0x([0-9A-Fa-f]{6,})")
SECTION_NAME_OK = re.compile(r"^\.(data|bss|rodata|text)\b")

HAL_DELAY = re.compile(r"\bHAL_Delay\s*\(")
SOFT_DELAY = re.compile(r"\b(?:delay_ms|delay_us|mdelay|udelay|osal_sleep_ms)\s*\(")

SKIP_DIRS = {
    ".git", "build", "Build", "Debug", "Release", "Objects", "Listings", ".vscode",
    "node_modules", "Drivers", "Middlewares", "CMSIS", "tests", ".trae-html-share-packages",
}


# ---------------------------------------------------------------- 文本处理


def _mask_one_line(line: str):
    """屏蔽单行里的块注释、行注释与字符串内容。返回 (结果, 是否进入块注释)。"""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if line.startswith("/*", i):
            j = line.find("*/", i + 2)
            if j >= 0:
                out.append(" " * (j + 2 - i))
                i = j + 2
            else:
                out.append(" " * (n - i))
                return "".join(out), True
        elif line.startswith("//", i):
            out.append(" " * (n - i))
            i = n
        elif line[i] in ('"', "'"):
            quote = line[i]
            out.append(quote)
            j = i + 1
            while j < n and line[j] != quote:
                out.append(" ")
                j += 1
            if j < n:
                out.append(quote)
                j += 1
            i = j
        else:
            out.append(line[i])
            i += 1
    return "".join(out), False


def mask_comments(text: str) -> str:
    """把注释与字符串内容替换为空格，保留行数。#include 行保留原始引号内容。"""
    out = []
    in_block = False
    for line in text.splitlines():
        if in_block:
            end = line.find("*/")
            if end >= 0:
                in_block = False
                out.append(" " * (end + 2) + _mask_one_line(line[end + 2:])[0])
            else:
                out.append(" " * len(line))
            continue
        if line.lstrip().startswith("#include"):
            cut = line.find("//")
            out.append(line[:cut] if cut >= 0 else line)
            continue
        masked, in_block = _mask_one_line(line)
        out.append(masked)
    return "\n".join(out)


# ---------------------------------------------------------------- 角色解析


def load_roles(root: str, roles_path: str | None):
    """返回 [(pattern, role)]，没有清单则返回 None。"""
    path = roles_path
    if path is None:
        for name in ROLE_FILE_NAMES:
            candidate = os.path.join(root, name)
            if os.path.isfile(candidate):
                path = candidate
                break
    if path is None:
        return None
    if not os.path.isfile(path):
        raise SystemExit(f"角色清单不存在：{path}")
    rules = []
    with open(path, encoding="utf-8", errors="ignore") as fh:
        for no, line in enumerate(fh, 1):
            raw = line.split("#", 1)[0].strip()
            if not raw or "=" not in raw:
                continue
            pattern, role = raw.rsplit("=", 1)
            pattern, role = pattern.strip(), role.strip()
            if role not in ROLES:
                raise SystemExit(f"{path}:{no} 未知角色 {role!r}，可用：{', '.join(ROLES)}")
            rules.append((pattern.replace("\\", "/"), role))
    return rules


def match_role(rel: str, rules) -> str:
    base = rel.rsplit("/", 1)[-1]
    for pattern, role in rules:
        target = base if "/" not in pattern else rel
        if fnmatch.fnmatch(target, pattern):
            return role
    return ""


def dir_role(rel: str) -> str:
    for part in rel.split("/")[:-1]:
        if part in LAYERS:
            return part
    return ""


def is_glue(rel: str) -> bool:
    return bool(GLUE_NAME.match(rel.rsplit("/", 1)[-1]))


def resolve_layer(rel: str, rules) -> str:
    layer = match_role(rel, rules) if rules else ""
    if not layer:
        layer = dir_role(rel)
    if not layer and is_glue(rel):
        layer = "glue"
    return layer


def iter_c_files(root: str):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if fn.endswith((".c", ".h")):
                yield os.path.join(dirpath, fn)


def read_source(path: str):
    with open(path, encoding="utf-8", errors="ignore") as fh:
        raw = fh.read()
    return raw, mask_comments(raw), raw.splitlines()


def header_is_type_only(path: str) -> bool:
    """判断头文件是否只含类型与函数原型（可被端口层包含，issue #3）。

    含平台头、寄存器访问、厂商类型或函数定义的头都不算“仅类型头”。
    """
    raw, masked, _ = read_source(path)
    if PLATFORM_HEADER.search(raw):
        return False
    if REG_ACCESS.search(masked) or MMIO_CAST.search(masked) or VENDOR_TYPE.search(masked):
        return False
    if next(iter_functions(masked), None) is not None:
        return False
    return True


# ---------------------------------------------------------------- 结构分析


def _skip_ws(code: str, i: int) -> int:
    while i < len(code) and code[i] in " \t\r\n":
        i += 1
    return i


def check_nesting(code: str):
    """只统计"决策型" if（带 else 分支）的嵌套；单分支守卫不计。

    统计全部 if 会把字模渲染、像素扫描这类"双层 for + 单分支 if"判成 4 层，
    在真实工程上制造大量误报。这里改成只认真正的决策金字塔。
    """
    pairs = {}
    stack = []
    for i, ch in enumerate(code):
        if ch == "{":
            stack.append(i)
        elif ch == "}" and stack:
            pairs[stack.pop()] = i

    blocks = []
    for m in re.finditer(r"\bif\b", code):
        j = _skip_ws(code, m.end())
        if j >= len(code) or code[j] != "(":
            continue
        depth = 0
        k = j
        while k < len(code):
            if code[k] == "(":
                depth += 1
            elif code[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        k = _skip_ws(code, k + 1)
        if k >= len(code) or code[k] != "{" or k not in pairs:
            continue
        if code.startswith("else", _skip_ws(code, pairs[k] + 1)):
            blocks.append((k, pairs[k]))

    issues = []
    for open_at, close_at in blocks:
        depth = 1 + sum(1 for o, c in blocks if o != open_at and o < open_at < c)
        if depth > 3:
            issues.append((code.count("\n", 0, open_at) + 1,
                           f"决策型 if 嵌套 {depth} 层（上限 3，单分支守卫不计）"))
    return issues


def find_global_vars(masked: str, raw_lines):
    """文件作用域（花括号深度 0）的可写变量声明。"""
    issues = []
    depth = 0
    for no, line in enumerate(masked.splitlines(), 1):
        code = line.strip()
        opens, closes = line.count("{"), line.count("}")
        raw = raw_lines[no - 1] if no <= len(raw_lines) else ""
        if depth == 0 and code and code.endswith(";") and not code.startswith("#"):
            if (
                "(" in code
                or ")" in code
                or code.startswith("extern")
                or code.startswith("static")
                or code.startswith("const")
                or "__attribute__" in raw
            ):
                depth += opens - closes
                continue
            m = GLOBAL_VAR.match(code)
            if m and m.group(2) not in CMSIS_WHITELIST:
                issues.append((no, f"跨模块全局变量：{m.group(2)}（改走参数中心/只读快照，或改 static）"))
        depth += opens - closes
    return issues


def find_extern_vars(masked: str, raw_lines):
    issues = []
    depth = 0
    for no, line in enumerate(masked.splitlines(), 1):
        code = line.strip()
        raw = raw_lines[no - 1] if no <= len(raw_lines) else ""
        if depth == 0 and code and EXTERN_VAR.match(code) and "__attribute__" not in raw:
            name = re.findall(r"[A-Za-z_]\w*", code)
            if name and name[-1] in CMSIS_WHITELIST:
                depth += line.count("{") - line.count("}")
                continue
            issues.append((no, f"extern 跨模块变量：{code[:40]}"))
        depth += line.count("{") - line.count("}")
    return issues


FUNC_DEF = re.compile(
    r"(?m)^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{"
)


def iter_functions(masked: str):
    """粗略提取函数定义：(name, 定义行号, 函数体文本, 函数体起始行号)。"""
    for m in FUNC_DEF.finditer(masked):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        start = m.end() - 1
        depth = 0
        i = start
        while i < len(masked):
            if masked[i] == "{":
                depth += 1
            elif masked[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = masked[start:i + 1]
        yield name, masked.count("\n", 0, m.start()) + 1, body, masked.count("\n", 0, start) + 1


def check_isr(masked: str):
    """ISR 内调用阻塞 / 格式化 / 动态内存。"""
    issues = []
    for name, def_line, body, body_line in iter_functions(masked):
        if not ISR_NAME.search(name):
            continue
        for m in ISR_BLOCKING.finditer(body):
            offset = body_line + body.count("\n", 0, m.start())
            issues.append((offset, f"ISR {name} 内调用 {m.group(1)}()（只允许置标志/发消息/清中断）"))
    return issues


def check_glue_length(raw: str, masked: str, limit: int):
    code_lines = sum(1 for line in masked.splitlines() if line.strip() and not line.strip().startswith("#"))
    if code_lines > limit:
        return [(1, f"入口胶水层 {code_lines} 行代码（上限 {limit}），业务逻辑应下沉到组件层")]
    return []


# ---------------------------------------------------------------- 跨文件索引


def layer_depth(layer: str) -> int:
    return 4 if layer == "glue" else LAYER_DEPTH.get(layer, -1)


def build_function_index(root: str, rules):
    """函数名 → 定义它的层集合。用于发现"下层声明/调用上层函数"的反向依赖。"""
    index = {}
    for path in iter_c_files(root):
        rel = os.path.relpath(path, root).replace("\\", "/")
        layer = resolve_layer(rel, rules)
        if layer not in LAYER_DEPTH and layer != "glue":
            continue
        _, masked, _ = read_source(path)
        for name, _, _, _ in iter_functions(masked):
            index.setdefault(name, set()).add(layer)
    return index


def scan_defines(masked: str):
    """返回 [(起始行号, 宏名, 展开文本)]，处理反斜杠续行。"""
    out = []
    lines = masked.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("#"):
            start = i
            buf = line.rstrip()
            while buf.endswith("\\") and i + 1 < len(lines):
                i += 1
                buf = buf[:-1] + " " + lines[i].rstrip()
            m = DEFINE_LINE.match(buf)
            if m:
                out.append((start + 1, m.group(1), buf[m.end():]))
        i += 1
    return out


def macro_reaches_hardware(expansion: str) -> bool:
    return bool(
        HAL_CALL.search(expansion)
        or LL_CALL.search(expansion)
        or SPL_CALL.search(expansion)
        or RTOS_CALL.search(expansion)
        or REG_ACCESS.search(expansion)
        or MMIO_CAST.search(expansion)
        or re.search(r"\bbsp_[a-z][a-z0-9_]*\s*\(", expansion)
    )


def build_macro_index(root: str, rules):
    """宏名 → (定义层, 文件)。展开后碰硬件/BSP 的宏，被上层使用即视为越层。"""
    index = {}
    for path in iter_c_files(root):
        rel = os.path.relpath(path, root).replace("\\", "/")
        layer = resolve_layer(rel, rules)
        if not layer:
            continue
        _, masked, _ = read_source(path)
        for _, name, expansion in scan_defines(masked):
            if macro_reaches_hardware(expansion):
                index.setdefault(name, (layer, rel))
    return index


# ---------------------------------------------------------------- 链接脚本


def _section_body(text: str, open_brace: int) -> str:
    depth = 0
    i = open_brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:i]
        i += 1
    return text[open_brace + 1:]


def _declared_size(body: str):
    m = LD_SIZE_DIRECTIVE.search(body)
    if m:
        return int(m.group(1), 0)
    m = LD_SIZE_COMMENT.search(body)
    if m:
        return int(m.group(1), 0)
    m = LD_RANGE_COMMENT.search(body)
    if m:
        lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        if hi >= lo:
            return hi - lo + 1
    return None


def collect_linker(root: str):
    """返回 [(rel, name, addr_text, addr_int, line, noload, size, wildcards)]。"""
    sections = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if not fn.endswith(".ld"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root).replace("\\", "/")
            with open(path, encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
            for m in LD_SECTION.finditer(text):
                body = _section_body(text, m.end() - 1)
                wildcards = [w for w in LD_WILDCARD.findall(body) if "*" in w or "?" in w]
                line = text.count("\n", 0, m.start()) + 1
                sections.append((
                    rel, m.group(1), m.group(2), int(m.group(2), 16), line,
                    bool(m.group(3)), _declared_size(body), wildcards,
                ))
    return sections


def check_linker(root: str, c_sections, errors, warns):
    sections = collect_linker(root)
    if not sections:
        return

    defined = {}
    by_addr = {}
    for rel, name, addr, value, line, noload, size, wildcards in sections:
        defined.setdefault(name, []).append((rel, addr, line, noload))
        by_addr.setdefault(value, []).append((rel, name, addr, line))
        if value % 4 != 0:
            errors.append((rel, line, f"固定地址段 {name} 未 4 字节对齐：{addr}"))
        if wildcards:
            pats = "、".join(f"*({w})" for w in wildcards)
            errors.append((rel, line, f"固定地址段 {name} 使用通配符 {pats}；必须写成 *({name})，避免吞并后续段"))

    for value, group in by_addr.items():
        if len(group) > 1:
            detail = "、".join(f"{name}@{rel}" for rel, name, _, _ in group)
            rel, name, addr, line = group[1]
            errors.append((rel, line, f"固定地址段冲突：{detail} 都放在 {addr}"))

    # 区间重叠：只有声明了长度（/* size: 0xNN */ 或 . = . + 0xNN）才判定
    sized = [(name, value, value + size - 1, rel, line)
             for rel, name, _, value, line, _, size, _ in sections if size]
    for i, (name_a, lo_a, hi_a, rel_a, line_a) in enumerate(sized):
        for name_b, lo_b, hi_b, rel_b, line_b in sized[i + 1:]:
            if lo_a == lo_b:
                continue
            if max(lo_a, lo_b) <= min(hi_a, hi_b):
                errors.append((
                    rel_b, line_b,
                    f"固定地址段区间重叠：{name_a} [0x{lo_a:08X}-0x{hi_a:08X}] 与 "
                    f"{name_b} [0x{lo_b:08X}-0x{hi_b:08X}]",
                ))

    for rel, name, line in c_sections:
        if name in defined:
            if not any(noload for _, _, _, noload in defined[name]):
                warns.append((rel, line, f"固定地址段 {name} 未标 (NOLOAD)，启动时可能被清零"))
            continue
        if SECTION_NAME_OK.match(name):
            continue
        errors.append((rel, line,
                       f"固定地址段 {name} 未在链接脚本定义（会被通配吞掉或落到默认段）"))


# ---------------------------------------------------------------- 主流程


def main():
    parser = argparse.ArgumentParser(description="四层架构启发式静态检查")
    parser.add_argument("root", help="工程根目录")
    parser.add_argument("--roles", default=None, help="角色清单文件路径")
    parser.add_argument("--glue-max-lines", type=int, default=200, help="入口胶水文件代码行上限")
    parser.add_argument("--max-per-file", type=int, default=5, help="未分层文件每文件最多提示条数")
    parser.add_argument("--no-linker", action="store_true", help="跳过链接脚本检查")
    parser.add_argument("--allow-unlayered", action="store_true",
                        help="允许工程完全没有分层目录/角色清单（默认判为 ERROR，避免绿灯空转）")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"路径不存在：{root}", file=sys.stderr)
        return 2

    rules = load_roles(root, args.roles)
    header_index = build_header_index(root, rules)
    func_index = build_function_index(root, rules)
    unsafe_macros = build_macro_index(root, rules)

    errors, warns = [], []
    unlayered_hits = 0
    unlayered_files = 0
    unlayered_total = 0
    layered_files = 0
    rules_matched = 0
    main_files = []
    c_sections = []

    for path in sorted(iter_c_files(root)):
        rel = os.path.relpath(path, root).replace("\\", "/")
        layer = resolve_layer(rel, rules)

        if layer:
            layered_files += 1
            if rules and match_role(rel, rules):
                rules_matched += 1
            if os.path.basename(rel) == "main.c":
                main_files.append(rel)
        else:
            unlayered_total += 1

        raw, masked, raw_lines = read_source(path)
        local_funcs = {name for name, _, _, _ in iter_functions(masked)}

        for m in SECTION_ATTR.finditer(raw):
            c_sections.append((rel, m.group(1), raw.count("\n", 0, m.start()) + 1))

        local_hits = 0

        if layer in UPPER_LAYERS:
            for def_line, name, expansion in scan_defines(masked):
                if macro_reaches_hardware(expansion):
                    errors.append((rel, def_line, f"宏间接越层：{name} 展开到下层调用/寄存器"))

        if layer in LAYER_DEPTH:
            reported = set()
            for m in EXTERN_FUNC.finditer(masked):
                name = m.group(1)
                defs = func_index.get(name)
                if not defs or len(defs) != 1 or name in reported:
                    continue
                only = next(iter(defs))
                if layer_depth(only) > layer_depth(layer):
                    line_no = masked.count("\n", 0, m.start()) + 1
                    errors.append((rel, line_no,
                                   f"反向依赖：{layer}/ 声明上层函数 {name}（定义在 {only}/）"))
                    reported.add(name)

            for no, line in enumerate(masked.splitlines(), 1):
                raw_line = raw_lines[no - 1] if no <= len(raw_lines) else ""
                if "arch-allow" in raw_line or line.lstrip().startswith("#"):
                    continue
                if DECL_ONLY_LINE.match(line):
                    continue  # 函数原型不是调用，不判反向依赖
                for m in CALL_NAME.finditer(line):
                    name = m.group(1)
                    if name in reported:
                        continue
                    defs = func_index.get(name)
                    if not defs or len(defs) != 1:
                        continue
                    only = next(iter(defs))
                    if layer_depth(only) > layer_depth(layer):
                        if layer == "bsp" and only == "components" and "_hw_" in name:
                            warns.append((rel, no,
                                          f"端口层调用组件 ISR 钩子：{name}（按 *_hw_* 命名识别，确认仅中断上下文使用）"))
                            reported.add(name)
                            continue
                        errors.append((rel, no,
                                       f"反向依赖：{layer}/ 调用上层函数 {name}（定义在 {only}/）"))
                        reported.add(name)

        for no, line in enumerate(masked.splitlines(), 1):
            raw_line = raw_lines[no - 1] if no <= len(raw_lines) else ""
            if "arch-allow" in raw_line:
                continue
            code = line

            if layer in ("", "glue"):
                if layer == "":
                    hits = []
                    for pat, label in (
                        (HAL_CALL, "HAL 调用"),
                        (LL_CALL, "LL 调用"),
                        (SPL_CALL, "厂商库调用"),
                        (RTOS_CALL, "RTOS 调用"),
                    ):
                        m = pat.search(code)
                        if m:
                            hits.append(f"{label}：{m.group(0)}")
                            break
                    if REG_ACCESS.search(code) or MMIO_CAST.search(code):
                        hits.append("寄存器访问")
                    if hits and local_hits < args.max_per_file:
                        warns.append((rel, no, "未分层文件 " + "；".join(hits)))
                        local_hits += 1
                        unlayered_hits += 1
                continue

            if layer in NON_BSP_LAYERS:
                if layer != "osal" or path.endswith(".h"):
                    m = PLATFORM_HEADER.search(code)
                    if m:
                        errors.append((rel, no, f"平台头泄漏：{m.group(1)}"))

            if layer in UPPER_LAYERS:
                for pat, label in (
                    (HAL_CALL, "越层调用 HAL"),
                    (LL_CALL, "越层调用 LL"),
                    (SPL_CALL, "越层调用厂商库"),
                    (RTOS_CALL, "越层调用 RTOS API"),
                ):
                    m = pat.search(code)
                    if m:
                        called = CALL_NAME.search(m.group(0))
                        if called and called.group(1) in local_funcs:
                            continue  # 本文件内定义的同名函数，不是厂商库（issue #2）
                        errors.append((rel, no, f"{label}：{m.group(0)}"))
                if REG_ACCESS.search(code) or MMIO_CAST.search(code):
                    errors.append((rel, no, "越层访问寄存器"))

                for name, (mlayer, mrel) in unsafe_macros.items():
                    if layer_depth(mlayer) >= layer_depth(layer):
                        continue
                    if re.search(r"\b" + re.escape(name) + r"\b", code):
                        errors.append((rel, no,
                                       f"宏间接越层：{name}（定义在 {mlayer} 层 {mrel}）"))

            if layer == "app":
                for m in BSP_PREFIX_CALL.finditer(code):
                    name = m.group(0).rstrip("(").strip()
                    if name.startswith("bsp_"):
                        errors.append((rel, no, f"APP 直调 BSP：{name}（应经组件层）"))
                    elif name not in APP_OSAL_ALLOWED:
                        errors.append((rel, no, f"APP 越权调用 OSAL：{name}（仅允许时间原语）"))

            if layer in UPPER_LAYERS:
                if HAL_DELAY.search(code):
                    errors.append((rel, no, "阻塞延时：HAL_Delay（应基于时基非阻塞）"))
                elif SOFT_DELAY.search(code):
                    warns.append((rel, no, "app/组件层出现阻塞延时（确认是否应改为非阻塞时基）"))

            if layer in NON_BSP_LAYERS:
                m = VENDOR_TYPE.search(code)
                if m:
                    errors.append((rel, no, f"厂商类型泄漏：{m.group(1)}"))

            for m in re.finditer(r'#\s*include\s*[<"]?([^>"]+)[>"]', code):
                target = m.group(1).replace("\\", "/").split("/")[-1]
                info = header_index.get(target)
                if not info:
                    continue
                tgt_layer, tgt_rel = info
                if LAYER_DEPTH.get(tgt_layer, -1) > LAYER_DEPTH.get(layer, 99):
                    if layer == "bsp" and tgt_layer == "osal":
                        break  # 端口层需要临界区原语与 tick（issue #4），符号级另有限制
                    if layer == "bsp" and tgt_layer == "components":
                        tgt_path = os.path.join(root, tgt_rel)
                        if os.path.isfile(tgt_path) and header_is_type_only(tgt_path):
                            warns.append((rel, no,
                                          f"反向依赖（仅类型/钩子头）：{layer}/ 包含 {tgt_layer}/ 的 {target}；确认只使用类型与 ISR 钩子"))
                            break
                    errors.append((rel, no, f"反向依赖：{layer}/ 包含 {tgt_layer}/ 的 {target}"))
                    break

            if layer == "bsp":
                for m in OSAL_CALL.finditer(code):
                    name = m.group(1)
                    if name.endswith(BSP_OSAL_ALLOWED):
                        continue
                    errors.append((rel, no,
                                   f"BSP 越权调用 OSAL：{name}（仅允许 *critical_enter / *critical_exit / *tick_inc）"))

        if layer == "":
            if local_hits:
                unlayered_files += 1
            continue

        if layer == "glue":
            errors.extend((rel, no, msg) for no, msg in check_glue_length(raw, masked, args.glue_max_lines))
        else:
            if layer in NON_BSP_LAYERS:
                if path.endswith(".c"):
                    errors.extend((rel, no, msg) for no, msg in find_global_vars(masked, raw_lines))
                errors.extend((rel, no, msg) for no, msg in find_extern_vars(masked, raw_lines))
            if layer != "bsp":
                errors.extend((rel, no, msg) for no, msg in check_nesting(masked))

        if path.endswith(".c"):
            for no, msg in check_isr(masked):
                raw_line = raw_lines[no - 1] if no <= len(raw_lines) else ""
                if "arch-allow" not in raw_line:
                    errors.append((rel, no, msg))

    if len(main_files) > 1:
        for rel in main_files[1:]:
            errors.append((rel, 1, f"存在多个 main.c（{', '.join(main_files)}）；入口只允许一个"))

    if not args.allow_unlayered:
        if layered_files == 0:
            errors.append(("(工程)", 1,
                           "未发现任何分层目录或角色清单：检查器没有执行分层约束。"
                           "请加 .arch-roles，或确需只统计未分层文件时加 --allow-unlayered"))
        elif rules and rules_matched == 0:
            errors.append((args.roles or ROLE_FILE_NAMES[0], 1,
                           "角色清单没有匹配到任何文件：检查实际未生效，请核对模式与路径"))

    if not args.no_linker:
        check_linker(root, c_sections, errors, warns)

    errors.sort(key=lambda x: (x[0], x[1]))
    warns.sort(key=lambda x: (x[0], x[1]))
    for rel, no, msg in errors:
        print(f"[ERROR] {rel}:{no} {msg}")
    for rel, no, msg in warns:
        print(f"[WARN ] {rel}:{no} {msg}")

    if unlayered_files or unlayered_total:
        print(
            f"[NOTE ] 另有 {unlayered_total} 个未分层文件（其中 {unlayered_files} 个含平台调用、"
            f"{unlayered_hits} 处已提示）；把它们放进 app/ components/ osal/ bsp/，或用 .arch-roles 声明角色"
        )
    if rules:
        print(f"[NOTE ] 已按角色清单 {args.roles or ROLE_FILE_NAMES[0]} 判定层归属")

    print(f"\n合计 {len(errors)} ERROR / {len(warns)} WARN（扫描目录：{root}）")
    return 1 if errors else 0


def build_header_index(root: str, rules):
    """头文件名 → (所在层, 相对路径)。"""
    index = {}
    for path in iter_c_files(root):
        if not path.endswith(".h"):
            continue
        rel = os.path.relpath(path, root).replace("\\", "/")
        layer = resolve_layer(rel, rules)
        if not layer:
            continue
        index.setdefault(os.path.basename(path), (layer, rel))
    return index


if __name__ == "__main__":
    sys.exit(main())
