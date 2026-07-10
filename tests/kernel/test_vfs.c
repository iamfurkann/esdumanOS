#include "ktest.h"
#include "syscall.h" 
#include "fs.h"

extern void ft_strcpy(char *dest, const char *src);
extern int fs_mkdir(const char *name, uint8_t parent_id);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern disk_file_entry_t dir_table[];

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

static inline int sys_create_file(const char *name, const char *content, int parent_id) { 
    return ktest_syscall(8, (int)name, (int)content, parent_id); 
}
static inline int sys_delete_file(const char *name, int parent_id) { 
    return ktest_syscall(22, (int)name, parent_id, 0); 
}
static inline int sys_mkdir(const char *name, int parent_id) { 
    return ktest_syscall(26, (int)name, parent_id, 0); 
}
static inline int sys_get_dir_id(const char *name, int parent_id) { 
    return ktest_syscall(29, (int)name, parent_id, 0); 
}


void test_vfs_boundary_and_depth(void) {
    printk("\n--- VFS Sinir ve Derinlik (OOB) Testleri ---\n");
    serial_print("\n--- VFS Sinir ve Derinlik (OOB) Testleri ---\n");

    int current_parent = 0; 
    int depth_reached = 0;
    char dir_name[8] = "d0";

    for (int i = 0; i < 15; i++) {
        dir_name[1] = '0' + (i % 10); 
        
        int res = fs_mkdir(dir_name, current_parent);
        if (res != 0) break; 

        int new_id = fs_get_entry_idx(dir_name, current_parent);
        if (new_id == -1 || new_id >= 1024) break;

        current_parent = new_id;
        depth_reached++;
    }

    int backtrack_id = current_parent;
    int steps_back = 0;
    int is_corrupted = 0;

    while (backtrack_id != 0) {
        if (backtrack_id < 0 || backtrack_id >= 1024) {
            is_corrupted = 1; break;
        }
        int next_parent = dir_table[backtrack_id].parent_id;
        if (next_parent == backtrack_id && backtrack_id != 0) {
            is_corrupted = 2; break;
        }
        backtrack_id = next_parent;
        steps_back++;
        if (steps_back > depth_reached + 1) {
            is_corrupted = 3; break;
        }
    }

    KTEST_ASSERT(is_corrupted == 0, "[STRICT] Derin dizinlerde 'cd ..' belleği ihlal etmiyor");
    KTEST_ASSERT(backtrack_id == 0, "[STRICT] 'cd ..' zinciri basariyla Root (0) noktasina ulasti");

    int oob_res1 = fs_mkdir("oob_test1", 1024);
    int oob_res2 = fs_mkdir("oob_test2", 2045);
    
    KTEST_ASSERT(oob_res1 != 0, "[SECURITY] VFS sinir-disi (1024) talepleri reddediyor");
    KTEST_ASSERT(oob_res2 != 0, "[SECURITY] VFS sinir-disi (2045) talepleri reddediyor");
}


// ANA VFS TEST FONKSİYONU
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
    KTEST_ASSERT(mkdir_res >= 0, "[STRICT] sys_mkdir basariyla yeni klasor olusturdu");

    int dir_id = sys_get_dir_id(u_dir, 0);
    KTEST_ASSERT(dir_id >= 0, "[STRICT] sys_get_dir_id gecerli ID dondu");

    int fake_dir = sys_get_dir_id(u_fake, 0);
    KTEST_ASSERT(fake_dir == -1, "[STRICT] sys_get_dir_id olmayan klasorde -1 donuyor");

    int file_res = sys_create_file(u_file, u_content, 0);
    KTEST_ASSERT(file_res >= 0, "[STRICT] sys_create_file basariyla sanal dosya yazdi");

    int del_res = sys_delete_file(u_file, 0);
    KTEST_ASSERT(del_res >= 0, "[STRICT] sys_delete_file dosyayi basariyla sildi");

    test_vfs_boundary_and_depth();
}