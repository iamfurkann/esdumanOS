/*
 * File: ahci.c
 * Purpose: The SATA controller a machine built this century actually has.
 *
 * This file is part of the esdumanOS test suite.
 *
 * v1.4.0 gave the kernel a way to ask the bus what was attached and, on a q35
 * board, the answer was a storage controller it could name and could not drive.
 * The line it printed said so. This is the driver that makes the line
 * unnecessary.
 *
 * It is deliberately the smallest driver that reads and writes a disk: one
 * controller, one port, one command slot, one sector per command, polled. Every
 * one of those is argued in ahci.h.
 *
 * The rule this file keeps is that no wait in it is unbounded. There are exactly
 * two loops that wait on hardware. One is ahci_wait(), which every state change
 * goes through. The other is the command-completion wait in ahci_run_command(),
 * which cannot use it: it has to watch two things at once - the issued bit
 * clearing, which means finished, and the task file error bit appearing, which
 * means finished badly - and a helper that waits for one value cannot express
 * that. It carries its own deadline, computed the same way.
 *
 * Two is the number to keep an eye on. The IDE driver had five waits, four of
 * them through a bounded helper and one written out by hand with no timeout, and
 * that one hung the machine on exactly the hardware this project is aiming at,
 * from v0.1.0 until v1.4.0 found it.
 */
#include "ahci.h"
#include "pci.h"
#include "paging.h"
#include "pmm.h"
#include "blockdev.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"
#include "rtc.h"
#include "stdio.h"

/** Mapped ABAR. Volatile: every read of it is a question to the hardware. */
static volatile uint8_t *hba = 0;

/** The port carrying the disk, or -1 before one is found. */
static int ahci_port_index = -1;

/** The single page holding the command list, FIS area, table and sector. */
static uint8_t *dma_virt = 0;
static uint32_t dma_phys = 0;

static uint32_t ahci_total_sectors = 0;

/* ── Register access ────────────────────────────────────────────────── */

static uint32_t hba_read(uint32_t off) {
    return *(volatile uint32_t *)(hba + off);
}

static void hba_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(hba + off) = value;
}

/** @brief Absolute offset of a register in the chosen port's block. */
static uint32_t port_off(uint32_t reg) {
    return AHCI_PORT_BASE + ((uint32_t)ahci_port_index * AHCI_PORT_STRIDE) + reg;
}

/**
 * @brief Waits for a register's bits to reach a value, or gives up.
 *
 * Every state change in this driver waits through here. The one wait that does
 * not is the command completion in ahci_run_command(), which watches two
 * conditions rather than one and says so where it is written; it carries the
 * same deadline. Everything else calling a single bounded waiter is the point -
 * the IDE driver had a perfectly good one and a call site that had been written
 * out by hand without it, and that call site was the one that hung.
 *
 * @param off Absolute register offset from the ABAR.
 * @param mask Bits to look at.
 * @param want The value those bits must reach.
 * @param what Logged if they never do.
 * @return E_OK, or E_IO on timeout.
 */
static int ahci_wait(uint32_t off, uint32_t mask, uint32_t want, const char *what) {
    uint32_t start = timer_get_ticks();

    while ((hba_read(off) & mask) != want) {
        if ((timer_get_ticks() - start) > AHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "AHCI", what);
            return E_IO;
        }
        asm volatile("pause");
    }
    return E_OK;
}

/* ── Port start and stop ────────────────────────────────────────────── */

/**
 * @brief Stops the port so its base addresses can be changed.
 *
 * The command list and FIS base registers are only allowed to move while the
 * engines that read them are stopped, and stopping is a request rather than an
 * instruction: ST and FRE are cleared, and CR and FR report when the hardware
 * has actually finished.
 */
static int ahci_port_stop(void) {
    uint32_t cmd = hba_read(port_off(AHCI_PORT_CMD));

    hba_write(port_off(AHCI_PORT_CMD), cmd & ~(uint32_t)(AHCI_CMD_ST | AHCI_CMD_FRE));

    return ahci_wait(port_off(AHCI_PORT_CMD), AHCI_CMD_CR | AHCI_CMD_FR, 0,
                     "Port would not stop; its engines are still running.");
}

/** @brief Starts the port once its bases are installed. */
static int ahci_port_start(void) {
    if (ahci_wait(port_off(AHCI_PORT_CMD), AHCI_CMD_CR, 0,
                  "Port still running when it was asked to start.") != E_OK) {
        return E_IO;
    }

    /*
     * FIS receive before command list, and not in one write. The controller may
     * not be told to run commands before it is able to receive their answers.
     */
    hba_write(port_off(AHCI_PORT_CMD),
              hba_read(port_off(AHCI_PORT_CMD)) | AHCI_CMD_FRE);
    hba_write(port_off(AHCI_PORT_CMD),
              hba_read(port_off(AHCI_PORT_CMD)) | AHCI_CMD_ST);

    return E_OK;
}

/* ── Issuing one command ────────────────────────────────────────────── */

/**
 * @brief Builds a command in slot 0 and waits for the controller to finish it.
 *
 * @param command The ATA command byte.
 * @param lba Starting sector; ignored by IDENTIFY.
 * @param sectors Sectors to move; 0 for IDENTIFY.
 * @param write Non-zero when the transfer goes to the disk.
 * @param bytes Bytes the PRDT should describe.
 * @return E_OK, or a negative errno.
 */
static int ahci_run_command(uint8_t command, uint32_t lba, uint16_t sectors,
                            int write, uint32_t bytes) {
    ahci_cmd_header_t *header = (ahci_cmd_header_t *)(dma_virt + AHCI_OFF_CMD_LIST);
    ahci_cmd_table_t  *table  = (ahci_cmd_table_t *)(dma_virt + AHCI_OFF_CMD_TABLE);
    ahci_fis_h2d_t    *fis    = (ahci_fis_h2d_t *)table->cfis;

    /*
     * Cleared before the command rather than after it. Status bits left over
     * from the previous transfer would be read as this one's outcome, which is
     * the failure mode where a driver reports the error it already handled and
     * misses the one it just caused.
     */
    hba_write(port_off(AHCI_PORT_IS), hba_read(port_off(AHCI_PORT_IS)));
    hba_write(port_off(AHCI_PORT_SERR), hba_read(port_off(AHCI_PORT_SERR)));

    ft_memset(header, 0, sizeof(ahci_cmd_header_t));
    header->cfl_flags = sizeof(ahci_fis_h2d_t) / 4;   /* FIS length in dwords */
    header->flags2    = write ? 0x40 : 0x00;          /* DW0 bit 6: write     */
    header->prdtl     = 1;
    header->ctba      = dma_phys + AHCI_OFF_CMD_TABLE;
    header->ctbau     = 0;

    ft_memset(table, 0, sizeof(ahci_cmd_table_t));
    table->prdt[0].dba = dma_phys + AHCI_OFF_DATA;
    table->prdt[0].dbc = bytes - 1;                   /* count is minus one   */

    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;                             /* this is a command    */
    fis->command  = command;
    fis->device   = 0x40;                             /* LBA mode             */
    fis->lba0     = (uint8_t)(lba & 0xFF);
    fis->lba1     = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3     = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4     = 0;                                /* 32-bit LBAs only     */
    fis->lba5     = 0;
    fis->countl   = (uint8_t)(sectors & 0xFF);
    fis->counth   = (uint8_t)((sectors >> 8) & 0xFF);

    /* The drive has to be idle before it is handed anything. */
    if (ahci_wait(port_off(AHCI_PORT_TFD), AHCI_TFD_BSY | AHCI_TFD_DRQ, 0,
                  "Drive never went idle; command not issued.") != E_OK) {
        return E_IO;
    }

    /*
     * The buffer is filled above and the command is issued here, in that order,
     * and on x86 that is enough. Stores retire in program order, and the page
     * the controller will read is mapped with caching disabled - so there is no
     * dirty cache line holding the command table when the controller goes
     * looking for it. A weaker architecture would need a barrier between these
     * two lines; this one does not, and the reason is written down rather than
     * assumed by whoever ports it.
     */
    hba_write(port_off(AHCI_PORT_CI), 1);

    /*
     * Two ways out, and both are checked. The command-issued bit clearing means
     * the controller is finished; the task file error bit means it finished
     * badly. Waiting only on the first would read a failed transfer's leftover
     * buffer as data - which is the defect v1.2.0 found in the IDE path, arrived
     * at from the other direction.
     */
    uint32_t start = timer_get_ticks();
    while (hba_read(port_off(AHCI_PORT_CI)) & 1) {
        if (hba_read(port_off(AHCI_PORT_IS)) & AHCI_PORT_IS_TFES) {
            klog(LOG_LEVEL_ERROR, "AHCI", "Controller reported a task file error.");
            return E_IO;
        }
        if ((timer_get_ticks() - start) > AHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "AHCI", "Command never completed.");
            return E_IO;
        }
        asm volatile("pause");
    }

    if (hba_read(port_off(AHCI_PORT_TFD)) & AHCI_TFD_ERR) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Drive reported an error on a completed command.");
        return E_IO;
    }

    return E_OK;
}

/* ── The block layer's face ─────────────────────────────────────────── */

static int ahci_bd_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx;

    if (ahci_port_index < 0) return E_NODEV;
    if (buf == 0) return E_INVAL;

    int res = ahci_run_command(ATA_CMD_READ_DMA_EX, lba, 1, 0, 512);

    if (res != E_OK) {
        /*
         * Zeroed on failure, exactly as the IDE driver does, and for the same
         * reason it is not enough on its own: handing back the previous sector's
         * contents would be worse, and zeros are not evidence of anything. The
         * return value is what the caller has to read.
         */
        ft_memset(buf, 0, 512);
        return res;
    }

    ft_memcpy(buf, dma_virt + AHCI_OFF_DATA, 512);
    return E_OK;
}

static int ahci_bd_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx;

    if (ahci_port_index < 0) return E_NODEV;
    if (buf == 0) return E_INVAL;

    ft_memcpy(dma_virt + AHCI_OFF_DATA, buf, 512);

    return ahci_run_command(ATA_CMD_WRITE_DMA_EX, lba, 1, 1, 512);
}

static blockdev_t ahci_blockdev = {
    .name         = "sata0",
    .sector_size  = 512,
    .sector_count = 0,      /* filled once IDENTIFY answers */
    .read         = ahci_bd_read,
    .write        = ahci_bd_write,
    .ctx          = 0
};

/* ── Bring-up ───────────────────────────────────────────────────────── */

/**
 * @brief Picks the first port with a working SATA disk behind it.
 *
 * @return The port index, or -1.
 */
static int ahci_find_port(void) {
    uint32_t implemented = hba_read(AHCI_REG_PI);

    for (int i = 0; i < 32; i++) {
        if (!(implemented & ((uint32_t)1 << i))) continue;

        uint32_t off  = AHCI_PORT_BASE + ((uint32_t)i * AHCI_PORT_STRIDE);
        uint32_t ssts = hba_read(off + AHCI_PORT_SSTS);

        /*
         * Both halves are asked. DET says a device answered; IPM says the link
         * is awake. A port that reports a device on a sleeping link is one that
         * accepts a command and never answers it.
         */
        if ((ssts & 0x0F) != AHCI_DET_PRESENT) continue;
        if (((ssts >> 8) & 0x0F) != AHCI_IPM_ACTIVE) continue;

        /* A CD-ROM answers on these ports too, with a different signature. */
        if (hba_read(off + AHCI_PORT_SIG) != AHCI_SIG_ATA) continue;

        return i;
    }
    return -1;
}

/**
 * @brief Reads the disk's capacity out of its IDENTIFY response.
 *
 * @return Sectors, or 0 when the drive did not answer with a usable figure.
 */
static uint32_t ahci_identify_capacity(void) {
    if (ahci_run_command(AHCI_ATA_IDENTIFY, 0, 0, 0, 512) != E_OK) return 0;

    const uint16_t *id = (const uint16_t *)(dma_virt + AHCI_OFF_DATA);

    /*
     * Words 100-103 hold the 48-bit sector count and words 60-61 the 28-bit one.
     * The larger is preferred where the drive offers it.
     *
     * Read as two 32-bit halves rather than as a uint64_t, and the reason is the
     * link line rather than taste: this kernel is linked without libgcc, so a
     * 64-bit shift the compiler decides to turn into a call to __ashldi3 is a
     * link error rather than slow code. Nothing above here is 64 bits anyway -
     * blockdev_t's sector_count and the superblock's total_sectors are both
     * uint32_t - so the value has to come back down to 32 regardless.
     */
    uint32_t lba48_low  = ((uint32_t)id[101] << 16) | id[100];
    uint32_t lba48_high = ((uint32_t)id[103] << 16) | id[102];
    uint32_t lba28      = ((uint32_t)id[61]  << 16) | id[60];

    /*
     * Clamped rather than wrapped. A disk with more than 2 TB of sectors that
     * silently kept only its low 32 bits would describe a small disk and write
     * to the wrong end of a large one; refusing the excess is a limit, and
     * wrapping is a corruption.
     */
    if (lba48_high != 0) {
        klog(LOG_LEVEL_WARN, "AHCI",
             "Disk is larger than a 32-bit sector count; using the first 2 TB of it.");
        return 0xFFFFFFFFu;
    }

    if (lba48_low != 0) return lba48_low;

    return lba28;
}

int ahci_init(void) {
    const pci_device_t *dev = pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA);

    if (dev == 0) {
        klog(LOG_LEVEL_INFO, "AHCI", "No SATA controller on the bus.");
        return E_NODEV;
    }

    pci_enable_device(dev);

    /* BAR5 is the AHCI register file. The low four bits describe the BAR
     * itself - type and prefetchability - and are not part of the address. */
    uint32_t abar = dev->bar[5] & 0xFFFFFFF0u;

    if (abar == 0) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Controller reports no register base.");
        return E_NODEV;
    }

    hba = (volatile uint8_t *)vmm_map_device(abar, 0x2000);
    if (hba == 0) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Could not map the controller's registers.");
        return E_NOMEM;
    }

    /*
     * AHCI mode asserted, and nothing else touched. A full HBA reset is the more
     * thorough way to reach a known state and is not done here: it resets every
     * port on the controller, and there is no machine available to this project
     * on which the difference could be observed. What is done is what the
     * specification requires before any other register is read.
     */
    hba_write(AHCI_REG_GHC, hba_read(AHCI_REG_GHC) | AHCI_GHC_AE);

    ahci_port_index = ahci_find_port();
    if (ahci_port_index < 0) {
        klog(LOG_LEVEL_INFO, "AHCI", "Controller found, but no port has a disk on it.");
        return E_NODEV;
    }

    /*
     * One frame, and it holds everything. The command list needs 1 KB at a 1 KB
     * boundary, the received FIS area 256 bytes at a 256-byte one, the command
     * table 144 bytes at a 128-byte one, and the sector 512 - 2560 bytes inside
     * a 4096-byte frame with every alignment satisfied. That arithmetic is why
     * there is no contiguous multi-frame allocator in this tree: a frame is
     * contiguous by definition and one is enough.
     */
    dma_phys = pmm_alloc_frame();
    if (dma_phys == 0xFFFFFFFFu) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Out of memory allocating the command area.");
        ahci_port_index = -1;
        return E_NOMEM;
    }

    dma_virt = (uint8_t *)vmm_map_device(dma_phys, PAGE_SIZE);
    if (dma_virt == 0) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Could not map the command area.");
        pmm_free_frame(dma_phys);
        ahci_port_index = -1;
        return E_NOMEM;
    }

    ft_memset(dma_virt, 0, PAGE_SIZE);

    /*
     * From here on a failure abandons the frame rather than freeing it, and that
     * is deliberate. It is mapped into the device window, the window has no way
     * to unmap, and returning the frame to the allocator while a live uncached
     * mapping still points at it would hand somebody else memory that two
     * mappings disagree about. One leaked page on a path that ends in a machine
     * with no disk is the cheaper of the two.
     */
    if (ahci_port_stop() != E_OK) {
        ahci_port_index = -1;
        return E_IO;
    }

    hba_write(port_off(AHCI_PORT_CLB),  dma_phys + AHCI_OFF_CMD_LIST);
    hba_write(port_off(AHCI_PORT_CLBU), 0);
    hba_write(port_off(AHCI_PORT_FB),   dma_phys + AHCI_OFF_FIS);
    hba_write(port_off(AHCI_PORT_FBU),  0);

    /* Interrupts stay masked. This driver polls; a raised line nothing services
     * would be an interrupt storm on a shared PCI line. */
    hba_write(port_off(AHCI_PORT_IE), 0);

    if (ahci_port_start() != E_OK) {
        ahci_port_index = -1;
        return E_IO;
    }

    ahci_total_sectors = ahci_identify_capacity();
    if (ahci_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "AHCI", "Disk did not report a usable capacity.");
        ahci_port_index = -1;
        return E_IO;
    }

    printk("[AHCI] SATA disk on port %d: %d sectors (%d KB) [Polled]\n",
           ahci_port_index, ahci_total_sectors, (ahci_total_sectors / 2));

    ahci_blockdev.sector_count = ahci_total_sectors;

    return blockdev_register(&ahci_blockdev);
}
