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
#include "uaccess.h"
#include "entropy.h"
#include "pbkdf2.h"
uint32_t kernel_stack_ring0[1024];
multiboot_info_t global_mboot_info;
char global_cmdline[256];
int is_test_mode = 0;

/**
 * @brief early_get_kbd_char
 * @return static char
 */
static char early_get_kbd_char(void) {
    while(1) {
        char c = get_keyboard_char();  // Read from IRQ-filled ring buffer
        if (c != 0) return c;
        asm volatile("hlt");  // Wait for next interrupt (keyboard IRQ fills buffer)
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
    //
    // EBX is bound explicitly with "=b". The previous form asked for "=r" and
    // guarded EBX by hand:
    //
    //     "pushl %%ebx; cpuid; movl %%ebx, %1; popl %%ebx"
    //
    // which lets the compiler pick %ebx itself for %1. When it does, the movl
    // is a no-op and the popl restores the pre-CPUID value over the result, so
    // the feature word read back as whatever happened to be in EBX. Detection
    // silently failed and neither bit was ever set, on any CPU. The hand-rolled
    // save is only needed for PIC code, and this kernel is built -fno-pic.
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0), "c"(0));

    uint32_t max_leaf = eax;
    if (max_leaf >= 7) {
        asm volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));

        uint32_t cr4;
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        int cr4_updated = 0;

        if (ebx & (1 << 7)) { // SMEP
            cr4 |= (1 << 20);
            cr4_updated = 1;
        }
        if (ebx & (1 << 20)) { // SMAP
            cr4 |= (1 << 21);
            cr4_updated = 1;
        }

        if (cr4_updated) {
            asm volatile("mov %0, %%cr4" :: "r"(cr4));
        }

        // Report what the hardware actually accepted, not what was requested.
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        printk("  CPU Protections     : SMEP %s | SMAP %s (CPUID.7:EBX=0x%x CR4=0x%x)\n",
               (cr4 & (1 << 20)) ? "ON " : "off",
               (cr4 & (1 << 21)) ? "ON " : "off",
               ebx, cr4);

        uaccess_set_smap_enabled((cr4 & (1 << 21)) != 0);
    } else {
        printk("  CPU Protections     : SMEP/SMAP unavailable (CPUID max leaf %d < 7)\n", max_leaf);
        uaccess_set_smap_enabled(0);
    }

    // --- SEC-3: ENABLE CR0.WP ---
    // With WP clear, the read/write bit in a page table entry is ignored for
    // supervisor accesses: a read-only user mapping is then only read-only for
    // Ring 3, and any kernel write through a user pointer silently corrupts the
    // page instead of faulting. Unlike SMEP/SMAP this needs no CPU feature bit
    // and is therefore always available.
    //
    // Every kernel mapping is created read/write (boot.asm's tables, the page
    // tables built by init_paging(), and the heap), so nothing the kernel writes
    // to itself is affected. The one caller that relied on the old behaviour is
    // the ELF loader, which now maps segments writable while copying and applies
    // the real permissions afterwards.
    {
        uint32_t cr0_wp;
        asm volatile("mov %%cr0, %0" : "=r"(cr0_wp));
        cr0_wp |= (1u << 16);
        asm volatile("mov %0, %%cr0" :: "r"(cr0_wp));

        asm volatile("mov %%cr0, %0" : "=r"(cr0_wp));
        if (cr0_wp & (1u << 16)) {
            serial_print("[KERNEL] CR0.WP Activated.\n");
        } else {
            serial_print("[KERNEL] WARNING: CR0.WP could not be enabled!\n");
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
    init_elf_master_key();

    if (mboot_info->flags & 0x00000004) {
        char *cmd = (char *)mboot_info->cmdline;
        int i = 0;
        while (cmd[i] && i < 255) { global_cmdline[i] = cmd[i]; i++; }
        global_cmdline[i] = '\0';
        global_mboot_info.cmdline = (uint32_t)global_cmdline;
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
    
    /*
     * Now that the kernel page directory exists, arm the double fault task. It
     * has to come after paging because the TSS carries the CR3 the handler runs
     * under; before this point a kernel stack overflow triple faults and the
     * machine simply resets with nothing on the wire.
     */
    init_double_fault_handler();

    serial_print("[MAIN-DBG] Checked CR0. Now printing OK...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Virtual Memory (Paging) Activated\n");
    
    serial_print("[MAIN-DBG] Printk finished. Now init_kheap...\n");
    
    init_kheap();
    void *heap_test = kmalloc(16);
    if (!heap_test) return -1;
    kfree(heap_test);

    init_timer(TIMER_HZ);
    return 0;
}
/**
 * @brief Early boot keyboard character reader for password input.
 * Reads one character, echoes '*' for password hiding.
 */
static void early_read_password(char *buf, int max_len) {
    int idx = 0;
    while (1) {
        char c = early_get_kbd_char();
        if (c == '\n' || c == '\r') {
            buf[idx] = '\0';
            printk("\n");
            break;
        } else if (c == '\b' && idx > 0) {
            idx--;
            terminal_putchar('\b'); terminal_putchar(' '); terminal_putchar('\b');
        } else if (c >= 32 && c <= 126 && idx < max_len - 1) {
            buf[idx++] = c;
            terminal_putchar('*');
        }
    }
}

/**
 * @brief First boot setup: creates /etc/passwd and /etc/shadow with user-chosen passwords.
 *
 * Called when /etc/setup_complete does not exist.
 * Creates root (UID 0) and esduman (UID 1000) accounts.
 * Passwords are hashed with PBKDF2-HMAC-SHA256 + random salt.
 *
 * @param etc_id The entry ID of the /etc directory
 */
static void first_boot_setup(int etc_id) {
    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    printk("\n");
    printk("  =====================================================\n");
    printk("    esdumanOS First Boot Setup                         \n");
    printk("    Set passwords for system accounts                  \n");
    printk("  =====================================================\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("\n");

    char pass1[64], pass2[64];
    char shadow_buf[512];
    char shadow_content[1024];
    int shadow_offset = 0;
    
    // === ROOT PASSWORD ===
    int root_set = 0;
    for (int attempt = 0; attempt < 3 && !root_set; attempt++) {
        printk("  Set root password: ");
        early_read_password(pass1, 64);
        printk("  Confirm root password: ");
        early_read_password(pass2, 64);
        
        int match = 1;
        for (int i = 0; i < 64; i++) {
            if (pass1[i] != pass2[i]) { match = 0; break; }
            if (pass1[i] == '\0') break;
        }
        
        if (!match) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("  Passwords do not match. Try again.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            continue;
        }
        
        if (pass1[0] == '\0') {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("  Password cannot be empty. Try again.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            continue;
        }
        
        if (create_shadow_entry("root", pass1, 0, shadow_buf, sizeof(shadow_buf)) == 0) {
            // Copy to shadow_content
            int len = 0;
            while (shadow_buf[len]) { shadow_content[shadow_offset++] = shadow_buf[len++]; }
            shadow_content[shadow_offset++] = '\n';
            root_set = 1;
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("  [OK] Root password set.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
    }
    
    if (!root_set) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("\n  [ERROR] Failed to set root password after 3 attempts.\n");
        printk("  System cannot continue. Please reboot.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        while(1) { asm volatile("cli; hlt"); }
    }
    
    // === ESDUMAN PASSWORD ===
    printk("\n");
    int esduman_set = 0;
    for (int attempt = 0; attempt < 3 && !esduman_set; attempt++) {
        printk("  Set esduman password: ");
        early_read_password(pass1, 64);
        printk("  Confirm esduman password: ");
        early_read_password(pass2, 64);
        
        int match = 1;
        for (int i = 0; i < 64; i++) {
            if (pass1[i] != pass2[i]) { match = 0; break; }
            if (pass1[i] == '\0') break;
        }
        
        if (!match) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("  Passwords do not match. Try again.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            continue;
        }
        
        if (pass1[0] == '\0') {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("  Password cannot be empty. Try again.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            continue;
        }
        
        if (create_shadow_entry("esduman", pass1, 1000, shadow_buf, sizeof(shadow_buf)) == 0) {
            int len = 0;
            while (shadow_buf[len]) { shadow_content[shadow_offset++] = shadow_buf[len++]; }
            shadow_content[shadow_offset++] = '\n';
            esduman_set = 1;
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("  [OK] esduman password set.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
    }
    
    if (!esduman_set) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("\n  [ERROR] Failed to set esduman password after 3 attempts.\n");
        printk("  System cannot continue. Please reboot.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        while(1) { asm volatile("cli; hlt"); }
    }
    
    shadow_content[shadow_offset] = '\0';
    
    // Create /etc/passwd
    char *passwd_content = "root:x:0:/root\nesduman:x:1000:/home/esduman\n";
    fs_create_file("passwd", (uint8_t*)passwd_content, ft_strlen(passwd_content), etc_id);
    
    // Create /etc/shadow
    fs_create_file("shadow", (uint8_t*)shadow_content, shadow_offset, etc_id);
    
    // Create /etc/setup_complete (format version flag)
    char *setup_flag = "format=v1";
    fs_create_file("setup_complete", (uint8_t*)setup_flag, ft_strlen(setup_flag), etc_id);
    
    printk("\n[VFS] /etc/passwd and /etc/shadow created with PBKDF2-HMAC-SHA256.\n");
    
    // Zero sensitive buffers
    volatile char *vp;
    vp = pass1; for (int i = 0; i < 64; i++) vp[i] = 0;
    vp = pass2; for (int i = 0; i < 64; i++) vp[i] = 0;
    vp = shadow_buf; for (int i = 0; i < 512; i++) vp[i] = 0;
    vp = shadow_content; for (int i = 0; i < 1024; i++) vp[i] = 0;
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
            // Check if this is first boot (no setup_complete file)
            int setup_idx = fs_get_entry_idx("setup_complete", etc_id);
            if (setup_idx == -1) {
                if (is_test_mode) {
                    // Test mode: auto-create accounts with fixed test password (no keyboard)
                    char shadow_buf[512];
                    char shadow_content[1024];
                    int shadow_offset = 0;

                    if (create_shadow_entry("root", "test", 0, shadow_buf, sizeof(shadow_buf)) == 0) {
                        int len = 0;
                        while (shadow_buf[len]) { shadow_content[shadow_offset++] = shadow_buf[len++]; }
                        shadow_content[shadow_offset++] = '\n';
                    }
                    if (create_shadow_entry("esduman", "test", 1000, shadow_buf, sizeof(shadow_buf)) == 0) {
                        int len = 0;
                        while (shadow_buf[len]) { shadow_content[shadow_offset++] = shadow_buf[len++]; }
                        shadow_content[shadow_offset++] = '\n';
                    }
                    shadow_content[shadow_offset] = '\0';

                    char *passwd_content = "root:x:0:/root\nesduman:x:1000:/home/esduman\n";
                    fs_create_file("passwd", (uint8_t*)passwd_content, ft_strlen(passwd_content), etc_id);
                    fs_create_file("shadow", (uint8_t*)shadow_content, shadow_offset, etc_id);
                    char *setup_flag = "format=v1";
                    fs_create_file("setup_complete", (uint8_t*)setup_flag, ft_strlen(setup_flag), etc_id);
                    printk("[TEST] Auto-created accounts (password: test)\n");
                } else {
                    // First boot: run interactive password setup
                    first_boot_setup(etc_id);
                }
            } else {
                // Verify shadow format version
                printk("[VFS] /etc/setup_complete found. Existing user accounts loaded.\n");
            }
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
                fs_create_file_raw("stat", stat_elf, stat_elf_len, bin_id);
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
    printk("Boot process completed. Dropping to User Mode (Ring 3)...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);


    // is_test_mode was already set early in kernel_main (before filesystem init)
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

    // Parse kernel command line early (before filesystem init needs is_test_mode)
    if (global_mboot_info.flags & 0x00000004) {
        char *cmdline = (char *)global_mboot_info.cmdline;
        if (ft_strstr(cmdline, "selftest") != NULL) {
            is_test_mode = 1;
        }
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
