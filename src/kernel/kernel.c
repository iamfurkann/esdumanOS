#include "kernel.h"

extern void set_kernel_stack(uint32_t stack);
extern void switch_to_user_mode(void *user_function, void *user_stack);
extern void init_multitasking(void);

uint32_t kernel_stack_ring0[1024];
uint32_t user_stack[1024];

extern uint32_t __bss_start;
extern uint32_t __bss_end;

void init_bss(void) {
    uint32_t *bss = &__bss_start;
    uint32_t *bss_end = &__bss_end;
    
    // BSS baslangicindan bitisine kadar her seyi 0 yap
    while (bss < bss_end) {
        *bss = 0;
        bss++;
    }
}

void get_line(char *buffer, int max_size) {
    int idx = 0;
    while (1) {
        uint32_t c = 0;

        asm volatile ("int $0x80" : "=a"(c) : "a"(3));

        if (c != 0) {
            if (c == '\n') {
                char nl[2] = "\n";
                asm volatile ("int $0x80" : : "a"(4), "b"(nl));
                buffer[idx] = '\0';
                return;
            }
            else if (c == '\b') {
                if (idx > 0) {
                    idx--;
                    char bs[4] = "\b \b";
                    asm volatile ("int $0x80" : : "a"(4), "b"(bs));
                }
            }
            else {
                if (idx < max_size - 1) {
                    buffer[idx++] = (char)c;
                    char str[2] = {(char)c, '\0'};
                    asm volatile ("int $0x80" : : "a"(4), "b"(str));
                }
            }
        }
    }
}

void user_shell_process(void) {
    /* DÜZELTME 1: Array ([]) yerine Pointer (*) kullanıyoruz. 
     * Böylece GCC, SSE optimizasyonunu kullanıp stack'i patlatmayacak! */
    char cmd_buf[256];
    char *prompt = "> ";

    while (1) {
        asm volatile ("int $0x80" : : "a"(4), "b"(prompt));
        get_line(cmd_buf, 256);
        if (cmd_buf[0] != '\0') {
            asm volatile ("int $0x80" : : "a"(5), "b"(cmd_buf));
        }
    }
}

void my_alarm_callback(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("\n[SINYAL] 3 Saniyelik Alarm Tetiklendi! Arka planda calistim!\n> ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

//main
/*void	kernel_main(uint32_t magic, multiboot_info_t* mboot_info) {
	init_bss();
    terminal_initialize();
    init_gdt();
    init_idt();
    
    char time_buffer[20];
    get_time_string(time_buffer);
    draw_status_bar(OS_VERSION_STR, time_buffer);

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
    printk("================================================================================\n\n");

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

    if (magic != 0x2BADB002)
        printk("[HATA] Gecersiz Multiboot Sihirli Numarasi: 0x%x\n", magic);
    else {
        init_pmm(mboot_info);
    }
    
    init_paging();
    init_kheap();
    init_signals();
    register_signal(1, my_alarm_callback);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("[ATA PIO] Hard Disk Sektor 0 Okunuyor...\n");

    uint8_t sector_buffer[512];
    ata_read_sector(0, sector_buffer);
    
    extern void init_fs(void);
    init_fs();

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    extern void init_multitasking(void);
    extern int create_process(uint32_t eip, uint32_t esp);
    
    init_multitasking();
    
    uint32_t u_stack_top = (((uint32_t)user_stack + sizeof(user_stack)) & 0xFFFFFFF0) - 4;
    uint32_t k_stack_top = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;

    create_process((uint32_t)user_shell_process, u_stack_top);
    
    set_kernel_stack(k_stack_top);
    printk("[KERNEL] Islemci Ring 3'e firlatiliyor...\n");
    
    switch_to_user_mode(user_shell_process, (void *)u_stack_top);

    printk("HATA: Ring 3 gecisi basarisiz!\n");
    while (1) {
        asm volatile("hlt");
    }
}
*/

void kernel_main(uint32_t magic, multiboot_info_t* mboot_info) {
    init_bss();
    terminal_initialize();
    init_gdt();
    init_idt();
    
    char time_buffer[20];
    get_time_string(time_buffer);
    draw_status_bar(OS_VERSION_STR, time_buffer);

    /* --- PROFESYONEL BOOT EKRANI VE ASCII ART --- */
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

    /* KERNEL MODÜL YÜKLEME SİMÜLASYONU (GERÇEK FONKSİYONLARLA) */
    
    /* 1. Multiboot & PMM */
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    if (magic != 0x2BADB002) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("Multiboot Magic Error!\n");
    } else {
        init_pmm(mboot_info);
        printk("Physical Memory Manager (PMM) Initialized\n");
    }

    /* 2. Paging */
    init_paging();
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Virtual Memory (Paging) Activated\n");

    /* 3. Heap & Signals */
    init_kheap();
    init_signals();
    register_signal(1, my_alarm_callback);
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Kernel Heap & Signal Handlers Registered\n");

    /* 4. ATA PIO & File System */
    uint8_t sector_buffer[512];
    ata_read_sector(0, sector_buffer);
    extern void init_fs(void);
    init_fs();
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("ATA PIO Disk Driver Loaded & VFS Mounted\n");

    /* 5. Multitasking */
    extern void init_multitasking(void);
    extern int create_process(uint32_t eip, uint32_t esp);
    init_multitasking();
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Preemptive Multitasking & Scheduler Active\n\n");

    /* --- RING 3 SHELL'E GEÇİŞ --- */
    
    uint32_t u_stack_top = (((uint32_t)user_stack + sizeof(user_stack)) & 0xFFFFFFF0) - 4;
    uint32_t k_stack_top = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;

    create_process((uint32_t)user_shell_process, u_stack_top);
    set_kernel_stack(k_stack_top);
    
    /* İşlemci Ring 3'e geçmeden hemen önce son mesaj! */
    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("Dropping to User Mode (Ring 3)...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    switch_to_user_mode(user_shell_process, (void *)u_stack_top);

    while (1) {
        asm volatile("hlt");
    }
}

