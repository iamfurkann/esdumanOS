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
char *banner = 
"                 __                                       _____   ____       \n"
"                /\\ \\                                     /\\  __`\\/\\  _`\\     \n"
"   __    ____   \\_\\ \\  __  __    ___ ___      __      ___\\ \\ \\/\\ \\ \\,\\L\\_\\   \n"
" /'__`\\ /',__\\  /'_` \\/\\ \\/\\ \\ /' __` __`\\  /'__`\\  /' _ `\\ \\ \\ \\ \\/_\\__ \\   \n"
"/\\  __//\\__, `\\/\\ \\L\\ \\ \\ \\_\\ \\/\\ \\/\\ \\/\\ \\/\\ \\L\\.\\_/\\ \\/\\ \\ \\ \\_\\ \\/\\ \\L\\ \\ \n"
"\\ \\____\\/\\____/\\ \\___,_\\ \\____/\\ \\_\\ \\_\\ \\_\\ \\__/.\\_\\ \\_\\ \\_\\ \\_____\\ `\\____\\\n"
" \\/____/\\/___/  \\/__,_ /\\/___/  \\/_/\\/_/\\/_/\\/__/\\/_/\\/_/\\/_/\\/_____/\\/_____/\n"
"                                                                             \n";
    
    printk("%s", banner);
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

    vfs_file_t temp_elf;
    if (fs_open("init.elf", 0,&temp_elf) != E_OK) {
        printk("[VFS] Sistemde 'init.elf' bulunamadi, RAM'deki sifreli veri diske aktariliyor...\n");
        if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("[VFS HATA] Sistem IMMUTABLE modda! 'init.elf' diske yazilamaz.\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        } else {
            fs_create_file_raw("init.elf", init_elf, init_elf_len, 0);
            printk("[VFS] Sifreli 'init.elf' basariyla diske yazildi!\n");
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
    int shell_idx = load_and_exec_elf("init.elf"); 
    if (shell_idx > 0) {
        foreground_task = shell_idx; 
    }

    extern void start_first_task(void);
    start_first_task();

    while (1) {
        asm volatile("hlt");
    }
}