#include "kernel.h"
#include "fs.h"
#include "process.h"

extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern disk_file_entry_t dir_table[];

#define KLOG_BUFFER_SIZE 4096

static char klog_buffer[KLOG_BUFFER_SIZE];
static uint32_t klog_head = 0;
static uint32_t klog_tail = 0;
static int klog_is_full = 0;

void klog_write_char(char c) {
    if ((c < 32 && c != '\n') || c > 126) return;

    klog_buffer[klog_head] = c;
    klog_head = (klog_head + 1) % KLOG_BUFFER_SIZE;

    if (klog_head == klog_tail) {
        klog_is_full = 1;
        klog_tail = (klog_tail + 1) % KLOG_BUFFER_SIZE;
    }
}

void dump_klog(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("\n--- KERNEL DIAGNOSTIC MESSAGES (dmesg) ---\n");
    
    if (!klog_is_full && klog_head == 0) {
        printk("Log tamponu bos.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    uint32_t current = klog_is_full ? klog_tail : 0;
    
    while (current != klog_head) {
        terminal_putchar(klog_buffer[current]);
        current = (current + 1) % KLOG_BUFFER_SIZE;
    }
    printk("\n--- END OF dmesg ---\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void klog_persist_to_disk(void) {
    int var_idx = fs_get_entry_idx("var", 0);
    if (var_idx == -1) return;
    int var_id = dir_table[var_idx].entry_id;

    int log_idx = fs_get_entry_idx("log", var_id);
    if (log_idx == -1) return;
    int log_parent_id = dir_table[log_idx].entry_id;

    printk("[KERNEL] Volatil dmesg tamponu /var/log/dmesg.log dosyasina aktariliyor...\n");

    extern int fs_delete(const char *name, uint8_t parent_id);
    fs_delete("dmesg.log", log_parent_id);

    char flat_log[KLOG_BUFFER_SIZE];
    uint32_t idx = 0;
    
    uint32_t current = klog_is_full ? klog_tail : 0;
    
    while (current != klog_head) {
        flat_log[idx++] = klog_buffer[current];
        current = (current + 1) % KLOG_BUFFER_SIZE;
    }
    
    uint32_t log_size = idx;

    extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
    
    int res = fs_create_file("dmesg.log", (uint8_t *)flat_log, log_size, log_parent_id);

    if (res == 0) {
        printk("[OK] Kalici log dosyasi basariyla olusturuldu (Sifreli saklama aktif).\n");
    } else {
        printk("[HATA] Log kaliciligi saglanamadi! Hata Kodu: %d\n", res);
    }
}