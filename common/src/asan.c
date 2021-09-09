
#include "api.h"
#include "asan.h"
#include "assert.h"

#ifndef ASAN
#error This code should be compiled only with ASAN defined.
#endif

#define RETURN_ADDR __builtin_extract_return_addr(__builtin_return_address(0))

#define ASAN_REPORT_WIDTH 16
#define ASAN_REPORT_LINES 4

/* See `callbacks.h` */
#if defined(IN_SHIM)
#define ABORT_NAME "shim_abort"
#elif defined(IN_PAL)
#define ABORT_NAME "pal_abort"
#else
#define ABORT_NAME "abort"
#endif

__attribute__((no_sanitize("address")))
void asan_poison_region(uintptr_t addr, size_t size, int8_t value) {
    assert((addr & ASAN_SHADOW_MASK) == 0);
    size = ALIGN_UP(size, ASAN_SHADOW_ALIGN);
    int8_t* shadow_ptr = (int8_t*)ASAN_MEM_TO_SHADOW(addr);
    size_t shadow_size = size >> ASAN_SHADOW_SHIFT;

    _real_memset(shadow_ptr, value, shadow_size);
}

__attribute__((no_sanitize("address")))
static void asan_poison_region_unaligned(uintptr_t addr, size_t size, int8_t value) {
    size_t aligned_addr = ALIGN_UP(addr, ASAN_SHADOW_ALIGN);
    size_t left_part_size = aligned_addr - addr;
    if (left_part_size) {
        int8_t* shadow_ptr = (int8_t*)ASAN_MEM_TO_SHADOW(aligned_addr) - 1;
        *shadow_ptr = ASAN_SHADOW_ALIGN - left_part_size;
    }
    asan_poison_region(aligned_addr, size - left_part_size, value);
}

__attribute__((no_sanitize("address")))
void asan_unpoison_region(uintptr_t addr, size_t size) {
    assert((addr & ASAN_SHADOW_MASK) == 0);
    int8_t* shadow_ptr = (int8_t*)ASAN_MEM_TO_SHADOW(addr);
    size_t shadow_size = size >> ASAN_SHADOW_SHIFT;
    size_t right_part_size = size & ASAN_SHADOW_MASK;

    _real_memset(shadow_ptr, 0, shadow_size);
    if (right_part_size)
        *(shadow_ptr + shadow_size) = right_part_size;
}

__attribute__((no_sanitize("address")))
static bool asan_check(uintptr_t addr) {
    int8_t val = *(int8_t*)ASAN_MEM_TO_SHADOW(addr);
    return val && val <= (int8_t)(addr & ASAN_SHADOW_MASK);
}

static int asan_buf_write_all(const char* str, size_t size, void* arg) {
    __UNUSED(arg);
    log_error("asan: %.*s", (int)size, str);
    return 0;
}

__attribute__((no_sanitize("address")))
static void asan_report(void* ip_addr, uintptr_t addr, size_t size, bool is_load) {
    log_error("asan: trying to %s %lu bytes at 0x%lx, IP = %p", is_load ? "load" : "store", size,
              addr, ip_addr);

    uintptr_t bad_addr;
    for (bad_addr = addr; bad_addr < addr + size; bad_addr++)
        if (asan_check(bad_addr))
            break;

    log_error("asan: (for a full traceback, run GDB and set a breakpoint for \"%s\")", ABORT_NAME);

    log_error("asan: the bad address is %p (%lu from beginning)", (void*)bad_addr, bad_addr - addr);
    log_error("asan:");

    struct print_buf buf = INIT_PRINT_BUF(asan_buf_write_all);

    uintptr_t bad_shadow = ASAN_MEM_TO_SHADOW(bad_addr);
    uintptr_t report_start = bad_shadow - bad_shadow % ASAN_REPORT_WIDTH
        - ASAN_REPORT_WIDTH * ASAN_REPORT_LINES;
    uintptr_t report_end = report_start + ASAN_REPORT_WIDTH * (ASAN_REPORT_LINES * 2 + 1);
    for (uintptr_t line = report_start; line < report_end; line += ASAN_REPORT_WIDTH) {
        buf_printf(&buf, "%p ", (void*)ASAN_SHADOW_TO_MEM(line));
        for (uintptr_t shadow = line; shadow < line + ASAN_REPORT_WIDTH; shadow++) {
            uint8_t val = *(uint8_t*)shadow;
            if (shadow == bad_shadow) {
                buf_printf(&buf, "[%02x]", val);
            } else if (shadow == bad_shadow + 1) {
                buf_printf(&buf, "%02x", val);
            } else {
                buf_printf(&buf, " %02x", val);
            }
        }
        buf_flush(&buf);
    }
    log_error("asan:");
    log_error("asan: shadow byte legend (1 shadow byte = %d application bytes):",
              ASAN_SHADOW_ALIGN);
    log_error("asan: %22s 00", "addressable:");
    log_error("asan: %22s %02x..%02x", "partially addressable:", 1, ASAN_SHADOW_ALIGN - 1);
    log_error("asan: %22s %02x", "heap left redzone:", ASAN_POISON_HEAP_LEFT_REDZONE);
    log_error("asan: %22s %02x", "freed heap region:", ASAN_POISON_HEAP_AFTER_FREE);
    log_error("asan: %22s %02x", "global redzone:", ASAN_POISON_GLOBAL_REDZONE);
    log_error("asan: %22s %02x", "stack left redzone:", ASAN_POISON_STACK_LEFT);
    log_error("asan: %22s %02x", "stack mid redzone:", ASAN_POISON_STACK_MID);
    log_error("asan: %22s %02x", "stack right redzone:", ASAN_POISON_STACK_RIGHT);
    log_error("asan: %22s %02x", "stack after return:", ASAN_POISON_STACK_AFTER_RETURN);
    log_error("asan: %22s %02x", "use after scope:", ASAN_POISON_STACK_USE_AFTER_SCOPE);
    log_error("asan: %22s %02x", "alloca left redzone:", ASAN_POISON_ALLOCA_LEFT);
    log_error("asan: %22s %02x", "alloca right redzone:", ASAN_POISON_ALLOCA_RIGHT);
}

__attribute__((no_sanitize("address")))
static bool asan_check_region(uintptr_t addr, size_t size) {
    uintptr_t start = addr;
    uintptr_t end = addr + size;
    if (asan_check(start))
        return true;

    if (size > 1 && asan_check(end - 1))
        return true;

    if (size > ASAN_SHADOW_ALIGN) {
        uintptr_t start_shadow = ASAN_MEM_TO_SHADOW(start);
        uintptr_t end_shadow = ASAN_MEM_TO_SHADOW(end);
        for (uintptr_t shadow = start_shadow; shadow < end_shadow; shadow++) {
            if (*(int8_t*)shadow != 0)
                return true;
        }
    }

    return false;
}

#define ASAN_LOAD(addr, size)          \
    do {                                                \
        if (asan_check_region(addr, size)) {            \
            asan_report(RETURN_ADDR, addr, size, true); \
            abort();                                    \
        }                                               \
    } while(0)


#define ASAN_STORE(addr, size)          \
    do {                                                 \
        if (asan_check_region(addr, size)) {             \
            asan_report(RETURN_ADDR, addr, size, false); \
            abort();                                     \
        }                                                \
    } while(0)

#define DEFINE_ASAN_CALLBACKS(size)                            \
    void __asan_load##size(uintptr_t addr) {                   \
        ASAN_LOAD(addr, size);                                 \
    }                                                          \
    void __asan_store##size(uintptr_t addr) {                  \
        ASAN_STORE(addr, size);                                \
    }                                                          \
    void __asan_report_load##size(uintptr_t addr) {            \
        asan_report(RETURN_ADDR, addr, size, true);            \
        abort();                                               \
    }                                                          \
    void __asan_report_store##size(uintptr_t addr) {           \
        asan_report(RETURN_ADDR, addr, size, false);           \
        abort();                                               \
    }

DEFINE_ASAN_CALLBACKS(1)
DEFINE_ASAN_CALLBACKS(2)
DEFINE_ASAN_CALLBACKS(4)
DEFINE_ASAN_CALLBACKS(8)
DEFINE_ASAN_CALLBACKS(16)

void __asan_loadN(uintptr_t addr, size_t size) {
    ASAN_LOAD(addr, size);
}

void __asan_storeN(uintptr_t addr, size_t size) {
    ASAN_STORE(addr, size);
}

void __asan_report_load_n(uintptr_t addr, size_t size) {
    asan_report(RETURN_ADDR, addr, size, true);
    abort();
}

void __asan_report_store_n(uintptr_t addr, size_t size) {
    asan_report(RETURN_ADDR, addr, size, false);
    abort();
}

void __asan_handle_no_return(void) {}

void __asan_alloca_poison(uintptr_t addr, size_t size) {
    uintptr_t left_redzone_addr = addr - ASAN_ALLOCA_REDZONE_SIZE;
    uintptr_t end_addr = addr + size;
    uintptr_t right_redzone_addr = ALIGN_UP(end_addr, ASAN_ALLOCA_REDZONE_SIZE) +
        ASAN_ALLOCA_REDZONE_SIZE;
    asan_poison_region(left_redzone_addr, ASAN_ALLOCA_REDZONE_SIZE, ASAN_POISON_ALLOCA_LEFT);
    asan_poison_region_unaligned(end_addr, right_redzone_addr - end_addr,
                                 ASAN_POISON_ALLOCA_RIGHT);
}

void __asan_allocas_unpoison(uintptr_t top, uintptr_t bottom) {
    if (top && top < bottom) {
        asan_unpoison_region(top, bottom - top);
    }
}

void __asan_init(void) {}
void __asan_version_mismatch_check_v8(void) {}
/*
void __asan_register_globals(void *globals, uintptr_t n) {
    __UNUSED(globals);
    __UNUSED(n);
}

void __asan_unregister_globals(void *globals, uintptr_t n) {
    __UNUSED(globals);
    __UNUSED(n);
}
*/
__attribute__((alias("__asan_memcpy")))
void* memcpy(void *dst, const void *src, size_t size);

void* __asan_memcpy(void *dst, const void *src, size_t size) {
    ASAN_LOAD((uintptr_t)src, size);
    ASAN_STORE((uintptr_t)dst, size);
    return _real_memcpy(dst, src, size);
}

__attribute__((alias("__asan_memset")))
void* memset(void *s, int c, uintptr_t n);

void* __asan_memset(void *s, int c, uintptr_t n) {
    ASAN_STORE((uintptr_t)s, n);
    return _real_memset(s, c, n);
}

__attribute__((alias("__asan_memmove")))
void* memmove(void* dest, const void* src, uintptr_t n);

void* __asan_memmove(void* dest, const void* src, uintptr_t n) {
    ASAN_LOAD((uintptr_t)src, n);
    ASAN_STORE((uintptr_t)dest, n);
    return _real_memmove(dest, src, n);
}

int memcmp(const void* lhs, const void* rhs, size_t count) {
    ASAN_LOAD((uintptr_t)lhs, count);
    ASAN_LOAD((uintptr_t)lhs, count);
    return _real_memcmp(lhs, rhs, count);
}

#define DEFINE_ASAN_SET_SHADOW(name, value)                         \
    void __asan_set_shadow_ ## name (uintptr_t addr, size_t size) { \
        _real_memset((void*)addr, value, size);                     \
    }

DEFINE_ASAN_SET_SHADOW(00, 0)
DEFINE_ASAN_SET_SHADOW(f1, 0xf1)
DEFINE_ASAN_SET_SHADOW(f2, 0xf2)
DEFINE_ASAN_SET_SHADOW(f3, 0xf3)
DEFINE_ASAN_SET_SHADOW(f5, 0xf5)
DEFINE_ASAN_SET_SHADOW(f8, 0xf8)
