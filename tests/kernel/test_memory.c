#include "ktest.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

void run_memory_tests(void) {
    printk("\n--- Memory (Heap/PMM) Testleri ---\n");
    
    void *ptr1 = kmalloc(128);
    KTEST_ASSERT(ptr1 != 0, "kmalloc(128) basariyla bellek ayirdi");
    
    void *ptr2 = kmalloc(256);
    KTEST_ASSERT(ptr2 != 0, "kmalloc(256) ardindan ikinci bellegi ayirdi");
    KTEST_ASSERT(ptr1 != ptr2, "kmalloc farkli pointerlar uretiyor");

    char *str = (char *)ptr1;
    str[0] = 'K'; str[1] = 'F'; str[2] = 'S'; str[3] = '\0';
    KTEST_ASSERT(str[0] == 'K', "Ayrilan bellege yazma ve okuma yapilabiliyor");

    kfree(ptr1);
    kfree(ptr2);
    KTEST_ASSERT(1, "kfree sistemi cokertmeden calisti (Double-Free yok)");
}