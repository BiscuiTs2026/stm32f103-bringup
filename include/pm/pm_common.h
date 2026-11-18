/*
 * pm_common.h - basic types and compile-time helpers.
 *
 * This header is dependency-free (C standard library only).
 * Every layer may include it; it includes no layer.
 */
#ifndef PM_COMMON_H
#define PM_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PM_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PM_UNUSED(x)     ((void)(x))

/* Compile-time assertion usable in C99 (no _Static_assert). */
#define PM_STATIC_ASSERT(cond, tag) \
    typedef char pm_static_assert_##tag[(cond) ? 1 : -1]

/* Compiler barrier: prevents reordering of shared-memory accesses.
 * On a hosted build there is no interrupt context, so it compiles away. */
#if defined(__GNUC__) || defined(__clang__)
#define PM_BARRIER() __asm volatile("" ::: "memory")
#else
#define PM_BARRIER() ((void)0)
#endif

#endif /* PM_COMMON_H */
