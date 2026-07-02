#include "ktest.h"
#include "pipe.h"

void run_pipe_tests(void) {
    printk("\n--- IPC (Pipe) Testleri ---\n");
    
    pipe_t *p = create_pipe();
    KTEST_ASSERT(p != 0, "create_pipe statik havuzdan boru secebiliyor");
    KTEST_ASSERT(p->head == 0 && p->tail == 0, "Boru baslangicta tertemiz (head=0, tail=0)");
    KTEST_ASSERT(p->read_refs == 1 && p->write_refs == 1, "Boru referans sayilari dogru basliyor");

    uint8_t buffer[10];
    int eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == -11, "Bos boruyu okumak EAGAIN (-11) donuyor (Uyku Modu)");
    
    p->write_refs = 0;
    eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == 0, "Yazari kapanmis bos boru EOF (0) donuyor (Uykuya Dalma Engellendi)");

    destroy_pipe(p);
    KTEST_ASSERT(1, "destroy_pipe memory leak olmadan boruyu havuza iade etti");
}