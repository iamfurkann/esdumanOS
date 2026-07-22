#include "kernel.h"
#include "init_elf.h"
#include "keyboard.h" 
#include "crypto.h"
#include "serial.h"

extern uint32_t _bss_start;
extern uint32_t _bss_end;
extern void init_timer(uint32_t freq);
extern void init_fs(void);
extern void init_security(multiboot_info_t *mboot_info);
extern void run_all_selftests(void) __attribute__((weak));
extern uint8_t kernel_master_key[32];
uint32_t kernel_stack_ring0[1024];

extern int fs_mkdir(const char *name, uint8_t parent_id);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern disk_file_entry_t dir_table[];

multiboot_info_t global_mboot_info;
char global_cmdline[256];
int is_test_mode = 0;

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

void kernel_panic(const char *message) {
    asm volatile("cli"); // İşlemciyi dondur, kesmeleri kapat
    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n==================================================\n");
    printk("                KERNEL PANIC!                     \n");
    printk("==================================================\n");
    printk("Baslatma Hatasi: %s\n", message);
    printk("Sistem guvenligi icin cekirdek durduruldu.\n");
    printk("==================================================\n");
    while (1) { asm volatile("hlt"); }
}

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

void spinlock_acquire(spinlock_t *lock) {
    uint32_t current_val = 1;
    while (1) {
        asm volatile("xchg %0, %1" : "+m"(lock->locked), "+r"(current_val) :: "memory");
        if (current_val == 0) break;
        asm volatile("pause");
    }
}

void spinlock_release(spinlock_t *lock) {
    asm volatile("movl $0, %0" : "=m"(lock->locked) : : "memory");
}

void init_bss(void) {
    uint32_t *bss = &_bss_start;
    uint32_t *bss_end = &_bss_end;
    
    if ((uint32_t)bss_end >= 0x1000000) { asm volatile("cli; hlt"); }
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

void kernel_main(uint32_t magic, multiboot_info_t* mboot_info) {
    init_serial();
    serial_print("\n[KERNEL] COM1 Seri Port Baslatildi. Loglama aktif.\n");
    init_bss();
    terminal_initialize();

    if (magic != 0x2BADB002) kernel_panic("Multiboot Error!");
    global_mboot_info = *mboot_info;

    // --- 1. ZIRHI GİYELİM (GDT VE IDT ŞİFREDEN ÖNCE YÜKLENMELİ) ---
    init_gdt();
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) gdtr;
    
    asm volatile("sgdt %0" : "=m"(gdtr));
    if (gdtr.limit == 0) kernel_panic("GDT donanima yuklenemedi!");

    init_idt();
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;

    asm volatile("sidt %0" : "=m"(idtr));
    if (idtr.limit == 0) kernel_panic("IDT donanima yuklenemedi!");

    // --- 2. GÜVENLİK VE ŞİFRE EKRANI ---
    init_security(&global_mboot_info);

    if (mboot_info->flags & 0x00000004) {
        char *cmd = (char *)mboot_info->cmdline;
        int i = 0;
        while (cmd[i] && i < 255) { global_cmdline[i] = cmd[i]; i++; }
        global_cmdline[i] = '\0';
        global_mboot_info.cmdline = (uint32_t)global_cmdline;

        extern char *ft_strstr(const char *haystack, const char *needle);
        if (ft_strstr(global_cmdline, "lock_kernel")) {
            terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
            printk("\n=======================================================\n");
            printk(" [SISTEM KILITLI] DEVAM ETMEK ICIN PAROLA GEREKLI!     \n");
            printk("=======================================================\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("Kernel Parolasi: ");
            
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
            extern int ft_strcmp(const char *s1, const char *s2);
            if (ft_strcmp(hash_out, "03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4") != 0) {
                terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
                printk("\n[HATA] PAROLA YANLIS! KERNEL KENDINI IMHA EDIYOR...\n");
                while(1) { asm volatile("cli; hlt"); }
            }

            extern void derive_master_key(const char *password);
            derive_master_key(input_pass);
            for (int z = 0; z < 64; z++) input_pass[z] = 0;
            
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("[OK] Parola dogrulandi. Sisteme Geciliyor...\n\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            
            for(int cl=0; cl<10000000; cl++) { asm volatile("pause"); }
            terminal_initialize(); 
        }
    }

    // --- 3. İŞLETİM SİSTEMİ BAŞLATMA (Buradan aşağısı logolar vb. aynı kalacak) ---
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
    
    // 5. PMM (FİZİKSEL BELLEK) DOĞRULAMASI
    init_pmm(&global_mboot_info);
    extern uint32_t pmm_get_total_memory(void);
    if (pmm_get_total_memory() == 0) {
        kernel_panic("PMM: Fiziksel RAM haritasi alinamadi veya bellek yetersiz!");
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Physical Memory Manager (PMM) Initialized\n");

    // 6. VMM (SANAL BELLEK) DOĞRULAMASI
    init_paging();
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000)) {
        kernel_panic("VMM: Paging (Sanal Bellek) aktif edilemedi! Islemci reddetti.");
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Virtual Memory (Paging) Activated\n");
    
    // 7. KERNEL HEAP DOĞRULAMASI
    init_kheap();
    extern void *kmalloc(size_t size);
    extern void kfree(void *);
    void *heap_test = kmalloc(16);
    if (!heap_test) kernel_panic("KHEAP: Kernel bellek tahsis sistemi (malloc) calismiyor!");
    kfree(heap_test);

    init_timer(100);
    extern void init_kernel_timers(void);
    extern void register_kernel_timer(int sig_num, void (*handler)(void));
    
    init_kernel_timers();
    register_kernel_timer(1, alarm_demo_callback);

    asm volatile("sti");

    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Kernel Heap & Signal Handlers Registered\n");

    init_fs();

    if (get_vfs_id("bin", 0) == -1) {
        printk("[VFS] Linux Kok Dizin Hiyerarsisi (FHS) Kuruluyor...\n");
        fs_mkdir("bin", 0); fs_mkdir("dev", 0); fs_mkdir("etc", 0);
        fs_mkdir("home", 0); fs_mkdir("root", 0); fs_mkdir("tmp", 0); fs_mkdir("var", 0);

        int var_id = get_vfs_id("var", 0);
        if (var_id != -1) fs_mkdir("log", var_id);

        int etc_id = get_vfs_id("etc", 0);
        if (etc_id != -1) {
            extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
            extern uint32_t ft_strlen(const char *);
            char *passwd_content = "root:x:0:/root\nesduman:x:1000:/home/esduman\n";
            fs_create_file("passwd", (uint8_t*)passwd_content, ft_strlen(passwd_content), etc_id);
            char *shadow_content = "root:3a9f30b13ed32aca36440492078b0dd63f6cb89947da7a7f70ff036b790fdba9:0\nesduman:a1269a9e20da104354604184eef3a9116f29e6c8f57bfb5fbee3461d9f83deb5:1000\n";
            fs_create_file("shadow", (uint8_t*)shadow_content, ft_strlen(shadow_content), etc_id);
            printk("[VFS] /etc/passwd ve /etc/shadow guvenli veritabanlari muhlendi.\n");
        }

        int home_id = get_vfs_id("home", 0);
        if (home_id != -1) fs_mkdir("esduman", home_id);
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
            extern const uint8_t sh_elf[]; 
            extern const uint32_t sh_elf_len;
            extern const uint8_t hello_elf[]; extern const uint32_t hello_elf_len;
            extern const uint8_t clear_elf[]; extern const uint32_t clear_elf_len;
            extern const uint8_t echo_elf[];  extern const uint32_t echo_elf_len;
            
            int bin_id = get_vfs_id("bin", 0);
            if (bin_id != -1) {
                fs_create_file_raw("sh.elf", sh_elf, sh_elf_len, bin_id);
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

    init_fpu();
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) kernel_panic("FPU: Islemciniz Kayar Nokta (SSE/FXSAVE) destegi saglamiyor!");

    init_multitasking();
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); printk("[OK] ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Preemptive Multitasking & Scheduler Active\n\n");

    uint32_t k_stack_top = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;
    extern void set_kernel_stack(uint32_t stack_top);
    set_kernel_stack(k_stack_top);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("Boot islemi tamamlandi. Sifreli Minishell cozuluyor...\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (global_mboot_info.flags & 0x00000004) {
        char *cmdline = (char *)global_mboot_info.cmdline;
        extern char *ft_strstr(const char *haystack, const char *needle);
        if (ft_strstr(cmdline, "selftest") != NULL) {
            is_test_mode = 1;
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