#include "syscall.h"
#include "errno.h"

typedef unsigned int uint32_t;

static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

int ft_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void ft_strcpy(char *dest, const char *src) {
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

int ft_strlen(const char *s) {
    int i = 0; 
    while(s[i]) i++; 
    return i;
}

void printk(const char *str) { syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); }
char get_keyboard_char(void) { char c = 0; syscall(SYSCALL_READ, 0, (int)&c, 1); return c;}
void sys_exit(void) { syscall(SYSCALL_EXIT, 0, 0, 0); while(1); }
int sys_setuid(int uid, const char *password) { return syscall(SYSCALL_SETUID, uid, (int)password, 0); }

void read_line(char *buf, int hide) {
    int idx = 0;
    while (1) {
        char c = get_keyboard_char();
        if (c == '\n' || c == '\r') { buf[idx] = '\0'; printk("\n"); break; } 
        else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } } 
        else if (c >= 32 && c <= 126 && idx < 254) {
            char str[2] = { hide ? '*' : c, '\0' }; 
            printk(str); 
            buf[idx++] = c;
        }
    }
}

void main(void) {
    char user_buf[32];
    char pass_buf[32];
    int current_uid = -1;
    char current_username[32];

    syscall(10, 0, 0, 0); // CLEAR SCREEN
    printk("======================================\n");
    printk("       esdumanOS Login Ekranina       \n");
    printk("             Hos Geldiniz!            \n");
    printk("======================================\n\n");

    int login_success = 0;
    while (!login_success) {
        printk("login: ");
        read_line(user_buf, 0); 

        printk("password: ");
        read_line(pass_buf, 1);

        int uid = syscall(SYSCALL_AUTH, (int)user_buf, (int)pass_buf, 0);

        if (uid >= 0) {
            current_uid = uid;
            ft_strcpy(current_username, user_buf);
            login_success = 1;
        } else {
            printk("\n[HATA] Gecersiz kullanici adi veya sifre!\n\n");
        }
    }

    if (current_uid == 0) {
        sys_setuid(0, pass_buf); 
    } else {
        sys_setuid(current_uid, ""); 
    }

    // [KRİTİK]: Doğrulama başarılıysa Shell'i (sh) başlat!
    // sys_get_dir_id ve syscall(5) = EXEC ile /bin/sh.elf dosyasını çalıştıracak
    int bin_id = syscall(29, (int)"bin", 0, 0); 
    
    // Eğer Shell çalıştırılabilirse bu fonksiyondan geriye hiç dönmez, Shell devam eder.
    int res = syscall(5, (int)"sh.elf", bin_id, (int)user_buf);
    
    if (res == -1) {
        printk("\n[KRITIK HATA] /bin/sh.elf bulunamadi veya calistirilamadi!\n");
        printk("Sistem durduruldu.\n");
    }
}

void _start(void) {
    main();
    sys_exit();
}