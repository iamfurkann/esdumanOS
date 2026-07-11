#include "ktest.h"
#include "pipe.h"
#include "syscall.h" // int 0x80 numaraları için

extern void ft_strcpy(char *dest, const char *src);

// =========================================================
// Kernel İçinden (Ring 0) Syscall Tetikleme Köprüsü
// =========================================================
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_pipe_tests(void) {
    printk("\n--- IPC (Pipe) Unit ve Entegrasyon Testleri ---\n");
    serial_print("\n--- IPC (Pipe) Unit ve Entegrasyon Testleri ---\n");
    
    // ---------------------------------------------------------
    // BÖLÜM 1: BİRİM TESTİ (Unit Test) - İç Mantık ve Bloklama
    // ---------------------------------------------------------
    pipe_t *p = create_pipe();
    KTEST_ASSERT(p != 0, "[STRICT] create_pipe statik havuzdan boru uretti (p != NULL)");

    uint8_t buffer[10];
    int eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == -11, "[STRICT] Bos boruyu dogrudan okumak EAGAIN (-11) dondu");

    p->write_refs = 0; // Yazarı manuel kapat
    eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == 0, "[STRICT] Yazari kapanmis boru EOF (0) dondu");
    destroy_pipe(p);

    // ---------------------------------------------------------
    // BÖLÜM 2: UÇTAN UCA ENTEGRASYON TESTİ (Syscall & FD Table)
    // ---------------------------------------------------------
    
    int *u_fds = (int *)0x500700;
    char *u_write_buf = (char *)0x500800;
    char *u_read_buf = (char *)0x500900;
    ft_strcpy(u_write_buf, "42KFS");

    // 1. SYSCALL ile Pipe Oluştur (Ring 3'ten geliyormuş gibi)
    int pipe_sys = ktest_syscall(SYSCALL_PIPE, (int)u_fds, 0, 0);
    KTEST_ASSERT(pipe_sys == 0, "[STRICT] SYSCALL_PIPE basariyla calisti (res == 0)");
    KTEST_ASSERT(u_fds[0] >= 3 && u_fds[1] >= 3, "[STRICT] SYSCALL_PIPE gecerli FD'ler dondu (FD >= 3)");

    // 2. SYSCALL ile Pipe'a Yaz (FD Tablosu Entegrasyonu)
    int w_res = ktest_syscall(SYSCALL_WRITE, u_fds[1], (int)u_write_buf, 5);
    KTEST_ASSERT(w_res == 5, "[STRICT] SYSCALL_WRITE User-Space buffer'indan pipe'a 5 byte yazdi");

    // 3. SYSCALL ile Pipe'tan Oku (Yazdığımız veriyi geri çekiyoruz)
    int r_res = ktest_syscall(SYSCALL_READ, u_fds[0], (int)u_read_buf, 5);
    KTEST_ASSERT(r_res == 5, "[STRICT] SYSCALL_READ pipe'tan User-Space buffer'ina 5 byte okudu");

    // 4. Kapatma Syscall'ları ve EOF Kontrolü
    int c_res1 = ktest_syscall(SYSCALL_CLOSE, u_fds[1], 0, 0);
    KTEST_ASSERT(c_res1 == 0, "[STRICT] SYSCALL_CLOSE yazar FD'yi basariyla kapatti");

    int r_eof = ktest_syscall(SYSCALL_READ, u_fds[0], (int)u_read_buf, 5);
    KTEST_ASSERT(r_eof == 0, "[STRICT] SYSCALL_READ kapanmis pipe'tan EOF (0) okudu");

    int c_res2 = ktest_syscall(SYSCALL_CLOSE, u_fds[0], 0, 0);
    KTEST_ASSERT(c_res2 == 0, "[STRICT] SYSCALL_CLOSE okuyucu FD'yi basariyla kapatti");
}