#include "kernel.h"

uint32_t __stack_chk_guard = 0x595e9fbd;

/**
 * @brief Initialize the stack check guard with a random value
 * This should be called early in boot process, after random numbers are available.
 */
__attribute__((no_stack_protector)) void init_stack_protect(void) {
    // Basic PRNG or use dev_random_read if we had it initialized this early
    // For now we just scramble it slightly using TSC
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    __stack_chk_guard ^= (lo ^ hi);
}

/**
 * @brief Called by compiler-generated code when a stack buffer overflow is detected.
 */
__attribute__((noreturn)) void __stack_chk_fail(void) {
    kernel_panic("STACK SMASHING DETECTED! A buffer overflow corrupted the stack.");
    while (1) {
        asm volatile("cli; hlt");
    }
}

/**
 * @brief GCC i386 might call __stack_chk_fail_local instead of __stack_chk_fail directly.
 */
__attribute__((noreturn)) void __stack_chk_fail_local(void) {
    __stack_chk_fail();
}
