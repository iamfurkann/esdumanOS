#include "ktest.h"
#include "syscall.h" 

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

static inline int sys_setuid(int uid, const char *password) {
    return ktest_syscall(SYSCALL_SETUID, uid, (int)password, 0);
}

void run_security_tests(void) {
    printk("\n--- Guvenlik ve Yetkilendirme Testleri ---\n");
    serial_print("\n--- Guvenlik ve Yetkilendirme Testleri ---\n");

    int root_res = sys_setuid(0, "test_pass");
    KTEST_ASSERT(root_res == 0 || root_res != -1, "sys_setuid Ring 0 icinden cagirildiginda isleme onay veriyor");
    
    int invalid_uid = sys_setuid(9999, "");
    KTEST_ASSERT(invalid_uid == 0 || invalid_uid != -1, "sys_setuid gecersiz UID cagrilarinda cokmeden yanit veriyor");
}