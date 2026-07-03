#include "ktest.h"
#include "syscall.h"

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_stress_tests(void) {
    printk("\n--- Sinir ve Stres Testleri (Stress/Boundary) ---\n");
    serial_print("\n--- Sinir ve Stres Testleri (Stress/Boundary) ---\n");
    
    int u_fds = 0x500A00;
    int pipes_created = 0;
    int res = 0;
    
    for (int i = 0; i < 50; i++) {
        res = ktest_syscall(SYSCALL_PIPE, u_fds, 0, 0);
        if (res == 0) pipes_created++;
        else break;
    }
    
    KTEST_ASSERT(res == -1 && pipes_created > 0 && pipes_created <= 16, 
                 "[STRICT] FD Exhaustion: MAX_FD siniri asilamadi ve sistem COKMEDI");

    char *long_name = (char *)0x500B00;
    for(int i=0; i<250; i++) long_name[i] = 'A';
    long_name[250] = '\0';
    
    int vfs_res = ktest_syscall(8 /* CREATE_FILE */, (int)long_name, (int)"", 0);
    KTEST_ASSERT(vfs_res == -1 || vfs_res == 0, 
                 "[STRICT] VFS Long Name: Dev dosya isminde sistem COKMEDI");
}