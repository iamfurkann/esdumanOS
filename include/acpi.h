#ifndef ACPI_H
#define ACPI_H

#include "types.h"

/**
 * @file acpi.h
 * @brief Enough ACPI to turn the machine off, and deliberately no more.
 *
 * Until v1.12.0 this kernel could not power a machine down. `halt` ran
 * `cli; hlt`, which stops the processor and leaves the fans running, and
 * `reboot` wrote 0xFE to the i8042 - a keyboard controller that a modern UEFI
 * laptop need not have at all. On the hardware this project is aimed at, the
 * only way out was holding the power button.
 *
 * ACPI is the answer and it needs one thing to start: the Root System
 * Description Pointer. On a BIOS machine that can be found by scanning the two
 * legacy areas below 1 MB. On a UEFI machine it cannot - those areas need not
 * exist, and the firmware publishes the pointer in the EFI configuration table,
 * which only the bootloader ever sees. That is the whole reason boot.asm grew a
 * second multiboot header this release; Multiboot 2 carries the pointer and
 * Multiboot 1 has no field for it.
 *
 * What this is not: an ACPI implementation. There is no AML interpreter, no
 * namespace, no device enumeration, no power management beyond the one
 * transition that ends the session. MADT, HPET and PCI routing are not read;
 * AHCI and xHCI go on polling exactly as they did. The one place AML is touched
 * is the search for \\_S5_, and that is a byte search with its limits written
 * down rather than a parser pretending otherwise.
 */

/** @brief Sleep state 5 - "soft off". The only transition this kernel makes. */
#define ACPI_SLP_TYP_SHIFT 10
#define ACPI_SLP_EN        (1u << 13)

/**
 * @brief Tables walked before the search gives up.
 *
 * A bounded table, like every other one here: the limit is a documented number
 * rather than a condition discovered at boot. A machine publishes on the order
 * of ten to twenty; sixty-four is above anything either QEMU or a laptop has
 * produced, and a longer list is truncated with a line rather than silently.
 */
#define ACPI_MAX_TABLES 64

/**
 * @brief The header every ACPI table starts with, including the FADT and DSDT.
 *
 * Thirty-six bytes, fixed by the specification. `length` covers the header and
 * everything after it, and it comes from the firmware - so it is a walk
 * condition rather than a fact, the same way a USB descriptor's bLength was in
 * v1.9.0 and a Multiboot 2 tag's size is one file over.
 */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

/**
 * @brief The Root System Description Pointer, in both of its lengths.
 *
 * Revision 0 stops after `rsdt_address` and is twenty bytes; revision 2 adds the
 * five fields below it and is thirty-six. The two checksums cover the two
 * lengths, which is why there are two: a revision 2 pointer has to satisfy both,
 * and a reader that checks only the first would accept a truncated one.
 */
typedef struct {
    char     signature[8];        /**< "RSD PTR ", trailing space included. */
    uint8_t  checksum;            /**< Over the first 20 bytes.            */
    char     oem_id[6];
    uint8_t  revision;            /**< 0 for ACPI 1.0, 2 for later.        */
    uint32_t rsdt_address;

    uint32_t length;              /**< Revision 2 onwards, from here down. */
    uint32_t xsdt_address_low;
    uint32_t xsdt_address_high;
    uint8_t  extended_checksum;   /**< Over `length` bytes.                */
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

/*
 * Offsets into the FADT that this kernel reads.
 *
 * Named rather than spelled as numbers at the point of use, because the FADT is
 * a long structure whose interesting fields are scattered through it and an
 * off-by-four here reads a different register than the one intended.
 *
 * Four of these were wrong when they were first written, and the sentence above
 * was already sitting over them - naming a constant does not make it right. The
 * neighbours are what they were mistaken for: FLAGS read the first word of
 * RESET_REG, RESET_REG read four bytes into itself, RESET_VALUE read the first
 * byte of X_FIRMWARE_CTRL, and X_DSDT read the head of X_PM1a_EVT_BLK. Every one
 * of them still produced a plausible-looking number, which is the whole problem
 * with an offset: there is no value it can return that looks wrong.
 *
 * What caught it was a warning added for something else. X_DSDT's high word came
 * back non-zero, the DSDT was reported as being above 4 GB on a machine whose
 * tables are nowhere near it, and the only reason the release still worked is
 * that the refusal fell back to the 32-bit DSDT field - which was one of the six
 * offsets that were right.
 *
 * The comments give each field's neighbour so that the next reader can check a
 * number against something other than itself.
 */
#define FADT_OFF_DSDT           40   /* after FIRMWARE_CTRL at 36            */
#define FADT_OFF_SMI_CMD        48   /* after SCI_INT, a halfword, at 46     */
#define FADT_OFF_ACPI_ENABLE    52   /* first of four bytes before PM1a_EVT  */
#define FADT_OFF_PM1A_CNT_BLK   64   /* third of the four PM1 block pointers */
#define FADT_OFF_PM1B_CNT_BLK   68   /* and the fourth                       */
#define FADT_OFF_PM1_CNT_LEN    89   /* second of the six length bytes at 88 */
#define FADT_OFF_FLAGS         112   /* after the reserved byte at 111       */
#define FADT_OFF_RESET_REG     116   /* a 12-byte GAS, immediately after it  */
#define FADT_OFF_RESET_VALUE   128   /* the byte the GAS above is written    */
#define FADT_OFF_X_DSDT        140   /* after X_FIRMWARE_CTRL at 132         */

/**
 * @brief The FADT revision from which the 64-bit and reset fields exist.
 *
 * A shorter table does not carry them, and the length alone is not the condition
 * the specification states - the revision is. Both are checked, because a table
 * that claims a revision and stops short of the fields it implies is a table
 * this kernel would otherwise read past the end of.
 */
#define FADT_REVISION_WITH_RESET 2

/** @brief FADT flags bit 10: the reset register below is meaningful. */
#define FADT_FLAG_RESET_REG_SUP (1u << 10)

/** @brief Generic Address Structure address space: system I/O ports. */
#define ACPI_ADDRESS_SPACE_IO   1

/**
 * @brief Brings ACPI up: finds the tables, the FADT, and the sleep values.
 *
 * Called from the boot path after paging, because the tables live wherever the
 * firmware put them and reaching them needs vmm_map_device() - on a real machine
 * they sit near the top of low memory, well above the sixteen megabytes boot.asm
 * identity maps.
 *
 * @param bootloader_rsdp The RSDP a Multiboot 2 bootloader handed over, or 0.
 *                        When 0 the legacy areas are scanned, which is right on
 *                        a BIOS machine and hopeless on a UEFI one.
 * @return E_OK when a FADT was found and the machine can be powered off,
 *         E_NODEV when no usable RSDP was found, or E_INVAL when the tables were
 *         found and could not be believed.
 */
int acpi_init(const void *bootloader_rsdp);

/** @brief Non-zero once a FADT has been found and its sleep values read. */
int acpi_present(void);

/** @brief The PM1a control port, or 0 when ACPI is not up. For the tests. */
uint32_t acpi_pm1a_cnt(void);

/**
 * @brief Cuts the power. Does not return when it works.
 *
 * @return E_NODEV when ACPI is not available, or E_IO when the write was made
 *         and the machine was still running afterwards - which is a real
 *         outcome, not a formality: firmware that wants ACPI mode entered first
 *         will ignore a sleep request until it is.
 */
int acpi_poweroff(void);

/**
 * @brief Resets the machine through the FADT's reset register, if it has one.
 *
 * The first rung of the ladder in sys_reboot(); the ones below it need no ACPI.
 *
 * @return E_NODEV when there is no usable reset register, or E_IO when the write
 *         returned and the machine did not restart.
 */
int acpi_reset(void);

/* ── The parts that can be asserted without a machine ────────────────── */

/**
 * @brief Whether a table's bytes sum to zero in the low byte.
 *
 * Split out and exported for the reason the xHCI packet-size decision was in
 * v1.11.0: the interesting cases are the ones no machine this project runs on
 * produces. A table with a broken checksum is one of them, and a test can build
 * one in four lines.
 *
 * @param table Start of the table.
 * @param len Bytes to sum; a length of zero is not a valid table and answers 0.
 * @return 1 when the sum's low byte is zero.
 */
int acpi_checksum_ok(const void *table, uint32_t len);

/**
 * @brief Validates an RSDP image and reports which revision it is.
 *
 * @param rsdp At least twenty bytes.
 * @return 0 for a valid ACPI 1.0 pointer, 2 for a valid later one, or -1 when
 *         the signature or either checksum does not hold.
 */
int acpi_rsdp_check(const void *rsdp);

/**
 * @brief Finds \\_S5_ in a block of AML and reads the two sleep type values.
 *
 * A byte search, and the header says so rather than letting the name imply an
 * interpreter. It looks for the four characters preceded by a NameOp, steps over
 * the package header that follows, and reads the first two elements - which is
 * what the shutdown path needs and the whole of what it understands. A firmware
 * that describes _S5_ any other way is one this kernel cannot power off, and
 * acpi_init() says so in the log rather than failing silently later.
 *
 * @param aml The DSDT's body.
 * @param len Its length; a search that would step past this stops.
 * @param slp_typa Receives the first value, already shifted into place.
 * @param slp_typb Receives the second.
 * @return E_OK when both were read, or E_NOENT when _S5_ was not found.
 */
int acpi_parse_s5(const uint8_t *aml, uint32_t len,
                  uint16_t *slp_typa, uint16_t *slp_typb);

#endif // ACPI_H
