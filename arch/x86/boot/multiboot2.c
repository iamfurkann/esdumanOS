/*
 * File: multiboot2.c
 * Purpose: The second way in, translated into the shape the first one left.
 *
 * This file is part of the esdumanOS test suite.
 *
 * esdumanOS has booted under Multiboot 1 since the beginning and still does:
 * three of the four kernel test targets start with QEMU's -kernel, which reads
 * that header and no other. What Multiboot 2 adds is one thing this kernel
 * cannot get any other way - the ACPI RSDP. On a BIOS machine the RSDP can be
 * found by scanning the legacy areas below 1 MB. On a UEFI machine it cannot:
 * those areas need not exist, and the firmware hands the pointer over in the EFI
 * configuration table, which only the bootloader ever sees. Without it there is
 * no FADT, without the FADT there is no PM1a control register, and without that
 * the machine cannot be powered off.
 *
 * So this translates rather than replacing. init_pmm() walks a Multiboot 1
 * memory map, install_framebuffer_console() reads Multiboot 1 framebuffer
 * fields, and selftest.c reads a Multiboot 1 command line; all three are left
 * alone and handed the structure they already know. The alternative - teaching
 * each of them a second shape - is three places to get right instead of one, and
 * two of them are on the path that decides how much memory exists.
 *
 * Nothing here may log. It runs before init_serial() and before the console
 * exists, because init_pmm() needs the memory map and everything else needs
 * memory. What it saw is recorded and reported by mb2_report() once there is
 * somewhere for a line to go - which matters more than it sounds, because on a
 * real machine "was the ACPI tag passed" is the entire diagnosis for a system
 * that will not shut down.
 */
#include "multiboot.h"
#include "types.h"
#include "errno.h"
#include "klog.h"

/* Tag types this kernel reads. The specification defines more; a tag that is not
 * one of these is stepped over rather than refused, because a bootloader is
 * entitled to describe things nothing here has asked about. */
#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_BASIC_MEM    4
#define MB2_TAG_MMAP         6
#define MB2_TAG_FRAMEBUFFER  8
#define MB2_TAG_ACPI_OLD    14
#define MB2_TAG_ACPI_NEW    15

/**
 * @brief The highest physical address this translation can reach.
 *
 * boot.asm identity maps the first four page tables - sixteen megabytes - before
 * jumping to the higher half, and this runs before anything extends that. A
 * bootloader is free to place the information structure anywhere below 4 GB, so
 * the reach is checked rather than assumed: a structure above the line would be
 * read as whatever happens to be mapped there, which is worse than refusing it.
 */
#define MB2_REACHABLE_LIMIT 0x01000000u

/** @brief One tag's header. Every tag begins with these two words. */
typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

/**
 * @brief One memory map entry, as Multiboot 2 lays it out.
 *
 * Twenty-four bytes, and the same twenty-four Multiboot 1 uses - but arranged
 * differently, and that is the point of the copy below. Multiboot 1 puts a
 * length in every entry; Multiboot 2 puts it once in the tag.
 */
typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

/* The translated memory map. In this kernel's own BSS rather than left in the
 * bootloader's structure, because the layouts differ and init_pmm() reads the
 * other one. BSS is cleared by boot.asm before kernel_main is entered. */
static multiboot_memory_map_t mb2_mmap[MB2_MMAP_MAX];
static uint32_t mb2_mmap_count = 0;
static int mb2_mmap_truncated = 0;

/*
 * The RSDP, taken out of the bootloader's structure and kept here.
 *
 * Thirty-six bytes is the whole of an ACPI 2.0 pointer and twenty is an ACPI 1.0
 * one; a tag longer than that is carrying something this kernel does not read,
 * so the copy is bounded rather than sized from the tag. In this kernel's own
 * BSS, which outlives the mapping the original was read through.
 */
static uint8_t mb2_rsdp_copy[36];
static const void *mb2_rsdp = 0;
static uint32_t mb2_rsdp_tag = 0;

/*
 * What was seen, for the log that cannot happen yet - as a bitmask of tag types
 * rather than a list.
 *
 * A list was the first shape this took, logged one line per tag at DEBUG. That
 * would have produced nothing at all: klog's default level is INFO and anything
 * below it is dropped before it reaches the ring, so the tag dump this file
 * exists to provide would have been silent on every machine. One line at INFO
 * carrying every answer is both louder and quieter than sixteen.
 *
 * The specification defines tag types up to 21, so a bit per type fits a word
 * with room left. A type outside that range is counted rather than shifted by,
 * because a shift of 32 or more is undefined and this number comes from the
 * bootloader.
 */
static uint32_t mb2_tag_mask = 0;
static int mb2_tag_count = 0;
static int mb2_tag_unknown = 0;

/** @brief Rounds a tag offset up to the eight-byte boundary the next one is on. */
static uint32_t mb2_align8(uint32_t v) {
    return (v + 7u) & ~7u;
}

const void *mb2_acpi_rsdp(void) {
    return mb2_rsdp;
}

/**
 * @brief Copies one Multiboot 2 memory map into Multiboot 1's arrangement.
 *
 * The entry stride comes out of the tag rather than out of sizeof(). That reads
 * like caution and is not: the specification says a future version may make the
 * entries longer, and a reader that indexes by the size of its own struct would
 * then read every entry after the first from the wrong offset - on a machine
 * where nothing else would say so. This kernel has already been bitten by
 * exactly that shape once, in the xHCI context stride, and the fix there was the
 * same: read the number the hardware published.
 */
static void mb2_take_mmap(const uint8_t *payload, uint32_t payload_len) {
    uint32_t entry_size = *(const uint32_t *)(payload + 0);
    uint32_t off = 8;   /* past entry_size and entry_version */

    if (entry_size < sizeof(mb2_mmap_entry_t)) return;   /* not a map we can read */

    mb2_mmap_count = 0;

    while (off + entry_size <= payload_len) {
        const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)(payload + off);

        if (mb2_mmap_count >= MB2_MMAP_MAX) {
            mb2_mmap_truncated = 1;
            break;
        }

        /*
         * size is what Multiboot 1 puts in front of each entry: the bytes that
         * follow it. init_pmm() steps by size + 4, so this has to be the size of
         * the five words after it and not of the whole struct.
         */
        mb2_mmap[mb2_mmap_count].size      = sizeof(multiboot_memory_map_t) - 4;
        mb2_mmap[mb2_mmap_count].addr_low  = e->base_low;
        mb2_mmap[mb2_mmap_count].addr_high = e->base_high;
        mb2_mmap[mb2_mmap_count].len_low   = e->len_low;
        mb2_mmap[mb2_mmap_count].len_high  = e->len_high;
        mb2_mmap[mb2_mmap_count].type      = e->type;
        mb2_mmap_count++;

        off += entry_size;
    }
}

int mb2_translate(uint32_t info_phys, multiboot_info_t *out) {
    if (out == 0) return E_INVAL;
    if (info_phys == 0 || (info_phys & 7u) != 0) return E_INVAL;
    if (info_phys >= MB2_REACHABLE_LIMIT) return E_FAULT;

    const uint8_t *base = (const uint8_t *)info_phys;
    uint32_t total = *(const uint32_t *)(base + 0);

    /* Eight bytes of header and at least an end tag. A total_size smaller than
     * that is not a short structure, it is not this structure. */
    if (total < 16 || info_phys + total > MB2_REACHABLE_LIMIT) return E_INVAL;

    for (uint32_t i = 0; i < sizeof(multiboot_info_t); i++) {
        ((uint8_t *)out)[i] = 0;
    }

    uint32_t off = 8;

    while (off + sizeof(mb2_tag_t) <= total) {
        const mb2_tag_t *tag = (const mb2_tag_t *)(base + off);
        const uint8_t *payload = (const uint8_t *)tag + sizeof(mb2_tag_t);
        uint32_t payload_len;

        if (tag->type == MB2_TAG_END) break;

        /*
         * A tag shorter than its own header is not a malformed tag to step over.
         * It is a walk that never ends, and every byte being stepped by came from
         * the bootloader - the same bound the USB descriptor walk got in v1.9.0
         * and the extended capability chain in v1.8.0, arrived at from a third
         * direction. Refused rather than skipped, because a structure containing
         * one is not describing anything.
         */
        if (tag->size < sizeof(mb2_tag_t)) return E_INVAL;
        if (off + tag->size > total) break;   /* cut short by the structure's own length */

        payload_len = tag->size - sizeof(mb2_tag_t);

        mb2_tag_count++;
        if (tag->type < 32) {
            mb2_tag_mask |= (uint32_t)1 << tag->type;
        } else {
            mb2_tag_unknown++;
        }

        switch (tag->type) {
        case MB2_TAG_CMDLINE:
            /*
             * Pointed at rather than copied. The string lives inside the
             * bootloader's structure, which stays where it is - and kernel.c
             * copies it into global_cmdline a few lines later anyway, which is
             * the copy that actually matters.
             */
            out->cmdline = (uint32_t)payload;
            out->flags |= MULTIBOOT_FLAG_CMDLINE;
            break;

        case MB2_TAG_BASIC_MEM:
            if (payload_len >= 8) {
                out->mem_lower = *(const uint32_t *)(payload + 0);
                out->mem_upper = *(const uint32_t *)(payload + 4);
                out->flags |= MULTIBOOT_FLAG_MEMORY;
            }
            break;

        case MB2_TAG_MMAP:
            if (payload_len >= 8) {
                mb2_take_mmap(payload, payload_len);
                if (mb2_mmap_count > 0) {
                    out->mmap_addr   = (uint32_t)mb2_mmap;
                    out->mmap_length = mb2_mmap_count * sizeof(multiboot_memory_map_t);
                    out->flags |= MULTIBOOT_FLAG_MMAP;
                }
            }
            break;

        case MB2_TAG_FRAMEBUFFER:
            if (payload_len >= 22) {
                out->framebuffer_addr_low  = *(const uint32_t *)(payload + 0);
                out->framebuffer_addr_high = *(const uint32_t *)(payload + 4);
                out->framebuffer_pitch     = *(const uint32_t *)(payload + 8);
                out->framebuffer_width     = *(const uint32_t *)(payload + 12);
                out->framebuffer_height    = *(const uint32_t *)(payload + 16);
                out->framebuffer_bpp       = payload[20];
                out->framebuffer_type      = payload[21];
                out->flags |= MULTIBOOT_FLAG_FRAMEBUFFER;
            }
            break;

        case MB2_TAG_ACPI_OLD:
        case MB2_TAG_ACPI_NEW:
            /*
             * The reason this file exists, and it is taken away rather than
             * pointed at.
             *
             * The first version of this kept the address of the copy inside the
             * bootloader's structure, which is reachable here and nowhere else:
             * this runs while boot.asm's identity mapping of the low sixteen
             * megabytes is still in force, and init_paging() drops it - the
             * comment at the end of that function says so in as many words. By
             * the time acpi_init() runs there is nothing at that address at all,
             * and reading it hung the machine on the one boot path this release
             * exists to add.
             *
             * The command line has been copied out for the same reason since
             * long before this file existed - global_cmdline in kernel.c - so
             * the precedent was already here to follow. A boot-time artifact is
             * reachable at boot time, and anything wanted afterwards has to be
             * taken away while it can still be read.
             *
             * The newer tag wins where both are present: it is the one that
             * carries an XSDT, and a machine that publishes both is telling you
             * the old one is there for software that cannot read the new.
             */
            if (payload_len >= 20 &&
                (mb2_rsdp == 0 || tag->type == MB2_TAG_ACPI_NEW)) {
                uint32_t take = payload_len;

                if (take > sizeof(mb2_rsdp_copy)) take = sizeof(mb2_rsdp_copy);

                for (uint32_t b = 0; b < take; b++) mb2_rsdp_copy[b] = payload[b];

                mb2_rsdp     = mb2_rsdp_copy;
                mb2_rsdp_tag = tag->type;
            }
            break;

        default:
            break;
        }

        off = mb2_align8(off + tag->size);
    }

    return E_OK;
}

void mb2_report(void) {
    klog_int(LOG_LEVEL_INFO, "BOOT", "Multiboot 2 tags provided by the bootloader",
             mb2_tag_count);

    /* One line, every answer: bit N set means a tag of type N was present. The
     * two that decide whether this machine can be powered off are 14 and 15. */
    klog_hex(LOG_LEVEL_INFO, "BOOT", "Tag types present, as a bitmask", mb2_tag_mask);

    if (mb2_tag_unknown > 0) {
        klog_int(LOG_LEVEL_INFO, "BOOT",
                 "Tags with a type this kernel has no bit for", mb2_tag_unknown);
    }

    klog_int(LOG_LEVEL_INFO, "BOOT", "Memory map entries carried over",
             (int)mb2_mmap_count);

    if (mb2_mmap_truncated) {
        klog_int(LOG_LEVEL_WARN, "BOOT",
                 "Memory map longer than this kernel carries; entries kept", MB2_MMAP_MAX);
    }

    if (mb2_rsdp != 0) {
        klog_int(LOG_LEVEL_INFO, "BOOT", "ACPI RSDP handed over in tag", (int)mb2_rsdp_tag);
    } else {
        /*
         * Worth a line rather than a silence. On a BIOS machine this is ordinary
         * and acpi_init() will find the RSDP by scanning; on a UEFI machine it is
         * the whole diagnosis for a system that cannot be powered off, and there
         * is nowhere else it would show up.
         */
        klog(LOG_LEVEL_WARN, "BOOT",
             "No ACPI RSDP tag; the legacy scan is the only remaining source.");
    }
}
