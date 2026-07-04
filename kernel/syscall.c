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

extern security_level_t current_sec_level; 
extern void set_security_level(security_level_t level);
extern char get_keyboard_char(void);
extern void exit_current_process(arch_regs_t *regs);
extern int load_and_exec_elf(const char *);
extern int current_task;
extern disk_file_entry_t dir_table[];
extern process_t tasks[];

int is_valid_user_ptr(uint32_t addr) {
    if (addr >= 0x400000 && addr < 0xC0000000) {
        return 1;
    }
    return 0;
}

static uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

void syscall_handler(arch_regs_t *regs) {
    uint32_t syscall_num = regs->eax;

    switch (syscall_num) {

        /* ── 1. GÖREV VE SÜREÇ YÖNETİMİ ──────────────────────────── */
        case SYSCALL_EXIT: {
            extern int current_task;
            printk("\n[KERNEL] PID %d sonlandi.\n", current_task);
            exit_current_process(regs); 
            break;
        }
        case SYSCALL_EXEC: {
            if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
            
            if (tasks[current_task].uid != 0) { 
                regs->eax = -1; 
                printk("ERISIM ENGELLENDI: Bu komut icin ROOT (sudo) yetkisi gereklidir.\n"); 
                break; 
            }
            else { 
                extern int foreground_task;
                extern void sleep_current_task(arch_regs_t *, int);
                int child_idx = load_and_exec_elf((char *)regs->ebx); 
                if (child_idx >= 0) {
                    foreground_task = child_idx;
                    sleep_current_task(regs, 5);
                }
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
            break;
        }

        case SYSCALL_SETUID: {
            extern int current_task;
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
                if (!is_valid_user_ptr((uint32_t)provided_password)) {
                    regs->eax = -1;
                    printk("ERISIM ENGELLENDI: Gecersiz parola adresi!\n");
                    break;
                }
                if (hash_djb2_salted(provided_password) == 0x19E28ECF) { 
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

            if (fd < 0 || fd >= MAX_FD_PER_TASK || !is_valid_user_ptr((uint32_t)buf)) { regs->eax = -1; break; }
            file_descriptor_t *desc = &tasks[current_task].fd_table[fd];

            if (desc->type == FD_TYPE_CONSOLE) {
                char c = get_keyboard_char();
                if (c == 0) { regs->eip -= 2; asm volatile("int $0x80" :: "a"(99)); } 
                else { buf[0] = c; regs->eax = 1; }
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
                } else {
                    regs->eax = ret;
                }
            }
            else { regs->eax = -1; }
            break;
        }

        case SYSCALL_WRITE: { // 4
            int fd = (int)regs->ebx;
            char *buf = (char *)regs->ecx;
            int size = (int)regs->edx;

            if (fd < 0 || fd >= MAX_FD_PER_TASK || !is_valid_user_ptr((uint32_t)buf)) { regs->eax = -1; break; }
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
                } else {
                    regs->eax = ret;
                }
            }
            else { regs->eax = -1; }
            break;
        }

        case SYSCALL_PIPE: { // SYSCALL_PIPE
            uint32_t *fds = (uint32_t *)regs->ebx;
            if (!is_valid_user_ptr((uint32_t)fds)) { regs->eax = -1; break; }
            
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
            if (oldfd < 0 || oldfd >= MAX_FD_PER_TASK || newfd < 0 || newfd >= MAX_FD_PER_TASK) { regs->eax = -1; break; }
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
            if (fd < 0 || fd >= MAX_FD_PER_TASK) { regs->eax = -1; break; }
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
            if (!is_valid_user_ptr(regs->ebx) || !is_valid_user_ptr(regs->ecx)) { 
                regs->eax = -1; break; 
            }
            extern uint32_t ft_strlen(const char *);
            char *filename = (char *)regs->ebx;
            char *content = (char *)regs->ecx;
            uint8_t parent_id = (uint8_t)regs->edx;

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
            if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
            
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
            if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
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
            if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
            uint8_t parent_id = (uint8_t)regs->ecx; // [YENİ]
            extern int fs_delete(const char *, uint8_t);
            regs->eax = fs_delete((char *)regs->ebx, parent_id);
            break;
        }
        
        case SYSCALL_MV_FILE: { // 23
            if (!is_valid_user_ptr(regs->ebx) || !is_valid_user_ptr(regs->ecx)) { 
                regs->eax = -1; break; 
            }
            uint8_t parent_id = (uint8_t)regs->edx; // [YENİ]
            extern int fs_rename(const char *, const char *, uint8_t);
            regs->eax = fs_rename((char *)regs->ebx, (char *)regs->ecx, parent_id);
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
            // [KONTROL]: Dökümü alınacak hedef adres (Hexdump pointer'ı) güvenli mi?
            if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
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
            if (!is_valid_user_ptr(regs->ebx) || !is_valid_user_ptr(regs->ecx)) { 
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
            // [KONTROL]: Sinyal handler adresi güvenli bir User-Space fonksiyonu mu?
            if (!is_valid_user_ptr(regs->ecx)) { regs->eax = -1; break; }
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
                if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
                extern int fs_mkdir(const char *name, uint8_t parent_id);
                regs->eax = fs_mkdir((const char *)regs->ebx, (uint8_t)regs->ecx);
            }
            break;
            
        case SYSCALL_LS_DIR: // 28
            {
                extern void fs_list_dir(uint8_t parent_id);
                fs_list_dir((uint8_t)regs->ebx);
                regs->eax = 0;
            }
            break;
            
        case SYSCALL_GET_DIR_ID: // 29
            {
                if (!is_valid_user_ptr(regs->ebx)) { regs->eax = -1; break; }
                extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
                int idx = fs_get_entry_idx((const char *)regs->ebx, (uint8_t)regs->ecx);
                
                if (idx != -1) {
                    extern disk_file_entry_t dir_table[];
                    if (dir_table[idx].file_type == 1) { 
                        regs->eax = dir_table[idx].entry_id;
                    } else {
                        regs->eax = -1;
                    }
                } else {
                    regs->eax = -1;
                }
            }
            break;

        default: {
            printk("Bilinmeyen Syscall Numarasi: %d\n", syscall_num);
            break;
        }
    }
    extern void check_and_deliver_signals(arch_regs_t *regs);
    check_and_deliver_signals(regs);
}