#include "types.h"
#include "pipe.h"
#include "stdio.h"

void run_kernel_selftests(void) {
    printk("\n========================================\n");
    printk("[KFS SELF-TEST] Moduller Dogrulaniyor...\n");
    
    // 1. PIPE POOL TESTİ
    pipe_t *p = create_pipe();
    if (p) {
        printk("  [OK] Pipe Pool Tahsisi Basarili\n");
        destroy_pipe(p); // Bellek sızıntısı yapmadan geri ver
    } else {
        printk("  [FAIL] Pipe Pool Tahsis Edilemedi!\n");
    }

    // 2. KMALLOC TESTİ
    // void *mem = kmalloc(128);
    // if (mem) { printk("  [OK] Kmalloc\n"); kfree(mem); }
    
    printk("[KFS SELF-TEST] TAMAMLANDI\n");
    printk("========================================\n\n");
}