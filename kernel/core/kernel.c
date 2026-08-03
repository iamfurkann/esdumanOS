/*
 * File: kernel.c
 * Purpose: Main kernel initialization and bootstrap routines.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kernel.h"
#include "init_elf.h"
#include "keyboard.h" 
#include "crypto.h"
#include "serial.h"
#include "fs.h"
#include "kheap.h"
#include "libft.h"
#include "pmm.h"
#include "process.h"
#include "security.h"
#include "tss.h"
uint32_t kernel_stack_ring0[1024];
multiboot_info_t global_mboot_info;
char global_cmdline[256];
int is_test_mode = 0;

/**
 * @brief early_get_kbd_char
 * @return static char
 */
static char early_get_kbd_char(void) {
    const char scancode_ascii[58] = {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
        0, '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
    };
    while(1) {
        if (inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            if (!(scancode & 0x80)) {
                if (scancode < 58 && scancode_ascii[scancode] != 0) {
                    return scancode_ascii[scancode];
                }
            }
        }
    }
}

/**
 * @brief kernel_panic
 * @param message
 */
void kernel_panic(const char *message) {
    asm volatile("cli"); // Freeze the processor, disable interrupts
    kernel_panic_mode = 1; // Prevent printk deadlocks
    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n==================================================\n");
    printk("                KERNEL PANIC!                     \n");
    printk("==================================================\n");
    printk("Boot Error: %s\n", message);
    printk("Kernel halted for system security.\n");
    printk("==================================================\n");
    while (1) { asm volatile("hlt"); }
}

/**
 * @brief spinlock_init
 * @param lock
 */
void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

/**
 * @brief spinlock_acquire
 * @param lock
 */
void spinlock_acquire(spinlock_t *lock) {
    uint32_t current_val = 1;
    while (1) {
        asm volatile("xchg %0, %1" : "+m"(lock->locked), "+r"(current_val) :: "memory");
        if (current_val == 0) break;
        asm volatile("pause");
    }
}

/**
 * @brief spinlock_release
 * @param lock
 */
void spinlock_release(spinlock_t *lock) {
    asm volatile("movl $0, %0" : "=m"(lock->locked) : : "memory");
}



/**
 * @brief get_vfs_id
 * @param name
 * @param parent_id
 * @return int
 */
int get_vfs_id(const char *name, uint8_t parent_id) {
    int idx = fs_get_entry_idx(name, parent_id);
    if (idx != -1) return dir_table[idx].entry_id;
    return -1;
}

/**
 * @brief init_fpu
 */
void init_fpu(void) {
    uint32_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); 
    cr0 |=  (1 << 1); 
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  
    cr4 |= (1 << 10); 
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("fninit");
}

/**
 * @brief kernel_main
 * @param magic
 * @param mboot_info
 */
static int init_boot_verify(multiboot_info_t* mboot_info) {
    init_serial();
    serial_print("\n[KERNEL] COM1 Serial Port Initialized. Logging active.\n");
    terminal_initialize();
    init_stack_protect();

    // --- SEC-2: ENABLE SMEP & SMAP ---
    uint32_t eax, ebx, ecx, edx;
    asm volatile("pushl %%ebx; cpuid; movl %%ebx, %1; popl %%ebx"
                 : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0), "c"(0));
    if (eax >= 7) {
        asm volatile("pushl %%ebx; cpuid; movl %%ebx, %1; popl %%ebx"
                     : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));
        
        uint32_t cr4;
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        int cr4_updated = 0;
        
        if (ebx & (1 << 7)) { // SMEP
            cr4 |= (1 << 20);
            cr4_updated = 1;
            serial_print("[KERNEL] SMEP Activated.\n");
        }
        if (ebx & (1 << 20)) { // SMAP
            cr4 |= (1 << 21);
            cr4_updated = 1;
            serial_print("[KERNEL] SMAP Activated.\n");
        }
        
        if (cr4_updated) {
            asm volatile("mov %0, %%cr4" :: "r"(cr4));
        }
    }

    // --- PUT ON THE ARMOR (GDT AND IDT MUST BE LOADED BEFORE PASSWORD) ---
    init_gdt();
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) gdtr;
    
    asm volatile("sgdt %0" : "=m"(gdtr));
    if (gdtr.limit == 0) return -1;

    init_idt();
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;

    asm volatile("sidt %0" : "=m"(idtr));
    if (idtr.limit == 0) return -1;

    // --- SECURITY AND PASSWORD SCREEN ---
    init_security(mboot_info);

    if (mboot_info->flags & 0x00000004) {
        char *cmd = (char *)mboot_info->cmdline;
        int i = 0;
        while (cmd[i] && i < 255) { global_cmdline[i] = cmd[i]; i++; }
        global_cmdline[i] = '\0';
        global_mboot_info.cmdline = (uint32_t)global_cmdline;
        
        if (ft_strstr(global_cmdline, "lock_kernel")) {
            terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
            printk("\n=======================================================\n");
            printk(" [SYSTEM LOCKED] PASSWORD REQUIRED TO CONTINUE!        \n");
            printk("=======================================================\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("Kernel Password: ");
            
            char input_pass[64];
            int pass_idx = 0;
            while (1) {
                char c = early_get_kbd_char();
                if (c == '\n' || c == '\r') break;
                
                if (c == '\b' && pass_idx > 0) {
                    pass_idx--;
                    terminal_putchar('\b'); terminal_putchar(' '); terminal_putchar('\b');
                }
                else if (c != 0 && c != '\b' && pass_idx < 63) {
                    input_pass[pass_idx++] = c;
                    terminal_putchar('*');
                }
            }
            input_pass[pass_idx] = '\0';
            printk("\n");

            char hash_out[65];
            sha256_to_hex(input_pass, hash_out);
            if (ft_strcmp(hash_out, KERNEL_PASSWORD_HASH) != 0) {
                terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
                printk("\n[ERROR] INCORRECT PASSWORD! KERNEL SELF-DESTRUCTING...\n");
                while(1) { asm volatile("cli; hlt"); }
            }
            derive_master_key(input_pass);
            for (int z = 0; z < 64; z++) input_pass[z] = 0;
            
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("[OK] Password verified. Entering system...\n\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            
            for(int cl=0; cl<10000000; cl++) { asm volatile("pause"); }
            terminal_initialize(); 
        }
    }

    // --- OPERATING SYSTEM INITIALIZATION ---
    char time_buffer[20];
    get_time_string(time_buffer);
    draw_status_bar(OS_VERSION_STR, time_buffer);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("                 __                                       _____   ____       \n");
    printk("                /\\ \\                                     /\\  __`\\/\\  _`\\     \n");
    printk("   __    ____   \\_\\ \\  __  __    ___ ___      __      ___\\ \\ \\/\\ \\ \\,\\L\\_\\   \n");
    printk(" /'__`\\ /',__\\  /'_` \\/\\ \\/\\ \\ /' __` __`\\  /'__`\\  /' _ `\\ \\ \\ \\ \\/_\\__ \\   \n");
    printk("/\\  __//\\__, `\\/\\ \\L\\ \\ \\ \\_\\ \\/\\ \\/\\ \\/\\ \\/\\ \\L\\.\\_/\\ \\/\\ \\ \\ \\_\\ \\/\\ \\L\\ \\ \n");
    printk("\\ \\____\\/\\____/\\ \\___,_\\ \\____/\\ \\_\\ \\_\\ \\_\\ \\__/.\\_\\ \\_\\ \\_\\ \\_____\\ `\\____\\\n");
    printk(" \\/____/\\/___/  \\/__,_ /\\/___/  \\/_/\\/_/\\/_/\\/__/\\/_/\\/_/\\/_/\\/_____/\\/_____/\n");
    printk("                                                                             \n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    printk("  Kernel Architecture : 32-bit (i386)\n");
    printk("  Memory Model        : Paging Enabled (16MB Identity Mapped)\n");
    printk("================================================================================\n\n");
    
    return 0;
}

static int init_memory_subsystem(void) {
    init_pmm(&global_mboot_info);
    if (pmm_get_total_memory() == 0) {
        return -1;
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Physical Memory Manager (PMM) Initialized\n");

    init_paging();
    serial_print("[MAIN-DBG] Returned from init_paging()!\n");

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000)) {
        return -1;
    }
    
    serial_print("[MAIN-DBG] Checked CR0. Now printing OK...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Virtual Memory (Paging) Activated\n");
    
    serial_print("[MAIN-DBG] Printk finished. Now init_kheap...\n");
    
    init_kheap();
    void *heap_test = kmalloc(16);
    if (!heap_test) return -1;
    kfree(heap_test);

    init_timer(100);
    return 0;
}

static int init_filesystem_and_vfs(void) {
    init_kernel_timers();
    register_kernel_timer(1, alarm_demo_callback);

    asm volatile("sti");

    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Kernel Heap & Signal Handlers Registered\n");

    init_fs();

    if (get_vfs_id("bin", 0) == -1) {
        printk("[VFS] Setting up Linux Root Directory Hierarchy (FHS)...\n");
        fs_mkdir("bin", 0); fs_mkdir("dev", 0); fs_mkdir("etc", 0);
        fs_mkdir("home", 0); fs_mkdir("root", 0); fs_mkdir("tmp", 0); fs_mkdir("var", 0);

        int var_id = get_vfs_id("var", 0);
        if (var_id != -1) fs_mkdir("log", var_id);

        int etc_id = get_vfs_id("etc", 0);
        if (etc_id != -1) {
            char *passwd_content = "root:x:0:/root\nesduman:x:1000:/home/esduman\n";
            fs_create_file("passwd", (uint8_t*)passwd_content, ft_strlen(passwd_content), etc_id);
            char *shadow_content = "root:3a9f30b13ed32aca36440492078b0dd63f6cb89947da7a7f70ff036b790fdba9:0\nesduman:a1269a9e20da104354604184eef3a9116f29e6c8f57bfb5fbee3461d9f83deb5:1000\n";
            fs_create_file("shadow", (uint8_t*)shadow_content, ft_strlen(shadow_content), etc_id);
            printk("[VFS] /etc/passwd and /etc/shadow secure databases sealed.\n");
        }

        int home_id = get_vfs_id("home", 0);
        if (home_id != -1) {
            fs_mkdir("esduman", home_id);
            // Fix ownership: /home/esduman should be owned by esduman (UID 1000), not root
            int esduman_dir_id = get_vfs_id("esduman", home_id);
            if (esduman_dir_id != -1) {
                dir_table[esduman_dir_id].owner_uid = 1000;
            }
        }
    }

    int tmp_id = get_vfs_id("tmp", 0);
    if (tmp_id != -1) {
        printk("[VFS] Cleaning /tmp temporary directory (Reboot Flush)...\n");
        for (int i = 0; i < 256; i++) { 
            if (dir_table[i].is_used == 1 && dir_table[i].parent_id == tmp_id) {
                fs_delete(dir_table[i].filename, tmp_id);
            }
        }
    }

    vfs_file_t temp_elf;
    if (fs_open("init.elf", 0,&temp_elf) != E_OK) {
        printk("[VFS] 'init.elf' not found in system, encrypted data in RAM is being written to disk...\n");
        if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("[VFS ERROR] System is in IMMUTABLE mode! 'init.elf' cannot be written to disk.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        } else {
            fs_create_file_raw("init.elf", init_elf, init_elf_len, 0);
            
            int bin_id = get_vfs_id("bin", 0);
            if (bin_id != -1) {
                fs_create_file_raw("sh", sh_elf, sh_elf_len, bin_id);
                fs_create_file_raw("touch", touch_elf, touch_elf_len, bin_id);
                fs_create_file_raw("rm", rm_elf, rm_elf_len, bin_id);
                fs_create_file_raw("mv", mv_elf, mv_elf_len, bin_id);
                fs_create_file_raw("cp", cp_elf, cp_elf_len, bin_id);
                fs_create_file_raw("free", free_elf, free_elf_len, bin_id);
                fs_create_file_raw("whoami", whoami_elf, whoami_elf_len, bin_id);
                fs_create_file_raw("kill", kill_elf, kill_elf_len, bin_id);
                fs_create_file_raw("grep", grep_elf, grep_elf_len, bin_id);
                fs_create_file_raw("head", head_elf, head_elf_len, bin_id);
                fs_create_file_raw("date", date_elf, date_elf_len, bin_id);
                fs_create_file_raw("hello", hello_elf, hello_elf_len, bin_id);
                fs_create_file_raw("clear", clear_elf, clear_elf_len, bin_id);
                fs_create_file_raw("echo", echo_elf, echo_elf_len, bin_id);
            }
            printk("[VFS] Encrypted '/bin' tools successfully written to disk!\n");
        }
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("ATA PIO Disk Driver Loaded & VFS Mounted\n");

    return 0;
}

static int init_userspace(void) {
    init_fpu();
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) return -1;

    init_multitasking();
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Preemptive Multitasking & Scheduler Active\n\n");

    uint32_t k_stack_top = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("Boot process completed. Decrypting encrypted Minishell...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (global_mboot_info.flags & 0x00000004) {
        char *cmdline = (char *)global_mboot_info.cmdline;
        if (ft_strstr(cmdline, "selftest") != NULL) {
            is_test_mode = 1;
        }
        if (is_test_mode) {
            if (run_all_selftests) {
                run_all_selftests();
            } else {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("\n[WARNING] TEST MODE: Test modules not included in this Kernel (Production Build).\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            while(1) { asm volatile("cli; hlt"); }
        }
    }
    asm volatile("sti");
    int shell_idx = load_and_exec_elf("init.elf", 0); 
    if (shell_idx > 0) {
        foreground_task = shell_idx; 
    }
    start_first_task();
    return 0;
}

void kernel_main(uint32_t magic, multiboot_info_t* mboot_info) {
    if (magic != 0x2BADB002) kernel_panic("Multiboot Error: Invalid magic number!");
    global_mboot_info = *mboot_info;
    
    if (init_boot_verify(&global_mboot_info) != 0) {
        kernel_panic("Boot verification failed!");
    }
    if (init_memory_subsystem() != 0) {
        kernel_panic("Memory subsystem initialization failed!");
    }
    if (init_filesystem_and_vfs() != 0) {
        kernel_panic("Filesystem/VFS initialization failed!");
    }
    if (init_userspace() != 0) {
        kernel_panic("Userspace initialization failed!");
    }

    while (1) {
        asm volatile("hlt");
    }
}
