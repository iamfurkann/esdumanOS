#include "ktest.h"
#include "syscall.h" 
#include "process.h" // process_t ve tasks[] dizisine erismek icin

extern void ft_strcpy(char *dest, const char *src);

// Test bitince UID'yi temizleyebilmek icin Kernel degiskenlerini ice aktariyoruz
extern process_t tasks[];

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
    
    // [TEST İZOLASYONU]: Test baslamadan onceki orijinal UID'yi kaydet
    int original_uid = 0;
    if (current_task >= 0) {
        original_uid = tasks[current_task].uid;
    }
    
    // Syscall güvenliğini aşmak için adresleri Kullanıcı Alanına (User Space) yerleştiriyoruz
    char *u_pass_wrong = (char *)0x500400;
    char *u_pass_correct = (char *)0x500500;
    char *u_empty = (char *)0x500600;
    
    ft_strcpy(u_pass_wrong, "yanlis_sifre_123");
    ft_strcpy(u_pass_correct, "1234"); // Sistemdeki geçerli root parolası
    ft_strcpy(u_empty, "");

    // 1. ROOT YETKİSİNİ BIRAK: UID'yi geçici olarak 1000'e (Normal Kullanıcı) çek
    sys_setuid(1000, u_empty);

    // 2. YANLIŞ PAROLA TESTİ: Normal kullanıcı yanlış şifreyle Root olmaya çalışıyor
    int root_res_wrong = sys_setuid(0, u_pass_wrong);
    KTEST_ASSERT(root_res_wrong == -1, "[STRICT] sys_setuid YANLIS sifreyle kesinlikle -1 donuyor");
    
    // 3. GEÇERSİZ UID TESTİ: Kesinlikle -1 dönmeli!
    int invalid_uid = sys_setuid(9999, u_empty);
    KTEST_ASSERT(invalid_uid == -1, "[STRICT] sys_setuid gecersiz UID'de kesinlikle -1 donuyor");

    // 4. DOĞRU PAROLA TESTİ: Kesinlikle 0 dönmeli!
    int root_res_correct = sys_setuid(0, u_pass_correct);
    KTEST_ASSERT(root_res_correct == 0, "[STRICT] sys_setuid DOGRU sifreyle kesinlikle 0 donuyor");

    if (current_task >= 0) {
        tasks[current_task].uid = original_uid; 
    }
}