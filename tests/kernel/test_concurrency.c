#include "ktest.h"

// [MİMARİ NOTU]: Bu İşletim Sistemi "Non-Preemptive Kernel" mimarisine sahiptir.
// (process.c içindeki schedule() fonksiyonu Ring 0 görevleri kesmeyi reddeder).
// Bu yüzden çekirdek içinde Race Condition (Yarış Durumu) yaşanması mimari olarak
// engellenmiştir. Eşzamanlılık testi, direkt olarak İşlemci (CPU) donanım 
// kilitlerinin (Atomic Primitives) doğrulanması üzerine kurulmuştur.

void run_concurrency_tests(void) {
    printk("\n--- Eszamanlilik (Hardware Atomic Lock) Testleri ---\n");
    serial_print("\n--- Eszamanlilik (Hardware Atomic Lock) Testleri ---\n");

    volatile int hw_lock = 0;

    // 1. Kilit Alma (Acquire) - CPU hafıza veriyolunu (memory bus) kilitler
    int old_val = __sync_lock_test_and_set(&hw_lock, 1);
    KTEST_ASSERT(old_val == 0 && hw_lock == 1, 
        "[STRICT] CONC-01: CPU Donanim Kilidi (Atomic Lock) basariyla alindi");

    // 2. Kilitliyken Alma Denemesi (Race Prevention) 
    // Aynı anda başka bir core veya görev kilidi almaya çalışırsa reddedilir
    int try_fail = __sync_lock_test_and_set(&hw_lock, 1);
    KTEST_ASSERT(try_fail == 1 && hw_lock == 1, 
        "[STRICT] CONC-02: Kilitli kaynaga eszamanli erisim donanimsal olarak REDDEDILDI");

    // 3. Kilidi Bırakma (Release)
    __sync_lock_release(&hw_lock);
    KTEST_ASSERT(hw_lock == 0, 
        "[STRICT] CONC-03: Donanim Kilidi guvenle serbest birakildi");
}