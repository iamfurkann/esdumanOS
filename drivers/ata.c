#include "ata.h"
#include "io.h"
#include "stdio.h"
#include "klog.h"

extern uint32_t timer_get_ticks(void); 
#define ATA_TIMEOUT_MS 200

volatile int ata_interrupt_fired = 0;
static uint32_t ata_total_sectors = 0;

void ata_irq_handler(void) {
    ata_interrupt_fired = 1;
    inb(ATA_PORT_STATUS); 
}

static int ata_wait_irq(void) {
    uint32_t start_time = timer_get_ticks();
    
    while (!ata_interrupt_fired) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
            klog(LOG_LEVEL_ERROR, "ATA", "IRQ Zaman Asimi! Disk cevap vermiyor.");
            return 0;
        }
        // [DÜZELTME]: Sadece hlt kullanıyoruz. Kesmeleri kapatan 'cli' komutunu sildik.
        // Timer kesmesi zaten işlemciyi her 10ms'de bir uyandırıp döngüyü kontrol ettirecektir.
        asm volatile("hlt"); 
    }
    ata_interrupt_fired = 0;
    return 1;
}

static int ata_wait_bsy() {
    uint32_t start_time = timer_get_ticks();
    
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
            klog(LOG_LEVEL_ERROR, "ATA", "BSY Timeout! Disk mesgul durumdan cikmadi.");
            return 0;
        }
        asm volatile("pause"); 
    }
    return 1;
}

static int ata_wait_drq() {
    uint32_t start_time = timer_get_ticks();
    
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ)) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
            klog(LOG_LEVEL_ERROR, "ATA", "DRQ Timeout! (Veri akisina hazir degil)");
            return 0;
        }
        asm volatile("pause");
    }
    return 1;
}

uint32_t ata_identify(void) {
    // 1. Sürücüyü seç (Drive 0 / Master)
    outb(ATA_PORT_DRV_HEAD, 0xA0);
    
    // [YAMA 1]: Sürücü seçimi yapıldıktan sonra donanımın toparlanması için
    // küçük bir gecikme (delay) eklemek şarttır (QEMU/Bochs emülatörleri için kritik)
    for (int i = 0; i < 1000; i++) {
        inb(ATA_PORT_STATUS); 
    }

    outb(ATA_PORT_SECT_COUNT, 0);
    outb(ATA_PORT_LBA_LOW, 0);
    outb(ATA_PORT_LBA_MID, 0);
    outb(ATA_PORT_LBA_HIGH, 0);
    
    // 2. IDENTIFY Komutunu Gönder (0xEC)
    outb(ATA_PORT_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PORT_STATUS);
    
    // Eğer status 0 dönerse, bu portta bir disk bağlı değil demektir.
    if (status == 0) return 0;

    // [YAMA 2]: IDENTIFY komutundan hemen sonra BSY (Meşgul) bitinin 
    // inmesini beklemek zorundayız. QEMU bazen burada takılabilir.
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        // Döngüde kal, meşguliyetin bitmesini bekle
    }

    // Disk ATAPI (CD-ROM vs) ise kapasite okuyamayız, reddet.
    if (inb(ATA_PORT_LBA_MID) != 0 || inb(ATA_PORT_LBA_HIGH) != 0) return 0;

    // 3. DRQ (Veri Hazır) bitini veya ERR (Hata) bitini bekle
    uint32_t start_time = timer_get_ticks();
    while (1) {
        status = inb(ATA_PORT_STATUS);
        if (status & ATA_SR_ERR) {
            klog(LOG_LEVEL_ERROR, "ATA", "IDENTIFY komutu disk tarafindan reddedildi (ERR Biti).");
            return 0;
        }
        if (status & ATA_SR_DRQ) break; // Veri okumaya hazır
        
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
            klog(LOG_LEVEL_ERROR, "ATA", "IDENTIFY sirasinda DRQ Timeout olustu.");
            return 0;
        }
    }

    // 4. Cihaz bilgilerini (256 adet 16-bit word) oku
    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PORT_DATA);
    }

    // 5. Kapasite hesapla (LBA28 için 60 ve 61. wordler)
    uint32_t total_sectors = (buffer[61] << 16) | buffer[60];
    if (total_sectors > 0) {
        printk("[ATA] Disk tanindi! Kapasite: %d Sektor (%d KB) [IRQ Modu Aktif]\n", 
               total_sectors, (total_sectors * 512) / 1024);
        ata_total_sectors = total_sectors;
        return total_sectors;
    }
    
    return 0;
}

int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk baslatilamadi veya taninmadi! Okuma reddedildi.");
        return 0;
    }
    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] HATA: LBA Siniri Asildi! (Istenen: %d, Maks: %d)\n", lba, ata_total_sectors - 1);
        return 0;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sektor okumasi basladi. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);

    if (!ata_wait_bsy()) {
        extern void *ft_memset(void *, int, uint32_t);
        ft_memset(buffer, 0, 512);
        return 0; 
    }

    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);

    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    asm volatile("cli");
    ata_interrupt_fired = 0;
    outb(ATA_PORT_COMMAND, ATA_CMD_READ_PIO);
    asm volatile("sti");

    if (!ata_wait_irq()) {
        extern void *ft_memset(void *, int, uint32_t);
        ft_memset(buffer, 0, 512);
        return 0;
    }
    
    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Donanim Hatasi: Disk okuma basarisiz! Sektor", lba);
        extern void *ft_memset(void *, int, uint32_t);
        ft_memset(buffer, 0, 512);
        return 0;
    }
    
    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PORT_DATA);
    }
    
    return 1;
}

int ata_write_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk baslatilamadi veya taninmadi! Yazma reddedildi.");
        return 0;
    }

    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] HATA: LBA Siniri Asildi! Yazma Iptal (Istenen: %d, Maks: %d)\n", lba, ata_total_sectors - 1);
        return 0;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sektor yazmasi basladi. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);
    
    if (!ata_wait_bsy()) return 0;
    
    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);
    
    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    outb(ATA_PORT_COMMAND, ATA_CMD_WRITE_PIO);
    if (!ata_wait_bsy()) return 0;
    if (!ata_wait_drq()) return 0;

    uint16_t *ptr = (uint16_t *)buffer;
    asm volatile("cli");
    ata_interrupt_fired = 0;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PORT_DATA, ptr[i]);
    }
    asm volatile("sti");

    if (!ata_wait_irq()) return 0;

    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Donanim Hatasi: Diske yazma basarisiz! Sektor", lba);
        return 0;
    }

    outb(ATA_PORT_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy(); 
    
    ata_interrupt_fired = 0;
    inb(ATA_PORT_STATUS);

    return 1;
}