/*
 * File: kernel.c
 * Purpose: Main kernel initialization and bootstrap routines.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kernel.h"
#include "init_elf.h"
/* The boot path is where hardware is brought up, so this is where the disk
 * driver is named. Nothing under fs/ includes it any more. */
#include "ata.h"
#include "keyboard.h" 
#include "crypto.h"
#include "entropy.h"
#include "serial.h"
#include "fs.h"
#include "kheap.h"
#include "libft.h"
#include "pmm.h"
#include "process.h"
#include "security.h"
#include "tss.h"
#include "uaccess.h"
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
 * @brief Announces a subsystem coming up: green on screen, a record in the log.
 *
 * The two renderings are deliberately different. The green "[OK] ..." list is
 * boot UI - it is what tells someone watching that the machine is getting
 * further - and dmesg wants the same events as records with a level and a
 * module. Logging them through klog() would print them a second time in a
 * second shape, so the record goes in through klog_record().
 *
 * @param what The subsystem, as it should read in both places.
 */
static void boot_ok(const char *what) {
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("%s\n", what);

    klog_record(LOG_LEVEL_INFO, "BOOT", what);
}

/**
 * @brief Applies the offset in /etc/timezone, if there is one.
 *
 * The offset was a build-time constant, so correcting it meant rebuilding the
 * kernel. It still has a compiled-in default - the first status bar is drawn
 * long before the filesystem is up, and something has to be right for that one
 * second - but from here on the file decides.
 *
 * A missing or unparseable file leaves the default in place rather than failing
 * the boot. A clock three hours out is a nuisance; a kernel that will not start
 * because a text file has a typo in it is worse.
 *
 * The format is a signed hour count, not a zone name. See the file's own header
 * for why.
 */
static void load_timezone(void) {
    vfs_file_t f;

    /* fs_get_entry_idx() rather than get_vfs_id(), which is defined further down
     * this file and has no declaration to be called through from up here. */
    int etc_id = fs_get_entry_idx("etc", 0);
    if (etc_id < 0) return;
    if (fs_open("timezone", (fs_id_t)etc_id, &f) != E_OK) return;

    uint8_t raw[64];
    int got = fs_read(&f, raw, sizeof(raw) - 1);
    if (got <= 0) return;
    raw[got] = '\0';

    /* First line that is not blank and does not begin with '#'. */
    int i = 0;
    while (i < got) {
        while (i < got && (raw[i] == ' ' || raw[i] == '\t')) i++;

        if (raw[i] == '#') {
            while (i < got && raw[i] != '\n') i++;
            i++;
            continue;
        }
        if (raw[i] == '\n' || raw[i] == '\r') { i++; continue; }
        break;
    }
    if (i >= got) return;

    int sign = 1;
    if (raw[i] == '+') i++;
    else if (raw[i] == '-') { sign = -1; i++; }

    if (i >= got || raw[i] < '0' || raw[i] > '9') {
        klog(LOG_LEVEL_WARN, "TIME", "/etc/timezone holds no number; keeping the built-in offset.");
        return;
    }

    int value = 0;
    while (i < got && raw[i] >= '0' && raw[i] <= '9') {
        value = value * 10 + (raw[i] - '0');
        if (value > 99) break;   /* out of range whatever follows */
        i++;
    }

    if (rtc_set_tz_offset(sign * value) != E_OK) {
        klog_int(LOG_LEVEL_WARN, "TIME", "/etc/timezone is out of range; keeping the built-in offset. Read", sign * value);
        return;
    }

    klog_int(LOG_LEVEL_INFO, "TIME", "Timezone offset loaded from /etc/timezone", sign * value);
}

/**
 * @brief get_vfs_id
 * @param name
 * @param parent_id
 * @return int
 */
int get_vfs_id(const char *name, fs_id_t parent_id) {
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
    /*
     * This was init_elf_master_key(), and it filled the file system's key. It
     * fills the embedded-program key now, which is the only thing that key was
     * ever entitled to open: the programs encrypted into this image at build
     * time. The file system's key comes from the disk's own slot, opened with a
     * passphrase, which cannot happen here because there is no disk mounted yet
     * - see unlock_disk_key(), called from init_filesystem_and_vfs().
     */
    init_image_asset_key();

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
    draw_status_bar(OS_STATUS_LABEL, time_buffer);

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
    boot_ok("Physical Memory Manager (PMM) Initialized");

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
    boot_ok("Virtual Memory (Paging) Activated");
    
    serial_print("[MAIN-DBG] Printk finished. Now init_kheap...\n");
    
    init_kheap();
    void *heap_test = kmalloc(16);
    if (!heap_test) return -1;
    kfree(heap_test);

    init_timer(TIMER_HZ);
    return 0;
}
/**
 * @brief Attempts allowed at the disk passphrase prompt before the machine stops.
 *
 * The same three the account prompts allow, and it buys the same thing: a
 * mistyped passphrase is not a lost disk, and an unattended machine is not an
 * oracle somebody can sit in front of. There is no lockout beyond this because
 * there is nowhere to record one that an attacker holding the disk could not
 * simply erase.
 */
#define DISK_PASSPHRASE_ATTEMPTS 3

/**
 * @brief The passphrase a test build uses, since it has nobody to ask.
 *
 * make test_kernel zeroes the disk before every run, so every run is a first
 * boot and would sit at a prompt with no keyboard behind it - the suite would
 * spend QEMU_TEST_TIMEOUT and report "KERNEL HUNG", which is exactly the failure
 * this project has already been fooled by once.
 *
 * It goes through the same PBKDF2, wrap and unwrap that a typed passphrase does;
 * only the source of the characters differs. The iteration count is already
 * reduced in test builds by PBKDF2_DEV_ITERATIONS.
 */
#define TEST_MODE_PASSPHRASE "test"

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
/**
 * @brief Stops the machine with a reason, when boot cannot honestly continue.
 *
 * Not kernel_panic(): nothing has gone wrong with the kernel. The disk could not
 * be opened, which is a thing the user did or a thing that happened to their
 * disk, and dressing it up as a fault would send them looking in the wrong
 * place.
 */
static void halt_with_reason(const char *line1, const char *line2) {
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    printk("\n  %s\n", line1);
    if (line2) printk("  %s\n", line2);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    while (1) { asm volatile("cli; hlt"); }
}

/**
 * @brief Sets the disk passphrase on a file system that has just been created.
 *
 * Asked twice, because a passphrase that protects a disk and was mistyped
 * protects it from its owner. Three attempts at getting the two to agree, then
 * the machine stops - continuing would mean either an unencrypted disk or one
 * whose key nobody knows, and neither is a thing to do quietly.
 */
static void prompt_new_disk_passphrase(void) {
    char pass1[KEYSLOT_MAX_PASSPHRASE];
    char pass2[KEYSLOT_MAX_PASSPHRASE];

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    printk("\n");
    printk("  =====================================================\n");
    printk("    esdumanOS Disk Encryption Setup                    \n");
    printk("    This disk is new. Choose a passphrase for it.      \n");
    printk("  =====================================================\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("\n  The passphrase unlocks the file system at every boot.\n");
    printk("  There is no recovery key: if it is lost, the disk is lost.\n\n");

    for (int attempt = 0; attempt < DISK_PASSPHRASE_ATTEMPTS; attempt++) {
        int match = 1;
        int res;

        printk("  Set disk passphrase: ");
        early_read_password(pass1, KEYSLOT_MAX_PASSPHRASE);
        printk("  Confirm disk passphrase: ");
        early_read_password(pass2, KEYSLOT_MAX_PASSPHRASE);

        for (int i = 0; i < KEYSLOT_MAX_PASSPHRASE; i++) {
            if (pass1[i] != pass2[i]) { match = 0; break; }
            if (pass1[i] == '\0') break;
        }

        if (!match) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("  Passphrases do not match. Try again.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            continue;
        }

        res = fs_keyslot_install(pass1);

        if (res == E_OK) {
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("  [OK] Disk encryption key created.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }

        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        if (res == E_INVAL) {
            printk("  A passphrase is required, and must be shorter than 64 characters.\n");
        } else {
            printk("  Could not create a key: the entropy pool is unavailable.\n");
        }
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    halt_with_reason("No disk passphrase was set after three attempts.",
                     "Nothing has been written to the disk. Reboot to try again.");
}

/**
 * @brief Asks for the passphrase that opens an existing disk.
 *
 * A failure here says the passphrase was rejected *or* the slot is damaged, and
 * does not pretend to tell them apart. It cannot: both arrive as a tag that does
 * not match. What it can say is that the superblock's other fields were readable,
 * which is why the wording puts the passphrase first.
 */
static void prompt_existing_disk_passphrase(void) {
    char pass[KEYSLOT_MAX_PASSPHRASE];

    printk("\n");
    for (int attempt = 0; attempt < DISK_PASSPHRASE_ATTEMPTS; attempt++) {
        int res;

        printk("  Disk passphrase: ");
        early_read_password(pass, KEYSLOT_MAX_PASSPHRASE);

        res = fs_keyslot_unlock(pass);
        if (res == E_OK) {
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("  [OK] Disk unlocked.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }

        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        if (res == E_INVAL) {
            printk("  This disk's key slot is not one this kernel can use.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            halt_with_reason("The key slot names a work factor outside this build's range.",
                             "The disk was written by a different build of esdumanOS.");
        }
        printk("  Passphrase rejected.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    halt_with_reason("Passphrase rejected three times, or the key slot is damaged.",
                     "The disk has not been modified.");
}

/**
 * @brief Puts the file system's data key in the kernel's hands, or stops.
 *
 * Runs between mounting the disk and the first thing that writes to it. The
 * directory table and the allocation table are plaintext, so the mount itself
 * needs no key - but /etc/passwd and /etc/shadow are written through
 * fs_create_file(), which encrypts, so the key has to exist before the first
 * boot sets up accounts.
 */
static void unlock_disk_key(void) {
    if (!fs_mounted) return;

    if (is_test_mode) {
        int res = fs_was_formatted ? fs_keyslot_install(TEST_MODE_PASSPHRASE)
                                   : fs_keyslot_unlock(TEST_MODE_PASSPHRASE);
        if (res != E_OK) {
            klog_int(LOG_LEVEL_ERROR, "SEC", "Test-mode disk key setup failed", res);
        }
        return;
    }

    if (fs_was_formatted) {
        prompt_new_disk_passphrase();
        return;
    }

    if (fs_keyslot_present()) {
        prompt_existing_disk_passphrase();
        return;
    }

    /*
     * Mounted, not formatted by this boot, and carrying no slot. Installing one
     * here would generate a fresh data key and every file already on the disk
     * would decrypt to noise - so this refuses rather than offering a prompt
     * whose successful outcome is silent destruction.
     */
    halt_with_reason("This disk has no key slot and was not created by this boot.",
                     "Refusing to install one: it would replace the key its files use.");
}

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

/**
 * @brief Gives one entry the mode it should have, if it does not already.
 *
 * The comparison is the point. fs_chmod() writes the whole directory table back
 * to the disk, which is 96 sectors through a 64-sector cache, and doing that a
 * dozen times on every boot to set values that were already right would be a
 * measurable cost for no change at all. On a settled system this writes nothing.
 *
 * @param name Entry name.
 * @param parent_id Directory holding it.
 * @param mode Mode it should have.
 */
static void ensure_mode(const char *name, fs_id_t parent_id, uint16_t mode) {
    int idx = fs_get_entry_idx(name, parent_id);

    if (idx < 0) return;
    if ((dir_table[idx].mode & FS_MODE_PERM_MASK) == (mode & FS_MODE_PERM_MASK)) return;

    klog_int(LOG_LEVEL_INFO, "VFS", "Correcting the mode on a system path; entry", dir_table[idx].entry_id);
    fs_chmod(dir_table[idx].entry_id, mode);
}

/**
 * @brief Puts the system's own paths back to the permissions they must have.
 *
 * Runs on every boot rather than only when the entries are created, and that is
 * a security requirement rather than tidiness. v0.9.0 stored a mode on every
 * entry and enforced none of them, so everything it created took the default -
 * `/etc/shadow` included, at 0644. A kernel that started enforcing modes and
 * only stamped newly created entries would, the first time it mounted such a
 * disk, be handing the password database to every user on it.
 *
 * So the rule is applied, not assumed. These are paths whose permissions belong
 * to the operating system rather than to whoever last touched them.
 */
static void apply_system_modes(void) {
    int etc_id = get_vfs_id("etc", 0);
    int home_id = get_vfs_id("home", 0);
    int bin_id = get_vfs_id("bin", 0);

    /* Readable and searchable by everyone, writable by root alone. */
    ensure_mode("bin", 0, 0755);
    ensure_mode("etc", 0, 0755);
    ensure_mode("var", 0, 0755);
    ensure_mode("dev", 0, 0755);
    ensure_mode("home", 0, 0755);

    /*
     * /tmp is the one directory anybody may write in, and 01777 rather than 0777
     * is what makes that survivable. The write bit on a directory is the
     * permission to remove things from it, so at 0777 anybody could delete
     * anybody else's temporary file - documented as a limitation from v0.9.1
     * until the sticky bit arrived to separate the two.
     */
    ensure_mode("tmp", 0, 01777);

    /* Root's home is root's business. */
    ensure_mode("root", 0, 0700);

    if (etc_id >= 0) {
        /* Everyone reads /etc/passwd, as everywhere - it carries no secrets.
         * /etc/shadow carries all of them. */
        ensure_mode("passwd", (fs_id_t)etc_id, 0644);
        ensure_mode("shadow", (fs_id_t)etc_id, 0600);
    }

    /* The entropy seed is root's, for the same reason the shadow file is: a seed
     * anyone can read tells them where the pool started. It is created 0600 and
     * corrected here for a disk that arrived otherwise. */
    int var_id = get_vfs_id("var", 0);
    if (var_id >= 0) {
        ensure_mode(ENTROPY_SEED_NAME, (fs_id_t)var_id, 0600);
    }

    if (home_id >= 0) {
        for (int i = 0; i < fs_max_entries; i++) {
            if (dir_table[i].is_used && dir_table[i].parent_id == (fs_id_t)home_id &&
                dir_table[i].file_type == FT_DIR) {
                ensure_mode(dir_table[i].filename, (fs_id_t)home_id, 0755);
            }
        }
    }

    /*
     * Everything in /bin is a program, and until now not one of them said so.
     * They are written with fs_install_image_asset(), which lands on
     * fs_create_file() and stamps FS_MODE_DEFAULT_FILE - 0644, no execute bit
     * anywhere - and nothing ever looked, because exec decided by which
     * directory a file sat in.
     *
     * That is why this runs before anything consults the bit rather than in the
     * same change: a kernel that started asking for execute permission against
     * a /bin full of 0644 would boot to a shell that could not launch a single
     * command, on every existing disk.
     *
     * init.elf is the same thing at the root. Nothing execs it through the
     * syscall - the boot path calls load_and_exec_elf() directly, below the
     * permission layer - but a file that is a program should read as one.
     */
    if (bin_id >= 0) {
        for (int i = 0; i < fs_max_entries; i++) {
            if (dir_table[i].is_used && dir_table[i].parent_id == (fs_id_t)bin_id &&
                dir_table[i].file_type == FT_REGULAR) {
                ensure_mode(dir_table[i].filename, (fs_id_t)bin_id, 0755);
            }
        }
    }
    ensure_mode("init.elf", 0, 0755);
}

static int init_filesystem_and_vfs(void) {
    init_kernel_timers();
    /*
     * No slot is registered here any more. Slot 1 used to carry
     * alarm_demo_callback(), armed only by SYSCALL_ALARM, which v1.0.0 replaced
     * with a real alarm(seconds) delivering SIG_ALRM from a per-process deadline.
     * The slots stay available to any kernel-side caller that wants one; nothing
     * in the boot path needs one.
     */

    asm volatile("sti");

    boot_ok("Kernel Heap & Signal Handlers Registered");

    /*
     * Bring the disk up before mounting anything on it.
     *
     * This call used to be the first line of init_fs(), which meant the file
     * system knew which driver it was sitting on and named it. It brings the
     * drive up, learns its capacity and registers it as the root block device;
     * init_fs() then asks the block layer how big the disk is and never learns
     * that the answer came from an IDE controller. Moving it here is what makes
     * that true rather than nearly true.
     *
     * A disk that does not answer registers nothing, and init_fs() refuses to
     * mount rather than formatting - see the blank-disk path there.
     */
    ata_identify();

    init_fs();

    /*
     * Between the mount and the first write. Everything below this line that
     * touches /etc goes through fs_create_file(), which encrypts under the
     * security level's default - so the key has to be in hand before the first
     * boot writes a single account.
     */
    unlock_disk_key();

    if (get_vfs_id("bin", 0) == -1) {
        klog(LOG_LEVEL_INFO, "VFS", "Setting up the root directory hierarchy.");
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
                    klog(LOG_LEVEL_INFO, "TEST", "Accounts auto-created with the fixed test password.");
                } else {
                    // First boot: run interactive password setup
                    first_boot_setup(etc_id);
                }
            } else {
                // Verify shadow format version
                klog(LOG_LEVEL_INFO, "VFS", "setup_complete found; existing accounts loaded.");
            }
        }

        int home_id = get_vfs_id("home", 0);
        if (home_id != -1) {
            fs_mkdir("esduman", home_id);
            // Fix ownership: /home/esduman should be owned by esduman (UID 1000), not root
            /* Ownership, and the group with it. The uid was set here from the
             * start; the gid arrived with the v0.9.0 format and was left at
             * root's, which only stopped mattering because the owner check wins
             * before the group one is reached. */
            int esduman_dir_id = get_vfs_id("esduman", home_id);
            if (esduman_dir_id != -1) {
                fs_chown((fs_id_t)esduman_dir_id, 1000, 1000);
            }
        }
    }

    int tmp_id = get_vfs_id("tmp", 0);
    if (tmp_id != -1) {
        klog(LOG_LEVEL_INFO, "VFS", "Cleaning /tmp on boot.");
        /*
         * fs_max_entries, not a number. This loop was written when the table
         * held 256 entries and stayed at 256 when v0.9.0 doubled it, so anything
         * in /tmp landing in a high slot survived the boot that was supposed to
         * clear it - the opposite of what /tmp promises. Writing the count as a
         * literal has been wrong here twice; since v1.3.0 it is not even a
         * constant, because the table is sized from the disk.
         */
        for (int i = 0; i < fs_max_entries; i++) {
            if (dir_table[i].is_used == 1 && dir_table[i].parent_id == tmp_id) {
                fs_delete(dir_table[i].filename, tmp_id);
            }
        }
    }

    vfs_file_t temp_elf;
    if (fs_open("init.elf", 0,&temp_elf) != E_OK) {
        klog(LOG_LEVEL_INFO, "VFS", "init.elf absent; writing the embedded images to disk.");
        if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("[VFS ERROR] System is in IMMUTABLE mode! 'init.elf' cannot be written to disk.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        } else {
            fs_install_image_asset("init.elf", init_elf, init_elf_len, 0);
            
            int bin_id = get_vfs_id("bin", 0);
            if (bin_id != -1) {
                fs_install_image_asset("sh", sh_elf, sh_elf_len, bin_id);
                fs_install_image_asset("touch", touch_elf, touch_elf_len, bin_id);
                fs_install_image_asset("rm", rm_elf, rm_elf_len, bin_id);
                fs_install_image_asset("mv", mv_elf, mv_elf_len, bin_id);
                fs_install_image_asset("cp", cp_elf, cp_elf_len, bin_id);
                fs_install_image_asset("free", free_elf, free_elf_len, bin_id);
                fs_install_image_asset("whoami", whoami_elf, whoami_elf_len, bin_id);
                fs_install_image_asset("kill", kill_elf, kill_elf_len, bin_id);
                fs_install_image_asset("grep", grep_elf, grep_elf_len, bin_id);
                fs_install_image_asset("head", head_elf, head_elf_len, bin_id);
                fs_install_image_asset("wc", wc_elf, wc_elf_len, bin_id);
                fs_install_image_asset("date", date_elf, date_elf_len, bin_id);
                fs_install_image_asset("stat", stat_elf, stat_elf_len, bin_id);
                fs_install_image_asset("edit", edit_elf, edit_elf_len, bin_id);
                fs_install_image_asset("chmod", chmod_elf, chmod_elf_len, bin_id);
                fs_install_image_asset("chown", chown_elf, chown_elf_len, bin_id);
                fs_install_image_asset("hello", hello_elf, hello_elf_len, bin_id);
                fs_install_image_asset("clear", clear_elf, clear_elf_len, bin_id);
                fs_install_image_asset("echo", echo_elf, echo_elf_len, bin_id);
            }

            /*
             * System files under /etc.
             *
             * The directory has existed since the FHS hierarchy was created and
             * held nothing but the password database, so every tool that wanted
             * one of these facts carried it compiled in instead - the shell's
             * prompt had the hostname in a string literal, and the version was
             * readable from Ring 0 only.
             *
             * Written here rather than at first boot so they arrive with the
             * /bin tools they belong to. That means the same caveat: this block
             * runs only when init.elf is absent, so a disk image carried over
             * from an earlier version keeps its old /etc until it is recreated.
             *
             * fs_create_file() rather than the _raw() above: these are text, and
             * they have to come back out through the ordinary decrypting read
             * that cat and the shell use.
             */
            int sys_etc_id = get_vfs_id("etc", 0);
            if (sys_etc_id != -1) {
                const char *os_release =
                    "NAME=\"esdumanOS\"\n"
                    "VERSION=\"" OS_VERSION_PLAIN "\"\n"
                    "ID=esdumanos\n"
                    "PRETTY_NAME=\"esdumanOS " OS_VERSION_PLAIN "\"\n"
                    "HOME_URL=\"https://github.com/iamfurkann/esdumanOS\"\n";
                fs_create_file("os-release", (const uint8_t *)os_release, ft_strlen(os_release), sys_etc_id);

                const char *hostname = "esdumanOS\n";
                fs_create_file("hostname", (const uint8_t *)hostname, ft_strlen(hostname), sys_etc_id);

                const char *motd =
                    "esdumanOS " OS_VERSION_PLAIN "\n"
                    "Type 'help' for the builtin commands, or look in /bin.\n"
                    "Ctrl-D ends input for a program reading the keyboard.\n";
                fs_create_file("motd", (const uint8_t *)motd, ft_strlen(motd), sys_etc_id);

                /*
                 * Read by the shell at startup. Only "export KEY VALUE" is
                 * recognised - it is a settings file, not a script, and running
                 * arbitrary commands from it would mean forking and exec'ing
                 * before the first prompt appears. The file says so itself, so
                 * nobody has to read the shell to find out.
                 */
                const char *profile =
                    "# Read by /bin/sh at startup.\n"
                    "# Only 'export KEY VALUE' is recognised; '#' begins a comment.\n"
                    "export SHELL /bin/sh\n"
                    "export TERM vga\n";
                fs_create_file("profile", (const uint8_t *)profile, ft_strlen(profile), sys_etc_id);

                /*
                 * An offset, not a zone name, and the file says so.
                 *
                 * /etc/timezone on Linux holds something like "Europe/Istanbul",
                 * which is only meaningful with a timezone database to look it
                 * up in. There is none here and there will not be one - tzdata
                 * is measured in megabytes and the whole disk is two. Writing a
                 * name we could not honour would be worse than writing the
                 * number we actually apply.
                 */
                const char *timezone =
                    "# Hours this machine is ahead of UTC, applied to the RTC.\n"
                    "# An offset and not a zone name: there is no timezone database\n"
                    "# here, so 'Europe/Istanbul' would be a name nothing could read.\n"
                    "# Range -12 to +14. '#' begins a comment.\n"
                    "+3\n";
                fs_create_file("timezone", (const uint8_t *)timezone, ft_strlen(timezone), sys_etc_id);
            }
            klog(LOG_LEVEL_INFO, "VFS", "/bin tools and /etc files written to disk.");
        }
    }

    /*
     * The mode pass runs here, after everything this function creates, and the
     * position is load-bearing rather than arbitrary.
     *
     * It used to run further up, before the block above. That was fine while it
     * only corrected entries that had survived from a previous boot - but the
     * /bin tools and init.elf are written *here*, with
     * fs_install_image_asset(), which stamps the 0644 default and no execute
     * bit. On a fresh disk they
     * therefore arrived after the pass had already gone by and kept 0644 for
     * that whole boot, which the tests caught the moment anything started
     * reading the execute bit.
     *
     * Anything created before this line is covered. Anything created after it is
     * not, and must set its own mode - which is what entropy_persist_seed() does
     * for the seed file on the boot that creates it.
     */
    apply_system_modes();

    /*
     * And the seed, after the modes for the same reason and after the mount
     * because entropy_init() ran back in init_security() when there was no disk
     * to read from. This is the half of the pool's seeding that needs a file
     * system.
     */
    entropy_load_seed();

    load_timezone();

    boot_ok("ATA PIO Disk Driver Loaded & VFS Mounted");

    return 0;
}

static int init_userspace(void) {
    init_fpu();
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) return -1;

    init_multitasking();
    
    boot_ok("Preemptive Multitasking & Scheduler Active");
    printk("\n");

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
        /* The first task founds its own group, so the shell's pgid is its pid. */
        foreground_pgid = (uint32_t)shell_idx;
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
