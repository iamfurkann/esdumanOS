/*
 * File: test_acpi.c
 * Purpose: The tables, and the four offsets that were wrong.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Almost all of this runs against tables built here rather than against the
 * machine, and that is the point rather than a compromise. The interesting cases
 * are the ones no machine this project can reach produces: a checksum that does
 * not hold, a table claiming to live above 4 GB, a firmware that describes _S5_
 * in a form this kernel cannot follow. QEMU produces none of them and the laptop
 * this is aimed at will produce them once, in a hotel room, with no debugger.
 *
 * The same argument the xHCI context stride made in v1.9.0 and the endpoint zero
 * packet size made in v1.11.0. It has now paid three times.
 *
 * The layout assertions at the end exist because four of the FADT offsets in
 * acpi.h were wrong on the first attempt - reading a neighbouring field, always
 * returning a plausible number, and caught only because a warning added for an
 * unrelated reason fired on a machine where it could not be true. There is no
 * value a wrong offset can return that looks wrong, so the offsets are checked
 * against the specification's own arithmetic rather than against a result.
 */
#include "ktest.h"
#include "acpi.h"
#include "types.h"
#include "errno.h"
#include "libft.h"

/**
 * @brief Fills in the checksum byte so that the table sums to zero.
 */
static void fix_checksum(uint8_t *table, uint32_t len, uint32_t sum_off) {
    uint8_t sum = 0;

    table[sum_off] = 0;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + table[i]);
    table[sum_off] = (uint8_t)(0u - sum);
}

/**
 * @brief The sum, which is the cheapest thing a firmware can get wrong.
 */
static void run_checksum_assertions(void) {
    static uint8_t table[32];

    ft_memset(table, 0, sizeof(table));
    for (int i = 0; i < 31; i++) table[i] = (uint8_t)(i * 7);
    fix_checksum(table, sizeof(table), 31);

    KTEST_ASSERT(acpi_checksum_ok(table, sizeof(table)) &&
                 (table[0]++, !acpi_checksum_ok(table, sizeof(table))),
                 "[STRICT] [ACPI] a table whose bytes sum to zero passes, and one byte off does not");

    KTEST_ASSERT(!acpi_checksum_ok(table, 0) && !acpi_checksum_ok(0, 32),
                 "[STRICT] [ACPI] a length of zero is not a short table, and neither is no table");
}

/**
 * @brief Builds an RSDP of either revision, correct in every field.
 */
static void build_rsdp(uint8_t *buf, uint8_t revision) {
    acpi_rsdp_t *r = (acpi_rsdp_t *)buf;

    ft_memset(buf, 0, sizeof(acpi_rsdp_t));
    ft_memcpy(r->signature, "RSD PTR ", 8);
    r->revision     = revision;
    r->rsdt_address = 0x000E1000;

    if (revision >= 2) {
        r->length            = sizeof(acpi_rsdp_t);
        r->xsdt_address_low  = 0x000E2000;
        r->xsdt_address_high = 0;
    }

    /* The 1.0 sum covers the first twenty bytes; the 2.0 sum covers the whole
     * of it, and a 2.0 pointer has to satisfy both. Order matters: fixing the
     * short one changes bytes the long one covers. */
    fix_checksum(buf, 20, 9);
    if (revision >= 2) fix_checksum(buf, sizeof(acpi_rsdp_t), 32);
}

static void run_rsdp_assertions(void) {
    static uint8_t rsdp[sizeof(acpi_rsdp_t)];

    build_rsdp(rsdp, 0);
    KTEST_ASSERT(acpi_rsdp_check(rsdp) == 0,
                 "[STRICT] [ACPI] a well-formed ACPI 1.0 pointer answers with its revision");

    build_rsdp(rsdp, 2);
    KTEST_ASSERT(acpi_rsdp_check(rsdp) == 2,
                 "[STRICT] [ACPI] and a well-formed 2.0 one answers with its own");

    build_rsdp(rsdp, 2);
    rsdp[3] = 'X';
    KTEST_ASSERT(acpi_rsdp_check(rsdp) < 0,
                 "[STRICT] [ACPI] a signature that is not RSD PTR is refused before anything is read");

    build_rsdp(rsdp, 2);
    rsdp[9]++;
    KTEST_ASSERT(acpi_rsdp_check(rsdp) < 0,
                 "[STRICT] [ACPI] a first checksum that does not hold is refused");

    /*
     * The second sum is the one a reader is most likely to skip, and skipping it
     * accepts a pointer whose extended half is damaged - which is the half
     * carrying the XSDT address.
     */
    build_rsdp(rsdp, 2);
    rsdp[32]++;
    KTEST_ASSERT(acpi_rsdp_check(rsdp) < 0,
                 "[STRICT] [ACPI] and so is a second one, which only a 2.0 pointer has");

    build_rsdp(rsdp, 2);
    ((acpi_rsdp_t *)rsdp)->length = 64;
    fix_checksum(rsdp, sizeof(acpi_rsdp_t), 32);
    KTEST_ASSERT(acpi_rsdp_check(rsdp) < 0,
                 "[STRICT] [ACPI] a length past the copy this kernel keeps is refused, not summed");
}

/**
 * @brief A minimal AML block with a real \\_S5_ definition in it.
 *
 * NameOp, the four characters, PackageOp, a one-byte package length, the element
 * count, and two values behind BytePrefix. That is the shape every firmware this
 * search is meant to follow produces, and the shortest thing that is one.
 */
static const uint8_t s5_present[] = {
    0x00, 0x00,
    0x08, '_', 'S', '5', '_',
    0x12, 0x06, 0x02,
    0x0A, 0x05,
    0x0A, 0x03,
    0x00
};

/* The same four characters with no NameOp in front: a string, not a definition. */
static const uint8_t s5_in_a_string[] = {
    0x0D, '_', 'S', '5', '_', 0x00,
    0x12, 0x06, 0x02, 0x0A, 0x05, 0x0A, 0x03
};

static const uint8_t s5_absent[] = {
    0x08, '_', 'S', '4', '_', 0x12, 0x06, 0x02, 0x0A, 0x05, 0x0A, 0x03
};

static void run_s5_assertions(void) {
    uint16_t a = 0xFFFF;
    uint16_t b = 0xFFFF;

    KTEST_ASSERT(acpi_parse_s5(s5_present, sizeof(s5_present), &a, &b) == E_OK &&
                 a == (5u << ACPI_SLP_TYP_SHIFT) &&
                 b == (3u << ACPI_SLP_TYP_SHIFT),
                 "[STRICT] [ACPI] _S5_ introduced by NameOp yields both sleep types, shifted into place");

    /*
     * The check that keeps this a search for a definition rather than for four
     * bytes. Acting on a match inside a string would send whatever followed it
     * to a hardware register.
     */
    a = 0xFFFF; b = 0xFFFF;
    KTEST_ASSERT(acpi_parse_s5(s5_in_a_string, sizeof(s5_in_a_string), &a, &b) == E_NOENT &&
                 a == 0xFFFF && b == 0xFFFF,
                 "[STRICT] [ACPI] the same four characters inside a string are not a definition");

    a = 0xFFFF; b = 0xFFFF;
    KTEST_ASSERT(acpi_parse_s5(s5_absent, sizeof(s5_absent), &a, &b) == E_NOENT,
                 "[STRICT] [ACPI] and a table without one is reported as not having one");

    /*
     * Every step in that walk is taken over bytes the firmware chose. A block
     * that ends in the middle of the package must stop rather than read on, and
     * this asserts it at every truncation rather than at one.
     */
    {
        int walked_off_the_end = 0;

        for (uint32_t cut = 1; cut < sizeof(s5_present); cut++) {
            uint16_t x = 0, y = 0;

            if (acpi_parse_s5(s5_present, cut, &x, &y) == E_OK &&
                cut < sizeof(s5_present) - 1) {
                walked_off_the_end = 1;
            }
        }

        KTEST_ASSERT(!walked_off_the_end,
                     "[STRICT] [ACPI] a walk cut short at any point stops rather than reading past its block");
    }
}

/**
 * @brief Where the specification puts the fields, checked as arithmetic.
 *
 * Four of these were wrong and every one of them returned a number that looked
 * like an answer. An offset has no failure mode that shows: it reads the
 * neighbouring field and hands back whatever is there. So this checks them
 * against the layout that fixes them rather than against a result they produce.
 */
static void run_fadt_layout_assertions(void) {
    KTEST_ASSERT(FADT_OFF_FLAGS == 112 && FADT_OFF_RESET_REG == 116 &&
                 FADT_OFF_RESET_VALUE == 128 && FADT_OFF_X_DSDT == 140,
                 "[STRICT] [ACPI] the four offsets that were wrong are where the specification puts them");

    /*
     * And the same four as a sequence, which is the form that would have caught
     * them: Flags is a doubleword, the reset register is a twelve-byte generic
     * address, the value written to it is one byte, ARM_BOOT_ARCH is two and the
     * minor version one, and X_FIRMWARE_CTRL is eight. There is no padding
     * between any of them.
     */
    KTEST_ASSERT(FADT_OFF_FLAGS + 4 == FADT_OFF_RESET_REG &&
                 FADT_OFF_RESET_REG + 12 == FADT_OFF_RESET_VALUE &&
                 FADT_OFF_RESET_VALUE + 1 + 2 + 1 + 8 == FADT_OFF_X_DSDT,
                 "[STRICT] [ACPI] and the fields from Flags to X_DSDT tile without a gap between them");

    /* The six that were right, held in place by the same kind of arithmetic. */
    KTEST_ASSERT(FADT_OFF_DSDT + 4 + 2 + 2 == FADT_OFF_SMI_CMD &&
                 FADT_OFF_SMI_CMD + 4 == FADT_OFF_ACPI_ENABLE &&
                 FADT_OFF_PM1A_CNT_BLK + 4 == FADT_OFF_PM1B_CNT_BLK,
                 "[STRICT] [ACPI] and the pointers this kernel writes to sit where they were found");
}

/**
 * @brief The one thing worth asking the machine, phrased so it holds on all of them.
 *
 * Every kernel test target reaches ACPI - the three that boot with -kernel find
 * the pointer by scanning, and the UEFI one is handed it by the bootloader - so
 * this could assert that it came up. It does not, because a target that ever
 * stops finding it should fail on the thing that broke rather than on a count,
 * and because an assertion with two branches is an assertion that reports a
 * different number on different machines. This one has no branch.
 */
static void run_machine_assertions(void) {
    KTEST_ASSERT(!acpi_present() || acpi_pm1a_cnt() != 0,
                 "[STRICT] [ACPI] ACPI is either absent or has named the port it will be switched off through");
}

void run_acpi_tests(void) {
    printk("\n--- ACPI Tests ---\n");

    run_checksum_assertions();
    run_rsdp_assertions();
    run_s5_assertions();
    run_fadt_layout_assertions();
    run_machine_assertions();
}
