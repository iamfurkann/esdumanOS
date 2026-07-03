#include "ktest.h"
#include "syscall.h"

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_adversarial_tests(void) {
    printk("\n--- Kotu Niyetli Girdi (Adversarial/Security) Testleri ---\n");
    serial_print("\n--- Kotu Niyetli Girdi (Adversarial/Security) Testleri ---\n");

    // Güvenli (User Space) dummy içerik adresi
    int safe_content = 0x500C00;

    // 1. NULL Pointer Saldırısı
    int res_null = ktest_syscall(SYSCALL_CREATE_FILE, 0x0, safe_content, 0);
    KTEST_ASSERT(res_null == -1, "[STRICT] Syscall: NULL pointer argumani kesinlikle REDDEDILDI");

    // 2. Kernel Alanına (Ring 0) Sızma Denemesi (0x400000 altı)
    // 0x3FFFFF tam Kernel ile User Space sınırıdır. Burayı okumak yasak olmalı!
    int res_kernel_read = ktest_syscall(SYSCALL_CREATE_FILE, 0x3FFFFF, safe_content, 0);
    KTEST_ASSERT(res_kernel_read == -1, "[STRICT] Syscall: Kernel bellegine (0x3FFFFF) erisim REDDEDILDI");

    // 3. Çok Yüksek Bellek (Non-Canonical/Unmapped) Adresi Saldırısı
    int res_high_mem = ktest_syscall(SYSCALL_CREATE_FILE, 0xC0000000, safe_content, 0);
    KTEST_ASSERT(res_high_mem == -1, "[STRICT] Syscall: Unmapped Yuksek Bellek adresi REDDEDILDI");
}