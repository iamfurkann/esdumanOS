#include "ktest.h"
#include "stdio.h"

int tests_passed = 0;
int tests_failed = 0;

static void ktest_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char temp[16]; int i = 0;
    while(n > 0) { temp[i++] = (n % 10) + '0'; n /= 10; }
    int j = 0;
    while(i > 0) { buf[j++] = temp[--i]; }
    buf[j] = '\0';
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void qemu_shutdown(int is_success) {
    // 0x10 gönderirsek Exit Code 33 olur (BAŞARI)
    // 0x11 gönderirsek Exit Code 35 olur (BAŞARISIZ)
    outb(0xf4, is_success ? 0x10 : 0x11);
}

void run_all_selftests(void) {
    terminal_initialize();
    printk("\n======================================================\n");
    printk("       KFS COMPREHENSIVE KERNEL TEST SUITE            \n");
    printk("======================================================\n");
    
    // Modülleri Çağır
    run_string_tests();
    run_memory_tests();
    run_pipe_tests();
    run_vfs_tests();
    run_security_tests();
    run_stress_tests();
    run_adversarial_tests();
    run_integration_tests();
    run_regression_tests();
    
    printk("\n======================================================\n");
    
    char pass_str[16]; ktest_itoa(tests_passed, pass_str);
    char fail_str[16]; ktest_itoa(tests_failed, fail_str);
    
    printk("SONUC: "); printk(pass_str); printk(" GECTI | "); 
    printk(fail_str); printk(" KALDI\n");
    printk("======================================================\n");

    if (tests_failed == 0) {
        qemu_shutdown(1);
    } else {
        qemu_shutdown(0);
    }
}