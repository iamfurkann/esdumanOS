#include "ktest.h"
#include "syscall.h"

// Kernel'in kendi güvenlik duvarı fonksiyonunu doğrudan dışarıdan (extern) alıyoruz
extern int is_valid_user_ptr(const void *ptr);

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_adversarial_tests(void) {
    printk("\n--- Kotu Niyetli Girdi (Adversarial/Security) Testleri ---\n");
    serial_print("\n--- Kotu Niyetli Girdi (Adversarial/Security) Testleri ---\n");

    int safe_content = 0x500C00;

    // =========================================================================
    // NEGATİF (REDDEDİLMESİ GEREKEN) SINIR DIŞI TESTLER
    // (Bunlar Syscall'un dışarıdan gelen veriyi reddettiğini kanıtlar)
    // =========================================================================
    int res_null = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0x0, safe_content, 0);
    KTEST_ASSERT(res_null == -1, "[STRICT] Syscall: NULL pointer argumani kesinlikle REDDEDILDI");

    int res_kernel_read = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0x3FFFFF, safe_content, 0);
    KTEST_ASSERT(res_kernel_read == -1, "[STRICT] Syscall: Kernel bellegine (0x3FFFFF) erisim REDDEDILDI");

    int res_high_mem = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0xC0000000, safe_content, 0);
    KTEST_ASSERT(res_high_mem == -1, "[STRICT] Syscall: Unmapped Yuksek Bellek adresi (0xC0000000) REDDEDILDI");

    // =========================================================================
    // POZİTİF (KABUL EDİLMESİ GEREKEN) TAM SINIR İÇİ TESTLER (Boundary Value)
    // Syscall'ların yan etkilerine (Kapalı FD, 0 byte reddi vb.) takılmamak için 
    // ve Page Fault (CR2) yememek için DOĞRUDAN güvenlik fonksiyonunu test ediyoruz!
    // =========================================================================

    // 4. Alt Sınır İçi (Exact Lower Bound)
    // 0x400000 User Space'in ilk byte'ıdır. Güvenlik duvarı burayı 1 (Geçerli) dönmelidir.
    int res_lower_bound = is_valid_user_ptr((const void *)0x400000);
    KTEST_ASSERT(res_lower_bound == 1, "[STRICT] Security: User Space Alt Siniri (0x400000) basariyla KABUL EDILDI");

    // 5. Üst Sınır İçi (Exact Upper Bound)
    // 0xBFFFFFFF sınır içidir! Güvenlik duvarı burayı 1 (Geçerli) dönmelidir.
    int res_upper_bound = is_valid_user_ptr((const void *)0xBFFFFFFF);
    KTEST_ASSERT(res_upper_bound == 1, "[STRICT] Security: User Space Ust Siniri (0xBFFFFFFF) basariyla KABUL EDILDI");
}