#include "ktest.h"
#include "syscall.h" 

extern void ft_strcpy(char *dest, const char *src);

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

static inline int sys_create_file(const char *name, const char *content, int parent_id) { return ktest_syscall(8, (int)name, (int)content, parent_id); }
static inline int sys_delete_file(const char *name, int parent_id) { return ktest_syscall(22, (int)name, parent_id, 0); }
static inline int sys_mkdir(const char *name, int parent_id) { return ktest_syscall(26, (int)name, parent_id, 0); }
static inline int sys_get_dir_id(const char *name, int parent_id) { return ktest_syscall(29, (int)name, parent_id, 0); }

void run_vfs_tests(void) {
    printk("\n--- VFS (Sanal Dosya Sistemi) Testleri ---\n");
    serial_print("\n--- VFS (Sanal Dosya Sistemi) Testleri ---\n");

    char *u_dir = (char *)0x500000;
    char *u_fake = (char *)0x500100;
    char *u_file = (char *)0x500200;
    char *u_content = (char *)0x500300;
    
    ft_strcpy(u_dir, "test_dir");
    ft_strcpy(u_fake, "olmayan_klasor");
    ft_strcpy(u_file, "test.txt");
    ft_strcpy(u_content, "Merhaba Test");

    int mkdir_res = sys_mkdir(u_dir, 0);
    KTEST_ASSERT(mkdir_res != -1, "sys_mkdir basariyla yeni klasor olusturdu");

    int dir_id = sys_get_dir_id(u_dir, 0);
    KTEST_ASSERT(dir_id != -1, "sys_get_dir_id olusturulan klasoru bulabiliyor");

    int fake_dir = sys_get_dir_id(u_fake, 0);
    KTEST_ASSERT(fake_dir == -1, "sys_get_dir_id olmayan klasorde guvenli sekilde -1 donuyor");

    int file_res = sys_create_file(u_file, u_content, 0);
    KTEST_ASSERT(file_res != -1, "sys_create_file basariyla sanal dosya yazdi");

    int del_res = sys_delete_file(u_file, 0);
    KTEST_ASSERT(del_res != -1, "sys_delete_file dosyayi diskten/VFS'ten basariyla sildi");
}