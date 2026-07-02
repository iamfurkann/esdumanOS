#include "types.h"
#include "stdio.h"
#include "pipe.h"

// Donanim portuna yazarak QEMU'yu kapatma fonksiyonu
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void qemu_shutdown(void) {
    // isa-debug-exit kullanarak QEMU'yu kapat
    outb(0xf4, 0x10);
}

void run_all_selftests(void) {
    printk("\n========================================\n");
    printk("[KFS SELF-TEST] Otomatik Testler Basliyor...\n");
    
    // 1. PIPE POOL TESTİ
    pipe_t *p = create_pipe();
    if (p) {
        printk("  [OK] Pipe Pool Tahsisi Basarili!\n");
        destroy_pipe(p);
    } else {
        printk("  [FAIL] Pipe Pool Tahsisi Basarisiz!\n");
    }

    printk("[KFS SELF-TEST] TUM TESTLER TAMAMLANDI!\n");
    printk("========================================\n");
    
    // Testler bitince QEMU'yu otomatik kapat (GitHub Actions icin)
    qemu_shutdown();
}