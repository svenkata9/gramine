/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2021 Intel Corporation
 *                    Paweł Marczewski <pawel@invisiblethingslab.com>
 */

/*
 * This file defines functions for address sanitization (ASan).
 *
 * Normally, code compiled with ASAN is linked against a special library (libasan), but that library
 * is hard to adapt to a no-stdlib setting as well as all the custom memory handling that we
 * perform.
 *
 * See also `ubsan.c` for a similar (but much simpler) integration with UBSan.
 *
 * For more information, see:
 *
 * - ASan documentation: https://clang.llvm.org/docs/AddressSanitizer.html
 *
 * - libasan source code in LLVM repository: https://github.com/llvm/llvm-project/
 *   (compiler-rt/lib/asan/)
 *
 * - AddressSanitizer compiler code, also in LLVM repository, for flags that we use to configure
 *   (llvm/lib/Transforms/Instrumentation)
 */

/*
 * How to use ASan:
 *
 * - Make sure the program maps the shadow memory area at startup. This will be something like:
 *
 *       mmap((void*)ASAN_SHADOW_START, ASAN_SHADOW_LENGTH, PROT_READ | PROT_WRITE,
 *            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
 *            -1, 0);
 *
 * - Annotate all functions that shouldn't perform sanitized memory access with
 *   `__attribute__((no_sanitize("address")))`.
 *
 * - When allocating new memory (e.g. in the definition of `malloc`), unpoison exactly the memory
 *   requested. Poison any memory to the left (e.g. allocation metadata) with
 *   ASAN_POISON_HEAP_LEFT_REDZONE.
 *
 * - When freeing the allocated memory (e.g. in the definition of `free`), poison the memory with
 *   ASAN_POISON_HEAP_AFTER_FREE.
 *
 * - When mapping new memory pages for heap, poison them with ASAN_POISON_GLOBAL_REDZONE (so that
 *   access is forbidden until that memory is allocated). Unpoison them when unmapping.
 *
 * The current version does not support stack and globals sanitization. You should compile the
 * program with:
 *
 *       -fsanitize=address
 *       -mllvm -asan-use-after-return=0
 *       -mllvm -asan-use-after-return=0
 *       -mllvm -asan-stack=0
 *       -mllvm -asan-globals=0
 *       -DASAN
 */

#ifndef ASAN_H_
#define ASAN_H_

/* All ASan code should be guarded by `#ifdef ASAN`. */
#ifdef ASAN

#ifndef __x86_64__
/* Other systems will probably require different ASAN_SHADOW_* parameters */
#error ASan is currently supported only for x86_64.
#endif

#include <stddef.h>
#include <stdint.h>

/*
 * Parameters of the shadow memory area. The beginning address is the default for x86_64 (but can be
 * changed with `-mllvm -asan-mapping-offset=0x...`).
 *
 * Each byte of shadow memory corresponds to ASAN_SHADOW_ALIGN (by default 8) bytes of user memory.
 *
 * - A value of 0 means all bytes are accessible.
 *
 * - A positive value (01..07) means only the first N bytes are accessible.
 *
 * - A negative value (80..FF) means the memory is forbidden to use, and the exact value is used to
 *   explain why that is the case.
 */
#define ASAN_SHADOW_START 0x7fff8000
#define ASAN_SHADOW_SHIFT 3
#define ASAN_SHADOW_LENGTH (1ULL << 44)
#define ASAN_SHADOW_ALIGN (1 << ASAN_SHADOW_SHIFT)
#define ASAN_SHADOW_MASK ((1 << ASAN_SHADOW_SHIFT) - 1)

/* Conversion between user and shadow addresses */
#define ASAN_MEM_TO_SHADOW(addr) (((addr) >> ASAN_SHADOW_SHIFT) + ASAN_SHADOW_START)
#define ASAN_SHADOW_TO_MEM(addr) (((addr) - ASAN_SHADOW_START) << ASAN_SHADOW_SHIFT)

// TODO remove:
#define ASAN_ALLOCA_REDZONE_SIZE 32

/* Magic values to mark different kinds of inaccessible memory. */
#define ASAN_POISON_HEAP_LEFT_REDZONE     0xfa
#define ASAN_POISON_HEAP_AFTER_FREE       0xfd
#define ASAN_POISON_GLOBAL_REDZONE        0xf9
// TODO remove:
#define ASAN_POISON_STACK_LEFT            0xf1
#define ASAN_POISON_STACK_MID             0xf2
#define ASAN_POISON_STACK_RIGHT           0xf3
#define ASAN_POISON_STACK_AFTER_RETURN    0xf5
#define ASAN_POISON_STACK_USE_AFTER_SCOPE 0xf8
#define ASAN_POISON_ALLOCA_LEFT           0xca
#define ASAN_POISON_ALLOCA_RIGHT          0xcb

/* Poison a memory region. `addr` must be aligned to ASAN_SHADOW_ALIGN, and `size` is rounded up to
 * ASAN_SHADOW_ALIGN. */
void asan_poison_region(uintptr_t addr, size_t size, int8_t value);

/* Unpoison a memory region. `addr` must be aligned to ASAN_SHADOW_ALIGN, but `size` is treated
 * exactly. */
void asan_unpoison_region(uintptr_t addr, size_t size);

/* Initialization callbacks. Generated in object .init sections. Graphene doesn't call these anyway,
 * so this needs to be a no-op. */
void __asan_init(void);
void __asan_version_mismatch_check_v8(void);

/*
 * Load/store callbacks:
 *
 * - `load` / `store`: check if an memory under given address is accessible; if not, report the
 *   error and abort
 *
 * - `report_load` / `report_store`: directly report an illegal access and abort
 *
 * For small areas, instead of generating `load` and `store` callbacks, LLVM can generate inline
 * checks for the shadow memory (and calls to `report_load` / `report_store`). This is controlled by
 * `-mllvm -asan-instrumentation-with-call-threshold=N`.
 */

#define DECLARE_ASAN_LOAD_STORE_CALLBACKS(n)                \
    void __asan_load##n(uintptr_t p);                       \
    void __asan_store##n(uintptr_t p);                      \
    void __asan_report_load##n(uintptr_t p);                \
    void __asan_report_store##n(uintptr_t p);

DECLARE_ASAN_LOAD_STORE_CALLBACKS(1)
DECLARE_ASAN_LOAD_STORE_CALLBACKS(2)
DECLARE_ASAN_LOAD_STORE_CALLBACKS(4)
DECLARE_ASAN_LOAD_STORE_CALLBACKS(8)
DECLARE_ASAN_LOAD_STORE_CALLBACKS(16)

/* Variable-size version of load/store callbacks, used for large accesses. */
void __asan_loadN(uintptr_t p, size_t size);
void __asan_storeN(uintptr_t p, size_t size);
void __asan_report_load_n(uintptr_t p, size_t size);
void __asan_report_store_n(uintptr_t p, size_t size);

/* Called when entering a function marked as no-return. Used for stack sanitization. */
void __asan_handle_no_return(void);

/* Poison an area around memory allocated with `alloca`. Used for stack sanitization. */
// TODO remove
void __asan_alloca_poison(uintptr_t addr, size_t size);
void __asan_allocas_unpoison(uintptr_t top, uintptr_t bottom);

/* Sanitized versions of builtin functions. Note that ASan also overrides the normal versions
 * (`memcpy` etc.) */
void* __asan_memcpy(void *dst, const void *src, size_t size);
void* __asan_memset(void *s, int c, size_t n);
void* __asan_memmove(void* dest, const void* src, size_t n);

/* Callbacks for setting the shadow memory to specific values. As with load/store callbacks, LLVM
 * normally generates inline stores and calls these functions only for bigger areas. This is
 * controlled by `-mllvm -asan-max-inline-poisoning-size=N`. */
void __asan_set_shadow_00(uintptr_t addr, size_t size);
void __asan_set_shadow_f1(uintptr_t addr, size_t size);
void __asan_set_shadow_f2(uintptr_t addr, size_t size);
void __asan_set_shadow_f3(uintptr_t addr, size_t size);
void __asan_set_shadow_f5(uintptr_t addr, size_t size);
void __asan_set_shadow_f8(uintptr_t addr, size_t size);

#endif /* ASAN */

#endif /* ASAN_H */
