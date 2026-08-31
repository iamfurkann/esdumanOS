/*
 * File: acpi.c
 * Purpose: Enough ACPI to turn the machine off.
 *
 * This file is part of the esdumanOS test suite.
 *
 * The narrow reading of this file is that it finds one table and reads six
 * fields out of it. The wide reading is that until this release esdumanOS could
 * not end a session: `halt` stopped the processor with the machine still
 * powered, and `reboot` wrote to a keyboard controller that a modern UEFI
 * laptop need not contain. On the hardware this project is aimed at, the only
 * way out was the power button.
 *
 * Three things are deliberately not here. There is no AML interpreter - the one
 * place AML is touched is a byte search for \\_S5_, and acpi_parse_s5() says so
 * in its own name rather than implying otherwise. There is no namespace and no
 * device enumeration; AHCI and xHCI go on polling exactly as they did, and the
 * MADT is not read. And there is no sleep state other than S5, because the
 * others are a power management story and this is an ending one.
 *
 * Every length in every structure below comes from the firmware, which makes
 * each of them a walk condition rather than a fact. That is the same lesson the
 * USB descriptor walk learned in v1.9.0 and the Multiboot 2 tag walk carries one
 * directory over, arriving here from a third direction: a zero length is not a
 * malformed field to step over, it is a loop that never ends.
 */
#include "acpi.h"
#include "types.h"
#include "errno.h"
#include "io.h"
#include "klog.h"
#include "libft.h"
#include "paging.h"

/* What acpi_init() learned, and all that the two exits below need. */
static int      acpi_ready      = 0;
static uint32_t fadt_pm1a_cnt   = 0;
static uint32_t fadt_pm1b_cnt   = 0;
static uint16_t fadt_slp_typa   = 0;
static uint16_t fadt_slp_typb   = 0;
static uint32_t fadt_smi_cmd    = 0;
static uint8_t  fadt_acpi_enable = 0;

static int      reset_supported = 0;
static uint8_t  reset_space     = 0;
static uint32_t reset_port      = 0;
static uint8_t  reset_value     = 0;

/* SCI_EN, bit 0 of PM1_CNT: the firmware has handed power management over. */
#define ACPI_SCI_EN 1u

/*
 * Byte-wise reads, because nothing about an ACPI table promises alignment.
 * The FADT's interesting fields sit at offsets like 89 and 132, and a 32-bit
 * load from an address the firmware chose is a fault waiting for the one machine
 * that lays its tables out differently.
 */
static uint8_t rd8(const uint8_t *p, uint32_t off) {
    return p[off];
}

static uint32_t rd32(const uint8_t *p, uint32_t off) {
    return (uint32_t)p[off]
         | ((uint32_t)p[off + 1] << 8)
         | ((uint32_t)p[off + 2] << 16)
         | ((uint32_t)p[off + 3] << 24);
}

int acpi_checksum_ok(const void *table, uint32_t len) {
    const uint8_t *p = (const uint8_t *)table;
    uint8_t sum = 0;

    if (p == 0 || len == 0) return 0;

    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

int acpi_rsdp_check(const void *rsdp) {
    const acpi_rsdp_t *r = (const acpi_rsdp_t *)rsdp;

    if (r == 0) return -1;
    if (ft_memcmp(r->signature, "RSD PTR ", 8) != 0) return -1;

    /* The first checksum covers the ACPI 1.0 pointer, which is the first twenty
     * bytes of every version of this structure. */
    if (!acpi_checksum_ok(r, 20)) return -1;

    if (r->revision < 2) return 0;

    /*
     * And the second covers the whole of a later one. Both are checked rather
     * than just the first: a revision 2 pointer whose extended half is damaged
     * would pass the twenty-byte sum and hand out an XSDT address that is not
     * one. The length is the firmware's number, so it is bounded before it is
     * used, and bounded to the structure this kernel knows rather than to
     * something merely sane. The pointer handed over by a Multiboot 2 bootloader
     * is copied into a thirty-six byte buffer - the whole of an ACPI 2.0 RSDP -
     * so a length beyond that is not a longer pointer to checksum, it is a read
     * past the end of the copy. A firmware declaring one is refused and said so,
     * which is better than summing whatever follows it.
     */
    if (r->length != sizeof(acpi_rsdp_t)) return -1;
    if (!acpi_checksum_ok(r, r->length)) return -1;

    return 2;
}

int acpi_parse_s5(const uint8_t *aml, uint32_t len,
                  uint16_t *slp_typa, uint16_t *slp_typb) {
    if (aml == 0 || slp_typa == 0 || slp_typb == 0) return E_INVAL;
    if (len < 8) return E_NOENT;

    for (uint32_t i = 0; i + 4 <= len; i++) {
        if (aml[i] != '_' || aml[i + 1] != 'S' ||
            aml[i + 2] != '5' || aml[i + 3] != '_') {
            continue;
        }

        /*
         * The four characters are not enough on their own. "_S5_" can occur in
         * any string the firmware happens to carry, and acting on a match inside
         * one would send an arbitrary value to the power management register. A
         * real definition is introduced by NameOp, either directly or with a
         * root prefix in front of it.
         */
        int named = 0;
        if (i >= 1 && aml[i - 1] == 0x08) named = 1;
        if (i >= 2 && aml[i - 2] == 0x08 && aml[i - 1] == '\\') named = 1;
        if (!named) continue;

        const uint8_t *p   = aml + i + 4;
        const uint8_t *end = aml + len;

        if (p >= end || *p != 0x12) continue;   /* PackageOp, or not this one */
        p++;

        /*
         * PkgLength's top two bits say how many more bytes of length follow it.
         * Stepping by the wrong amount here lands in the middle of the package
         * and reads its bytes as values, which is the difference between turning
         * the machine off and writing something arbitrary to a hardware
         * register.
         */
        if (p >= end) return E_NOENT;
        p += ((*p & 0xC0) >> 6) + 1;

        if (p >= end) return E_NOENT;
        p++;                                     /* NumElements */

        if (p >= end) return E_NOENT;
        if (*p == 0x0A) p++;                     /* BytePrefix before a value */
        if (p >= end) return E_NOENT;

        /* SLP_TYP is three bits wide in PM1_CNT; anything above that belongs to
         * a neighbouring field and is not ours to write. */
        *slp_typa = (uint16_t)((*p & 0x07u) << ACPI_SLP_TYP_SHIFT);
        p++;

        if (p >= end) return E_NOENT;
        if (*p == 0x0A) p++;
        if (p >= end) return E_NOENT;

        *slp_typb = (uint16_t)((*p & 0x07u) << ACPI_SLP_TYP_SHIFT);
        return E_OK;
    }

    return E_NOENT;
}

/**
 * @brief Maps one table and returns a pointer to its first byte.
 *
 * Two mappings per table: the header first, because its length is the only thing
 * that says how much there is to map, and then the whole of it. vmm_map_device()
 * is a bump allocator that never unmaps, so this spends a page or two per table
 * and never gets them back - which is affordable exactly once, at boot, over a
 * list this kernel bounds at ACPI_MAX_TABLES.
 */
static const uint8_t *map_table(uint32_t phys, uint32_t *len_out) {
    const acpi_sdt_header_t *head;
    const uint8_t *full;
    uint32_t len;

    if (phys == 0) return 0;

    head = (const acpi_sdt_header_t *)vmm_map_device(phys, sizeof(acpi_sdt_header_t));
    if (head == 0) return 0;

    len = head->length;

    /*
     * The firmware's number, bounded before it is trusted. A length below the
     * header is not a short table - it is a table whose body would start before
     * its own beginning - and one above the device window is a mapping that will
     * fail anyway, more usefully refused here with a name attached.
     */
    if (len < sizeof(acpi_sdt_header_t) || len > (1u << 20)) return 0;

    full = (const uint8_t *)vmm_map_device(phys, len);
    if (full == 0) return 0;

    if (!acpi_checksum_ok(full, len)) return 0;

    if (len_out) *len_out = len;
    return full;
}

/**
 * @brief Looks for the RSDP where a BIOS machine leaves it.
 *
 * Two places, both below a megabyte: the two kilobytes the EBDA pointer at
 * 0x40E points at, and the block from 0xE0000 to the end of the first megabyte.
 * The pointer is sixteen-byte aligned in both.
 *
 * This is the path every target that boots with QEMU's -kernel takes, because
 * there is no bootloader there to hand anything over. It is also the path that
 * cannot work on a UEFI machine, which is the whole reason Multiboot 2 is in
 * this release.
 */
static const void *scan_legacy_rsdp(void) {
    const uint8_t *area;
    uint32_t ebda;

    area = (const uint8_t *)vmm_map_device(0x400, 0x100);
    if (area != 0) {
        ebda = (uint32_t)((area[0x0E] | ((uint16_t)area[0x0F] << 8))) << 4;

        if (ebda >= 0x400 && ebda < 0xA0000) {
            const uint8_t *e = (const uint8_t *)vmm_map_device(ebda, 1024);

            if (e != 0) {
                for (uint32_t off = 0; off + 20 <= 1024; off += 16) {
                    if (acpi_rsdp_check(e + off) >= 0) return e + off;
                }
            }
        }
    }

    area = (const uint8_t *)vmm_map_device(0xE0000, 0x20000);
    if (area != 0) {
        for (uint32_t off = 0; off + 20 <= 0x20000; off += 16) {
            if (acpi_rsdp_check(area + off) >= 0) return area + off;
        }
    }

    return 0;
}

/**
 * @brief Reads the six fields the two exits need out of a mapped FADT.
 */
static int take_fadt(const uint8_t *fadt, uint32_t len) {
    uint32_t dsdt_phys;
    const uint8_t *dsdt;
    uint32_t dsdt_len = 0;
    uint8_t revision;

    if (len < FADT_OFF_PM1_CNT_LEN + 1) return E_INVAL;

    /*
     * The table's own revision, which is what the specification makes the reset
     * register and the 64-bit addresses conditional on. The length is checked as
     * well rather than instead: a table claiming a revision and stopping short
     * of the fields that revision implies is one this kernel would read past the
     * end of.
     */
    revision = ((const acpi_sdt_header_t *)fadt)->revision;

    fadt_pm1a_cnt = rd32(fadt, FADT_OFF_PM1A_CNT_BLK);
    fadt_pm1b_cnt = rd32(fadt, FADT_OFF_PM1B_CNT_BLK);
    fadt_smi_cmd  = rd32(fadt, FADT_OFF_SMI_CMD);
    fadt_acpi_enable = rd8(fadt, FADT_OFF_ACPI_ENABLE);

    if (fadt_pm1a_cnt == 0) {
        klog(LOG_LEVEL_ERROR, "ACPI", "The FADT names no PM1a control register.");
        return E_INVAL;
    }

    /*
     * The reset register, where the table is long enough to have one and its
     * flags say it means anything. Only the I/O space form is used: a
     * memory-space reset would need a mapping made at the moment the machine is
     * being torn down, which is the worst time to ask for one.
     */
    if (revision >= FADT_REVISION_WITH_RESET && len >= FADT_OFF_RESET_VALUE + 1) {
        uint32_t flags = rd32(fadt, FADT_OFF_FLAGS);

        if (flags & FADT_FLAG_RESET_REG_SUP) {
            reset_space = rd8(fadt, FADT_OFF_RESET_REG + 0);
            reset_port  = rd32(fadt, FADT_OFF_RESET_REG + 4);
            reset_value = rd8(fadt, FADT_OFF_RESET_VALUE);
            reset_supported = (reset_space == ACPI_ADDRESS_SPACE_IO && reset_port != 0);
        }
    }

    /*
     * And the DSDT, for the one AML question this kernel asks. X_DSDT is the
     * 64-bit field later revisions added; its low half is used where it is
     * present and non-zero, and an address above 4 GB is refused rather than
     * truncated - this kernel has no PAE and a truncated address is a different
     * table. The same decision the xHCI driver makes about a BAR.
     */
    dsdt_phys = rd32(fadt, FADT_OFF_DSDT);

    if (revision >= FADT_REVISION_WITH_RESET && len >= FADT_OFF_X_DSDT + 8) {
        uint32_t x_low  = rd32(fadt, FADT_OFF_X_DSDT + 0);
        uint32_t x_high = rd32(fadt, FADT_OFF_X_DSDT + 4);

        if (x_high != 0) {
            klog(LOG_LEVEL_WARN, "ACPI",
                 "The DSDT is above 4 GB; this kernel cannot reach it.");
        } else if (x_low != 0) {
            dsdt_phys = x_low;
        }
    }

    dsdt = map_table(dsdt_phys, &dsdt_len);

    if (dsdt == 0) {
        klog(LOG_LEVEL_ERROR, "ACPI", "The DSDT could not be read; no power off.");
        return E_INVAL;
    }

    if (acpi_parse_s5(dsdt + sizeof(acpi_sdt_header_t),
                      dsdt_len - sizeof(acpi_sdt_header_t),
                      &fadt_slp_typa, &fadt_slp_typb) != E_OK) {
        /*
         * Said out loud. A firmware that describes _S5_ in a form this search
         * does not follow is one this kernel cannot power off, and the symptom
         * would otherwise be `halt` quietly stopping the processor with the fans
         * still running - which is what it did before this release and looks
         * exactly the same.
         */
        klog(LOG_LEVEL_ERROR, "ACPI",
             "No _S5_ this kernel can read; halt will stop the CPU rather than cut power.");
        return E_NOENT;
    }

    return E_OK;
}

int acpi_init(const void *bootloader_rsdp) {
    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)bootloader_rsdp;
    const uint8_t *root;
    uint32_t root_len = 0;
    uint32_t entry_width;
    uint32_t count;
    int revision;

    acpi_ready = 0;

    if (rsdp != 0 && acpi_rsdp_check(rsdp) < 0) {
        klog(LOG_LEVEL_WARN, "ACPI", "The bootloader's RSDP did not check out; scanning.");
        rsdp = 0;
    }

    if (rsdp == 0) {
        rsdp = (const acpi_rsdp_t *)scan_legacy_rsdp();
        if (rsdp != 0) {
            klog(LOG_LEVEL_INFO, "ACPI", "RSDP found by scanning the legacy areas.");
        }
    } else {
        klog(LOG_LEVEL_INFO, "ACPI", "RSDP taken from the bootloader.");
    }

    if (rsdp == 0) {
        klog(LOG_LEVEL_WARN, "ACPI",
             "No RSDP anywhere; this machine cannot be powered off.");
        return E_NODEV;
    }

    revision = acpi_rsdp_check(rsdp);

    /*
     * XSDT where there is one, and the specification asks for that even on a
     * kernel this size. Its entries are 64-bit, which is the interesting part
     * here: an entry above 4 GB is unreachable without PAE and is refused with a
     * line rather than truncated into a different table.
     */
    if (revision >= 2 && rsdp->xsdt_address_low != 0 && rsdp->xsdt_address_high == 0) {
        root = map_table(rsdp->xsdt_address_low, &root_len);
        entry_width = 8;
    } else {
        root = map_table(rsdp->rsdt_address, &root_len);
        entry_width = 4;
    }

    if (root == 0) {
        klog(LOG_LEVEL_ERROR, "ACPI", "The root table could not be read.");
        return E_INVAL;
    }

    count = (root_len - sizeof(acpi_sdt_header_t)) / entry_width;

    if (count > ACPI_MAX_TABLES) {
        klog_int(LOG_LEVEL_WARN, "ACPI",
                 "More tables than this kernel walks; the list stops at", ACPI_MAX_TABLES);
        count = ACPI_MAX_TABLES;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = sizeof(acpi_sdt_header_t) + i * entry_width;
        uint32_t phys = rd32(root, off);
        const uint8_t *table;
        uint32_t table_len = 0;

        if (entry_width == 8 && rd32(root, off + 4) != 0) {
            klog(LOG_LEVEL_WARN, "ACPI", "A table above 4 GB; skipped, not truncated.");
            continue;
        }

        table = map_table(phys, &table_len);
        if (table == 0) continue;

        if (ft_memcmp(table, "FACP", 4) != 0) continue;

        if (take_fadt(table, table_len) != E_OK) return E_INVAL;

        acpi_ready = 1;
        klog_hex(LOG_LEVEL_INFO, "ACPI", "Power management is at PM1a control port",
                 fadt_pm1a_cnt);
        return E_OK;
    }

    klog(LOG_LEVEL_ERROR, "ACPI", "No FADT among the tables; this machine cannot be powered off.");
    return E_NODEV;
}

int acpi_present(void) {
    return acpi_ready;
}

uint32_t acpi_pm1a_cnt(void) {
    return acpi_ready ? fadt_pm1a_cnt : 0;
}

int acpi_poweroff(void) {
    if (!acpi_ready) return E_NODEV;

    /*
     * Some firmware boots with power management still its own and hands it over
     * only when asked. Where SMI_CMD names a port and SCI_EN is clear, the
     * handover is requested and waited for - bounded, like every wait in the USB
     * driver, because a firmware that never answers must not hold the shutdown
     * open for ever.
     */
    if (fadt_smi_cmd != 0 && fadt_acpi_enable != 0 &&
        (inw((uint16_t)fadt_pm1a_cnt) & ACPI_SCI_EN) == 0) {
        outb((uint16_t)fadt_smi_cmd, fadt_acpi_enable);

        for (int i = 0; i < 300; i++) {
            if (inw((uint16_t)fadt_pm1a_cnt) & ACPI_SCI_EN) break;
            for (volatile int d = 0; d < 10000; d++) { }
        }
    }

    outw((uint16_t)fadt_pm1a_cnt, (uint16_t)(fadt_slp_typa | ACPI_SLP_EN));

    if (fadt_pm1b_cnt != 0) {
        outw((uint16_t)fadt_pm1b_cnt, (uint16_t)(fadt_slp_typb | ACPI_SLP_EN));
    }

    /* Reaching this line is the failure. The machine was supposed to stop
     * between the write above and here. */
    return E_IO;
}

int acpi_reset(void) {
    if (!reset_supported) return E_NODEV;

    outb((uint16_t)reset_port, reset_value);
    return E_IO;
}
