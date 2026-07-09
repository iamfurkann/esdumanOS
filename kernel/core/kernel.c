#include "kernel.h"
#include "init_elf.h"

extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern void init_timer(uint32_t freq);
extern void init_fs(void);
extern void init_security(multiboot_info_t *mboot_info);
extern void run_all_selftests(void) __attribute__((weak));
extern uint8_t kernel_master_key[32];
uint32_t kernel_stack_ring0[1024];

extern int fs_mkdir(const char *name, uint8_t parent_id);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern disk_file_entry_t dir_table[];

void init_bss(void) {
    uint32_t *bss = &__bss_start;
    uint32_t *bss_end = &__bss_end;

    if ((uint32_t)bss_end >= 0x1000000) {
        asm volatile("cli; hlt");
    }
    
    while (bss < bss_end) {
        *bss = 0;
        bss++;
    }
}

int get_vfs_id(const char *name, uint8_t parent_id) {
    int idx = fs_get_entry_idx(name, parent_id);
    if (idx != -1) return dir_table[idx].entry_id;
    return -1;
}

void kernel_main(uint32_t magic, multiboot_info_t* mboot_info) {
    init_bss();
    terminal_initialize();
    init_gdt();
    init_idt();

    init_timer(100);
    
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
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    if (magic != 0x2BADB002) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("Multiboot Magic Error!\n");
    } else {
        init_pmm(mboot_info);
        printk("Physical Memory Manager (PMM) Initialized\n");
    }

    init_paging();
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Virtual Memory (Paging) Activated\n");
    
    init_kheap();
    init_signals();
    register_signal(1, alarm_demo_callback);
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Kernel Heap & Signal Handlers Registered\n");

    init_security(mboot_info);
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Security Initialized (Zero-Trust Mode)\n");

    uint8_t sector_buffer[512];
    ata_read_sector(0, sector_buffer);
    init_fs();

    if (get_vfs_id("bin", 0) == -1) {
        printk("[VFS] Linux Kok Dizin Hiyerarsisi (FHS) Kuruluyor...\n");
        fs_mkdir("bin", 0);
        fs_mkdir("dev", 0);
        fs_mkdir("etc", 0);
        fs_mkdir("home", 0);
        fs_mkdir("root", 0);
        fs_mkdir("tmp", 0);
        fs_mkdir("var", 0);

        int var_id = get_vfs_id("var", 0);
        if (var_id != -1) {
            fs_mkdir("log", var_id);
        }

        int etc_id = get_vfs_id("etc", 0);
        if (etc_id != -1) {
            extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
            extern uint32_t ft_strlen(const char *);
            
            char *passwd_content = "root:x:0:/root\nesduman:x:1000:/home/esduman\n";
            fs_create_file("passwd", (uint8_t*)passwd_content, ft_strlen(passwd_content), etc_id);
            
            char *shadow_content = "root:03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4:0\nesduman:03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4:1000\n";
            fs_create_file("shadow", (uint8_t*)shadow_content, ft_strlen(shadow_content), etc_id);
            
            printk("[VFS] /etc/passwd ve /etc/shadow guvenli veritabanlari muhlendi.\n");
        }

        int home_id = get_vfs_id("home", 0);
        if (home_id != -1) {
            fs_mkdir("esduman", home_id);
        }
    }

    int tmp_id = get_vfs_id("tmp", 0);
    if (tmp_id != -1) {
        printk("[VFS] /tmp gecici dizini temizleniyor (Reboot Flush)...\n");
        
        extern disk_file_entry_t dir_table[];
        extern int fs_delete(const char *name, uint8_t parent_id);
        
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) { 
            if (dir_table[i].is_used == 1 && dir_table[i].parent_id == tmp_id) {
                fs_delete(dir_table[i].filename, tmp_id);
            }
        }
    }

    vfs_file_t temp_elf;
    if (fs_open("init.elf", 0,&temp_elf) != E_OK) {
        printk("[VFS] Sistemde 'init.elf' bulunamadi, RAM'deki sifreli veri diske aktariliyor...\n");
        if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("[VFS HATA] Sistem IMMUTABLE modda! 'init.elf' diske yazilamaz.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        } else {
            fs_create_file_raw("init.elf", init_elf, init_elf_len, 0);
            extern const uint8_t hello_elf[]; extern const uint32_t hello_elf_len;
            extern const uint8_t clear_elf[]; extern const uint32_t clear_elf_len;
            extern const uint8_t echo_elf[];  extern const uint32_t echo_elf_len;
            
            int bin_id = get_vfs_id("bin", 0);
            if (bin_id != -1) {
                fs_create_file_raw("hello", hello_elf, hello_elf_len, bin_id);
                fs_create_file_raw("clear", clear_elf, clear_elf_len, bin_id);
                fs_create_file_raw("echo", echo_elf, echo_elf_len, bin_id);
            }
            printk("[VFS] Sifreli '/bin' araclari basariyla diske yazildi!\n");
        }
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("ATA PIO Disk Driver Loaded & VFS Mounted\n");

    asm volatile("cli");

    init_multitasking();
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Preemptive Multitasking & Scheduler Active\n\n");

    uint32_t k_stack_top = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("Boot islemi tamamlandi. Sifreli Minishell cozuluyor...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (mboot_info->flags & 0x00000004) {
        char *cmdline = (char *)mboot_info->cmdline;
        int is_test_mode = 0;
        for (int i = 0; cmdline[i] != '\0'; i++) {
            if (cmdline[i] == 's' && cmdline[i+1] == 'e' && cmdline[i+2] == 'l' && cmdline[i+3] == 'f') {
                is_test_mode = 1; break;
            }
        }
        if (is_test_mode) {
            if (run_all_selftests) {
                run_all_selftests();
            } else {
                terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                printk("\n[DIKKAT] TEST MODU: Test modulleri bu Kernel'e dahil edilmedi (Production Build).\n");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            while(1) { asm volatile("cli; hlt"); }
        }
    }

    extern int foreground_task;
    asm volatile("sti");
    int shell_idx = load_and_exec_elf("init.elf", 0); 
    if (shell_idx > 0) {
        foreground_task = shell_idx; 
    }

    extern void start_first_task(void);
    start_first_task();

    while (1) {
        asm volatile("hlt");
    }
}