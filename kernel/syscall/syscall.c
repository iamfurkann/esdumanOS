#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "tty.h"
#include "io.h"
#include "fs.h"
#include "syscall.h"
#include "keyboard.h"
#include "security.h"
#include "process.h"
#include "pipe.h"
#include "devfs.h"

extern security_level_t current_sec_level; 
extern void set_security_level(security_level_t level);
extern char get_keyboard_char(void);
extern void exit_current_process(arch_regs_t *regs);
extern int load_and_exec_elf(const char *, uint8_t);
extern disk_file_entry_t dir_table[];
extern process_t tasks[];
extern void dump_klog(void);

void print_hexdump(uint32_t addr, int lenght) {
  uint8_t *ptr = (uint8_t *)addr;
  const char hex_chars[] = "0123456789ABCDEF";

  for (int i = 0; i < lenght; i += 16) {
    printk("0x%x: ", (uint32_t)(ptr + i));
    for (int j = 0; j < 16; j++) {
      if (i + j < lenght) {
        uint8_t byte = ptr[i + j];
        printk("%c%c ", hex_chars[byte >> 4], hex_chars[byte & 0x0F]);
      } else {
        printk("  ");
      }
      if (j == 7) printk(" ");
    }
    printk(" |");
    for (int j = 0; j < 16; j++) {
      if (i + j < lenght) {
        uint8_t byte = ptr[i + j];
        if (byte >= 32 && byte <= 126) printk("%c", byte);
        else printk(".");
      }
    }
    printk("|\n");
  }
}

static int vfs_resolve_path(const char *path, int start_dir_id, char *basename) {
    if (!path || !path[0]) return -1;
    
    int current_id = start_dir_id;
    int i = 0;
    
    if (path[0] == '/') {
        current_id = 0; 
        i++;
    }
    
    char token[64];
    for (int k = 0; k < 64; k++) { token[k] = '\0'; basename[k] = '\0'; }
    int t_idx = 0;
    
    while (1) {
        if (path[i] == '/' || path[i] == '\0') {
            token[t_idx] = '\0';
            
            if (path[i] == '\0') {
                int j = 0;
                while (token[j] && j < 63) { basename[j] = token[j]; j++; }
                basename[j] = '\0';
                return current_id;
            }
            
            if (t_idx > 0) {
                if (token[0] == '.' && token[1] == '\0') {
                } 
                else if (token[0] == '.' && token[1] == '.' && token[2] == '\0') {
                    extern disk_file_entry_t dir_table[];
                    if (current_id != 0) {
                        for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
                            if (dir_table[k].entry_id == current_id && dir_table[k].file_type == 1 && dir_table[k].is_used == 1) {
                                current_id = dir_table[k].parent_id;
                                break;
                            }
                        }
                    }
                }
                else {
                    extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
                    int idx = fs_get_entry_idx(token, current_id);
                    if (idx == -1) return -1; // Klasör yok!
                    
                    extern disk_file_entry_t dir_table[];
                    if (dir_table[idx].file_type != 1) return -1;
                    
                    current_id = dir_table[idx].entry_id;
                }
            }
            t_idx = 0;
        } else {
            if (t_idx < 63) token[t_idx++] = path[i];
        }
        i++;
    }
    return -1;
}

static int check_vfs_access(int entry_id, int needs_write) {
    extern process_t tasks[];
    extern disk_file_entry_t dir_table[];

    if (current_task < 0) return 1; 
    uint32_t my_uid = tasks[current_task].uid;
    if (my_uid == 0) return 1;

    int curr = entry_id;
    int is_in_tmp = 0;

    while (curr != 0) {
        int found = 0;
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == curr && dir_table[i].is_used) {
                char *name = dir_table[i].filename;
                
                if (dir_table[i].parent_id == 0 && name[0] == 't' && name[1] == 'm' && name[2] == 'p' && name[3] == '\0') {
                    is_in_tmp = 1;
                }
                
                if (dir_table[i].owner_uid == 0 && name[0]=='r' && name[1]=='o' && name[2]=='o' && name[3]=='t' && name[4]=='\0') {
                    return 0;
                }
                
                curr = dir_table[i].parent_id;
                found = 1;
                break;
            }
        }
        if (!found) break;
    }
    if (is_in_tmp) return 1;

    if (!needs_write) {
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == entry_id && dir_table[i].is_used) {
                char *name = dir_table[i].filename;
                if (dir_table[i].owner_uid == 0 && name[0]=='s' && name[1]=='h' && name[2]=='a' && 
                    name[3]=='d' && name[4]=='o' && name[5]=='w' && name[6]=='\0') {
                    return 0;
                }
                break;
            }
        }
    }

    if (needs_write) {
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == entry_id && dir_table[i].is_used) {
                if (dir_table[i].owner_uid != my_uid) return 0;
                break;
            }
        }
    }
    
    return 1;
}

int validate_user_pointer(const void *ptr, size_t size) {
    uint32_t start_addr = (uint32_t)ptr;
    uint32_t end_addr = start_addr + size;

    if (end_addr < start_addr) {
        return 0; 
    }

    if (start_addr < 0x400000 || end_addr > 0xC0000000) {
        return 0; 
    }

    return 1;
}

int validate_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD_PER_TASK) {
        return 0; // Hata: Sınır dışı
    }
    return 1;
}

uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

void syscall_handler(arch_regs_t *regs) {
    asm volatile("sti");
    uint32_t syscall_num = regs->eax;

    switch (syscall_num) {
        case SYSCALL_EXIT: {
            exit_current_process(regs); 
            return;
        }
        case SYSCALL_EXEC: { // 5
            if ( !validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            char *target_path = (char *)regs->ebx;
            uint8_t calling_dir_id = (uint8_t)regs->ecx;

            char temp_args[128];
            for (int k = 0; k < 128; k++) temp_args[k] = '\0';
            temp_args[0] = '\0';
            char *args_str = (char *)regs->edx; 
            if (args_str && validate_user_pointer((const void *)args_str, 1)) {
                int i = 0;
                while (args_str[i] && i < 127) {
                    temp_args[i] = args_str[i];
                    i++;
                }
                temp_args[i] = '\0';
            }

            char basename[64];
            int parent_id = vfs_resolve_path(target_path, calling_dir_id, basename);
            if (parent_id == -1 || basename[0] == '\0') { regs->eax = -1; break; }

            extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
            extern disk_file_entry_t dir_table[];
            int bin_idx = fs_get_entry_idx("bin", 0);
            int bin_id = (bin_idx != -1) ? dir_table[bin_idx].entry_id : -1;

            if (parent_id != bin_id && tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("exec: Erisim Engellendi! (/bin disindaki programlar icin ROOT yetkisi gerekir)\n"); 
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                break; 
            }

            extern int foreground_task;
            extern void sleep_current_task(arch_regs_t *, int);
            extern int load_and_exec_elf(const char *name, uint8_t parent_id);

            int child_idx = load_and_exec_elf(basename, parent_id); 
            
            if (child_idx >= 0) {
                foreground_task = child_idx;
                int i = 0;
                while (temp_args[i]) {
                    tasks[child_idx].cmd_args[i] = temp_args[i];
                    i++;
                }
                tasks[child_idx].cmd_args[i] = '\0';

                sleep_current_task(regs, 5);
                regs->eax = 0;
                return;
            } else {
                regs->eax = -1;
            }
            break;
        }
        case SYSCALL_SET_PRIORITY: {
            extern void set_task_priority(int pid, uint8_t new_priority);
            set_task_priority((int)regs->ebx, (uint8_t)regs->ecx);
            break;
        }
        case SYSCALL_YIELD: {
            extern void schedule(arch_regs_t *regs);
            schedule(regs);
            return;
        }

        case SYSCALL_SETUID: {
            extern process_t tasks[];
            
            uint32_t requested_uid = (uint32_t)regs->ebx;
            char *provided_password = (char *)regs->ecx;

            if (tasks[current_task].uid == 0) {
                tasks[current_task].uid = requested_uid;
                regs->eax = 0; 
                printk("[SUDO] PID %d yetkisi UID %d olarak degistirildi.\n", current_task, requested_uid);
                break;
            }

            if (requested_uid == 0) {
                if (!validate_user_pointer((const void *)provided_password, 1)) {
                    regs->eax = -1;
                    printk("ERISIM ENGELLENDI: Gecersiz parola adresi!\n");
                    break;
                }

                int p_len = 0;
                while (provided_password[p_len]) p_len++;
                while (p_len > 0 && (provided_password[p_len - 1] == '\n' || provided_password[p_len - 1] == '\r')) {
                    provided_password[p_len - 1] = '\0';
                    p_len--;
                }
                
                extern int verify_user_password(const char *username, const char *password);
                int auth_uid = verify_user_password("root", provided_password);

                if (auth_uid == 0) { 
                    tasks[current_task].uid = 0;
                    regs->eax = 0;
                    printk("[SUDO] Parola dogrulandi. PID %d ROOT yetkisine yukseltildi!\n", current_task);
                } else {
                    regs->eax = -1;
                    printk("ERISIM ENGELLENDI: Yanlis ROOT parolasi!\n");
                }
                break;
            }
            regs->eax = -1; 
            printk("ERISIM ENGELLENDI: Yetki degisikligi icin ROOT olmalisiniz!\n");
            break;
        }

        /* ── 2. GİRİŞ/ÇIKIŞ VE TERMİNAL ──────────────────────────── */
        case SYSCALL_READ: { // 3
            int fd = (int)regs->ebx;
            char *buf = (char *)regs->ecx;
            int size = (int)regs->edx;

            if (!validate_fd(fd) || !validate_user_pointer(buf, size)) {
                regs->eax = -1; // -EFAULT veya -EBADF
                break;
            }
            file_descriptor_t *desc = &tasks[current_task].fd_table[fd];

            if (desc->type == FD_TYPE_CONSOLE) {
                char c = get_keyboard_char();
                if (c == 0) { regs->eip -= 2; asm volatile("int $0x80" :: "a"(99)); } 
                else { buf[0] = c; regs->eax = 1; }
            }
            else if (desc->type == FD_TYPE_FILE) {
                vfs_file_t *f = (vfs_file_t *)desc->ptr;
                extern int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t count);
                int bytes = fs_read(f, (uint8_t *)buf, size);
                regs->eax = bytes;
            }
            else if (desc->type == FD_TYPE_PIPE) {
                extern int pipe_read(pipe_t *p, uint8_t *buf, int size);
                int ret = pipe_read((pipe_t *)desc->ptr, (uint8_t *)buf, size);
                
                if (ret == -11) { 
                    tasks[current_task].state = TASK_WAITING;
                    tasks[current_task].wait_reason = WAIT_IPC;
                    regs->eip -= 2; // Syscall'u başa sar
                    extern void schedule(arch_regs_t *regs);
                    schedule(regs);
                    return;
                } else {
                    regs->eax = ret;
                }
            }
            else if (desc->type == FD_TYPE_DEVICE) {
                extern device_node_t dev_table[];
                int d_idx = desc->ptr;
                if (dev_table[d_idx].read) {
                    regs->eax = dev_table[d_idx].read((uint8_t *)buf, size);
                } else { regs->eax = -1; }
            }
            else { regs->eax = -1; }
            break;
        }

        case SYSCALL_WRITE: { // 4
            int fd = (int)regs->ebx;
            char *buf = (char *)regs->ecx;
            int size = (int)regs->edx;

            if (!validate_fd(fd) || !validate_user_pointer(buf, size)) {
                regs->eax = -1; // -EFAULT veya -EBADF
                break;
            }
            file_descriptor_t *desc = &tasks[current_task].fd_table[fd];

            if (desc->type == FD_TYPE_CONSOLE) {
                extern void terminal_putchar(char c);
                for(int i=0; i<size; i++) terminal_putchar(buf[i]);
                regs->eax = size;
            } 
            else if (desc->type == FD_TYPE_PIPE) {
                extern int pipe_write(pipe_t *p, const uint8_t *buf, int size);
                int ret = pipe_write((pipe_t *)desc->ptr, (uint8_t *)buf, size);

                if (ret == -11) { 
                    tasks[current_task].state = TASK_WAITING;
                    tasks[current_task].wait_reason = WAIT_IPC;
                    regs->eip -= 2; 
                    extern void schedule(arch_regs_t *regs);
                    schedule(regs);
                    return;
                } else {
                    regs->eax = ret;
                }
            }
            else if (desc->type == FD_TYPE_DEVICE) {
                extern device_node_t dev_table[];
                int d_idx = desc->ptr;
                if (dev_table[d_idx].write) {
                    regs->eax = dev_table[d_idx].write((const uint8_t *)buf, size);
                } else { regs->eax = -1; }
            }
            else { regs->eax = -1; }
            break;
        }

        case SYSCALL_PIPE: { // SYSCALL_PIPE
            uint32_t *fds = (uint32_t *)regs->ebx;
            // Bir int dizisi en az 2 eleman (8 byte) gerektirir
            if (!validate_user_pointer((const void *)fds, 8)) { regs->eax = -1; break; }
            
            pipe_t *p = create_pipe();
            if (!p) { regs->eax = -1; break; }

            int fd1 = -1, fd2 = -1;
            for(int i=3; i<MAX_FD_PER_TASK; i++) {
                if (tasks[current_task].fd_table[i].type == FD_TYPE_NONE) {
                    if (fd1 == -1) fd1 = i;
                    else if (fd2 == -1) { fd2 = i; break; }
                }
            }
            if (fd2 == -1) { destroy_pipe(p); regs->eax = -1; break; }

            // OKUMA UCU
            tasks[current_task].fd_table[fd1].type = FD_TYPE_PIPE;
            tasks[current_task].fd_table[fd1].ptr = (uint32_t)p;
            tasks[current_task].fd_table[fd1].mode = 0; // 0 = READ

            // YAZMA UCU
            tasks[current_task].fd_table[fd2].type = FD_TYPE_PIPE;
            tasks[current_task].fd_table[fd2].ptr = (uint32_t)p;
            tasks[current_task].fd_table[fd2].mode = 1; // 1 = WRITE

            fds[0] = fd1; 
            fds[1] = fd2; 
            regs->eax = 0;
            break;
        }

        case SYSCALL_DUP2: { // 37
            int oldfd = (int)regs->ebx;
            int newfd = (int)regs->ecx;
            if (!validate_fd(oldfd) || !validate_fd(newfd)) { regs->eax = -1; break; }
            if (tasks[current_task].fd_table[oldfd].type == FD_TYPE_NONE) { regs->eax = -1; break; }

            uint8_t old_type = tasks[current_task].fd_table[newfd].type;
            if (old_type == FD_TYPE_PIPE) {
                pipe_t *p = (pipe_t *)tasks[current_task].fd_table[newfd].ptr;
                if (p != 0) { 
                    if (tasks[current_task].fd_table[newfd].mode == 1) p->write_refs--; 
                    else p->read_refs--;
                    
                    if (p->read_refs <= 0 && p->write_refs <= 0) {
                        extern void destroy_pipe(pipe_t *p);
                        destroy_pipe(p);
                    }
                }
            }
            tasks[current_task].fd_table[newfd] = tasks[current_task].fd_table[oldfd];
            if (tasks[current_task].fd_table[oldfd].type == FD_TYPE_PIPE) {
                pipe_t *p = (pipe_t *)tasks[current_task].fd_table[oldfd].ptr;
                if (p != 0) {
                    if (tasks[current_task].fd_table[oldfd].mode == 1) p->write_refs++; 
                    else p->read_refs++;
                }
            }
            regs->eax = newfd;
            break;
        }

        case SYSCALL_CLOSE: { // 38
            int fd = (int)regs->ebx;
            if (!validate_fd(fd)) { regs->eax = -1; break; }
            file_descriptor_t *desc = &tasks[current_task].fd_table[fd];
            
            if (desc->type == FD_TYPE_PIPE && desc->ptr != 0) { // [GÜVENLİK AĞI]
                pipe_t *p = (pipe_t *)desc->ptr;
                if (desc->mode == 1) p->write_refs--; 
                else p->read_refs--;
                
                if (p->read_refs <= 0 && p->write_refs <= 0) {
                    extern void destroy_pipe(pipe_t *p);
                    destroy_pipe(p); 
                }
            }
            if (desc->type == FD_TYPE_FILE && desc->ptr != 0) { // DOSYA KAPATMA
                extern void kfree(void *);
                kfree((void *)desc->ptr); // VFS kopyasini RAM'den sil
            }
            // FD'yi tertemiz sıfırla!
            desc->type = FD_TYPE_NONE;
            desc->ptr = 0;
            desc->mode = 0;
            regs->eax = 0;
            break;
        }
        case SYSCALL_CLEAR_SCREEN: {
            terminal_initialize();
            break;
        }
        case SYSCALL_SET_LAYOUT: {
            current_layout = regs->ebx;
            break;
        }

        /* FILE SYSTEM */
        case SYSCALL_CREATE_FILE: { // 8
            if (!validate_user_pointer((const void *)regs->ebx, 1) || !validate_user_pointer((const void *)regs->ecx, 1)) { 
                regs->eax = -1; break; 
            }
            extern uint32_t ft_strlen(const char *);
            char *filename = (char *)regs->ebx;
            char *content = (char *)regs->ecx;
            uint8_t parent_id = (uint8_t)regs->edx;

            if (!check_vfs_access(parent_id, 1)) { // 1 = WRITE
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("Erisim Engellendi: Buraya yazma yetkiniz yok!\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                regs->eax = -1; break;
            }

            extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
            regs->eax = fs_create_file(filename, (uint8_t *)content, ft_strlen(content), parent_id);
            break;
        }
        case SYSCALL_LIST_FILES: {
            extern void fs_list_files(void); 
            fs_list_files();
            break;
        }

        case SYSCALL_CAT_RAW: {
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            
            char *target_file = (char *)regs->ebx;
            uint8_t parent_id = (uint8_t)regs->ecx;
            
            vfs_file_t file;
            extern int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);
            
            if (fs_open(target_file, parent_id, &file) == 0) {
                printk("--- %s [FİZİKSEL DISK HEX DOKUMU] ---\n", file.filename);
                uint8_t chunk[256];
                uint32_t bytes;
                file.current_offset = 0;
                
                extern int fs_read_raw(vfs_file_t *, uint8_t *, uint32_t);
                
                while ((bytes = fs_read_raw(&file, chunk, 256)) > 0) {
                    for (uint32_t i = 0; i < bytes; i++) {
                        printk("%x ", chunk[i]);
                    }
                }
                printk("\n----------------------------------\n");
            } else {
                printk("Hata: '%s' bulunamadi!\n", target_file);
            }
            break;
        }

        case SYSCALL_CAT_FILE: { // 11
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            char *target_file = (char *)regs->ebx;
            uint8_t parent_id = (uint8_t)regs->ecx;
            
            vfs_file_t file;
            extern int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);
            if (fs_open(target_file, parent_id, &file) == 0) {
                printk("--- %s ---\n", file.filename);
                uint8_t chunk[256];
                uint32_t bytes;
                file.current_offset = 0;
                while ((bytes = fs_read(&file, chunk, 256)) > 0) {
                    for (uint32_t i = 0; i < bytes; i++) {
                        if (chunk[i] != '\r' && chunk[i] != '\b' && chunk[i] != '\0') {
                            char temp[2] = { chunk[i], '\0' }; 
                            printk("%s", temp);
                        }
                    }
                }
                printk("\n");
            } else {
                printk("Hata: '%s' bulunamadi!\n", target_file);
            }
            break;
        }

        case SYSCALL_RM_FILE: { // 22
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            uint8_t parent_id = (uint8_t)regs->ecx; 

            if (!check_vfs_access(parent_id, 1)) {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("rm: Erisim Engellendi (Permission Denied). Bu dosyayi silemezsiniz!\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                regs->eax = -1; 
                break;
            }

            extern int fs_delete(const char *, uint8_t);
            regs->eax = fs_delete((char *)regs->ebx, parent_id);
            break;
        }
        
        case SYSCALL_MV_FILE: { // 23
            if (!validate_user_pointer((const void *)regs->ebx, 1) || !validate_user_pointer((const void *)regs->ecx, 1)) { 
                regs->eax = -1; break; 
            }
            uint8_t parent_id = (uint8_t)regs->edx; 

            if (!check_vfs_access(parent_id, 1)) {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("mv: Erisim Engellendi (Permission Denied). Dosya adi degistirilemez!\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                regs->eax = -1; 
                break;
            }

            extern int fs_rename(const char *, const char *, uint8_t);
            regs->eax = fs_rename((char *)regs->ebx, (char *)regs->ecx, parent_id);
            break;
        }

        case SYSCALL_OPEN: {
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            char basename[64];
            for (int k = 0; k < 64; k++) basename[k] = '\0';
            int parent_id = vfs_resolve_path((char*)regs->ebx, (uint8_t)regs->ecx, basename);
            
            if (parent_id == -1 || basename[0] == '\0') { regs->eax = -1; break; }

            extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
            extern disk_file_entry_t dir_table[];
            int dev_idx_vfs = fs_get_entry_idx("dev", 0);
            int dev_id = (dev_idx_vfs != -1) ? dir_table[dev_idx_vfs].entry_id : -1;

            if (parent_id == dev_id) {
                extern int get_device_idx(const char *name);
                int d_idx = get_device_idx(basename);
                
                if (d_idx != -1) {
                    int fd = -1;
                    for (int i = 3; i < MAX_FD_PER_TASK; i++) {
                        if (tasks[current_task].fd_table[i].type == FD_TYPE_NONE) { fd = i; break; }
                    }
                    if (fd != -1) {
                        tasks[current_task].fd_table[fd].type = FD_TYPE_DEVICE;
                        tasks[current_task].fd_table[fd].ptr = d_idx;
                        tasks[current_task].fd_table[fd].mode = 0;
                        regs->eax = fd;
                    } else { regs->eax = -1; }
                } else { regs->eax = -1; }
                break;
            }

            if (!check_vfs_access(parent_id, 0)) {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("cat: Erisim Engellendi (Permission Denied)\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                regs->eax = -1; break;
            }

            int fd = -1;
            for (int i = 3; i < MAX_FD_PER_TASK; i++) {
                if (tasks[current_task].fd_table[i].type == 0) { fd = i; break; }
            }
            if (fd == -1) { regs->eax = -1; break; }

            extern void *kmalloc(uint32_t);
            vfs_file_t *new_file = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
            extern int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);
            
            // Hedef dosya 'basename' içinde, bulunduğu dizin 'parent_id' içinde!
            if (fs_open(basename, parent_id, new_file) == 0) { 
                new_file->current_offset = 0;
                tasks[current_task].fd_table[fd].type = FD_TYPE_FILE;
                tasks[current_task].fd_table[fd].ptr = (uint32_t)new_file;
                tasks[current_task].fd_table[fd].mode = 0;
                regs->eax = fd;
            } else {
                extern void kfree(void *);
                kfree(new_file);
                regs->eax = -1;
            }
            break;
        }

        /* ── 4. BELLEK YÖNETİMİ VE HATA AYIKLAMA ─────────────────── */
        case SYSCALL_STACK_DUMP: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            extern void print_kernel_stack(void); 
            print_kernel_stack();
            break;
        }
        case SYSCALL_MEMINFO: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            else {
                extern uint32_t pmm_get_total_memory(void);
                extern uint32_t pmm_get_free_memory(void);
                printk("RAM: Toplam %d MB | Bos %d MB\n", pmm_get_total_memory()/(1024*1024), pmm_get_free_memory()/(1024*1024));
            }
            break;
        }
        case SYSCALL_TEST_MALLOC: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            else {
                extern void *kmalloc(uint32_t); 
                extern void kfree(void *); 
                extern uint32_t kmalloc_size(void *);
                char *w = (char *)kmalloc(50);
                if (w) { 
                    w[0]='4'; w[1]='2'; w[2]='\0'; 
                    printk("Ayrilan: 0x%x, Boyut: %d\n", (uint32_t)w, kmalloc_size(w)); 
                    kfree(w); 
                }
            }
            break;
        }
        case SYSCALL_HEXDUMP: {
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            else { extern void print_hexdump(uint32_t, int); print_hexdump(regs->ebx, 64); }
            break;
        }

        /* ── 5. IPC VE SİNYAL YÖNETİMİ ───────────────────────────── */
        case SYSCALL_IPC_SEND: {
            extern int send_message(int target_pid, uint32_t payload);
            regs->eax = send_message((int)regs->ebx, regs->ecx);
            break;
        }
        case SYSCALL_IPC_RECEIVE: {
            // İki adres de uint32_t tutacağı için 4 byte boyutlarında olmalı
            if (!validate_user_pointer((const void *)regs->ebx, 4) || !validate_user_pointer((const void *)regs->ecx, 4)) { 
                regs->eax = -1; break; 
            }
            extern int receive_message(uint32_t *sender_out, uint32_t *payload_out);
            regs->eax = receive_message((uint32_t *)regs->ebx, (uint32_t *)regs->ecx);
            break;
        }
        case SYSCALL_ALARM: {
            printk("Alarm kuruldu! 3 saniye sonra calacak...\n");
            extern void schedule_signal(int sig_num, uint32_t delay_ticks);
            schedule_signal(1, 55);
            break;
        }
        case SYSCALL_SIGNAL_REG: {
            if (!validate_user_pointer((const void *)regs->ecx, 1)) { regs->eax = -1; break; }
            extern void register_user_signal(int sig_num, uint32_t handler_addr);
            register_user_signal((int)regs->ebx, (uint32_t)regs->ecx);
            break;
        }
        case SYSCALL_KILL: {
            extern void send_user_signal(int target_pid, int sig_num);
            send_user_signal((int)regs->ebx, (int)regs->ecx);
            break;
        }

        /* ── 6. GÜVENLİK VE GÜÇ YÖNETİMİ ─────────────────────────── */
        case SYSCALL_LOCKDOWN: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Sistemi kilitlemek icin ROOT yetkisi gereklidir.\n"); 
                break; 
            }
            
            if (current_sec_level >= SEC_LEVEL_LOCKDOWN) {
                printk("Sistem zaten SECURE MODE altinda!\n");
            } else {
                set_security_level(SEC_LEVEL_LOCKDOWN);
                terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
                printk("\n[DIKKAT] KERNEL KILITLENDI (SECURE MODE AKTIF)!\n\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;
        }
        case SYSCALL_PANIC: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            else asm volatile("int $0x0");
            break;
        }
        case SYSCALL_REBOOT: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; // E_PERM
                printk("ERISIM ENGELLENDI: Sistemi yeniden baslatmak icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            outb(0x64, 0xFE);
            break;
        }
        case SYSCALL_HALT: {
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; // E_PERM
                printk("ERISIM ENGELLENDI: Sistemi durdurmak icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            printk("Sistem durduruldu.\n"); 
            asm volatile("cli; hlt");
            break;
        }
        
        case SYSCALL_SET_SEC_LEVEL: {
            if (tasks[current_task].uid != 0) {
                regs->eax = -1; // E_PERM
                printk("ERISIM ENGELLENDI: Guvenlik seviyesini (DEFCON) degistirmek icin ROOT yetkisi gereklidir!\n");
                break;
            }
            extern void set_security_level(security_level_t level); 
            set_security_level((security_level_t)regs->ebx);
            regs->eax = 0;
            break;
        }

        case SYSCALL_SIGRETURN: {
            extern void restore_signal_context(arch_regs_t *regs);
            restore_signal_context(regs);
            break;
        }

        case SYSCALL_MKDIR: // 26
            {
                if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
                uint8_t parent_id = (uint8_t)regs->ecx;
                
                if (!check_vfs_access(parent_id, 1)) { // 1 = WRITE İZNİ
                    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                    printk("Erisim Engellendi: Buraya klasor acma yetkiniz yok!\n");
                    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    regs->eax = -1; break;
                }
                
                extern int fs_mkdir(const char *name, uint8_t parent_id);
                regs->eax = fs_mkdir((const char *)regs->ebx, parent_id);
            }
            break;
            
        case SYSCALL_LS_DIR: // 28
            {
                if (!check_vfs_access((uint8_t)regs->ebx, 0)) {
                    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                    printk("ls: Erisim Engellendi (Permission Denied)\n");
                    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    regs->eax = -1; break;
                }
                extern void fs_list_dir(uint8_t parent_id);
                fs_list_dir((uint8_t)regs->ebx);
                regs->eax = 0;
            }
            break;
            
        case SYSCALL_GET_DIR_ID: { // 29
            if (!validate_user_pointer((const void *)regs->ebx, 1)) { regs->eax = -1; break; }
            
            char basename[64];
            int parent_dir_id = vfs_resolve_path((char*)regs->ebx, (uint8_t)regs->ecx, basename);
            if (parent_dir_id == -1) { regs->eax = -1; break; }

            if (basename[0] == '\0' || (basename[0] == '.' && basename[1] == '\0')) {
                regs->eax = parent_dir_id; 
            }
            else if (basename[0] == '.' && basename[1] == '.' && basename[2] == '\0') {
                int final_id = 0; 
                extern disk_file_entry_t dir_table[];
                for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
                    if (dir_table[k].entry_id == parent_dir_id && dir_table[k].file_type == 1 && dir_table[k].is_used == 1) {
                        final_id = dir_table[k].parent_id;
                        break;
                    }
                }
                regs->eax = final_id;
            }
            else {
                extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
                int idx = fs_get_entry_idx(basename, parent_dir_id);
                if (idx != -1) {
                    extern disk_file_entry_t dir_table[];
                    if (dir_table[idx].file_type == 1) regs->eax = dir_table[idx].entry_id;
                    else regs->eax = -1;
                } else {
                    regs->eax = -1;
                }
            }
            if (regs->eax != (uint32_t)-1 && !check_vfs_access(regs->eax, 0)) {
                regs->eax = -1;
            }
            break;
        }
        
        /*LOGS*/
        case SYSCALL_DMESG:
            if (current_task >= 0 && tasks[current_task].uid == 0) {
                dump_klog();
                regs->eax = 0;
            } else {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("dmesg: Erisim reddedildi. Bu islem ROOT yetkisi (UID 0) gerektirir!\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                regs->eax = -1;
            }
            break;
        case SYSCALL_AUTH: { // SYSCALL_AUTH
            if (!validate_user_pointer((const void *)regs->ebx, 1) || !validate_user_pointer((const void *)regs->ecx, 1)) { 
                regs->eax = -1; break; 
            }
            char *user = (char *)regs->ebx;
            char *pass = (char *)regs->ecx;

            int p_len = 0;
            while (pass[p_len]) p_len++;
            while (p_len > 0 && (pass[p_len - 1] == '\n' || pass[p_len - 1] == '\r' || pass[p_len - 1] == ' ')) {
                pass[p_len - 1] = '\0';
                p_len--;
            }

            extern int verify_user_password(const char *username, const char *password);
            regs->eax = verify_user_password(user, pass); // Başarılıysa UID döner, başarısızsa -1
            break;
        }

        case SYSCALL_GET_ARGS: {
            char *buf = (char *)regs->ebx;
            if (!validate_user_pointer((const void *)buf, 1)) { regs->eax = -1; break; }
            
            int i = 0;
            while (tasks[current_task].cmd_args[i] && i < 127) {
                buf[i] = tasks[current_task].cmd_args[i];
                i++;
            }
            buf[i] = '\0';
            regs->eax = i;
            break;
        }
        default: {
            printk("Bilinmeyen Syscall Numarasi: %d\n", syscall_num);
            break;
        }
    }
    extern void check_and_deliver_signals(arch_regs_t *regs);
    check_and_deliver_signals(regs);
}