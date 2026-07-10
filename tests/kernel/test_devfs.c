#include "ktest.h"
#include "stdio.h"
#include "types.h"

extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern int get_device_idx(const char *name);

void run_devfs_tests(void) {
    printk("\n--- DevFS (Device File System) Tests ---\n");
    serial_print("\n--- DevFS (Device File System) Tests ---\n");

    // 1. /dev dizininin VFS üzerinde varlığının kontrolü
    int dev_idx = fs_get_entry_idx("dev", 0);
    KTEST_ASSERT(dev_idx != -1, "VFS root altinda /dev dizini mevcut");

    // 2. Çekirdek Aygıt Kaydı (Device Registration) Kontrolü
    /* TODO: Sürücüler (Drivers) katmanına geçildiğinde burası açılacak!
    int tty_idx = get_device_idx("tty0"); 
    KTEST_ASSERT(tty_idx != -1, "/dev/tty0 aygiti sisteme kayitli");
    */

    // 3. Olmayan Aygıt (Geçersiz Yönlendirme) Koruması
    int fake_idx = get_device_idx("olmayan_aygit_42");
    KTEST_ASSERT(fake_idx == -1, "Gecersiz aygit erisim talebi engellendi (-1)");

    // 4. Klavye aygıtı kontrolü
    /* TODO: PS/2 Klavye sürücüsü yazıldığında burası açılacak!
    int kbd_idx = get_device_idx("keyboard");
    KTEST_ASSERT(kbd_idx != -1, "/dev/keyboard aygiti sisteme kayitli");
    */
}