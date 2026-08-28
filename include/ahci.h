#ifndef AHCI_H
#define AHCI_H

#include "types.h"

/**
 * @file ahci.h
 * @brief The disk on a machine built after about 2008.
 *
 * v1.4.0 taught the kernel to ask the bus what it had, and on a modern board the
 * answer came back as a storage controller it could name and could not drive.
 * This is the driver that answers it. Nothing under fs/ changes: AHCI registers
 * as a blockdev_t exactly as the IDE driver does, which is what the block layer
 * was built for in v1.2.0.
 *
 * Three things are deliberately absent, and each is absent for a reason rather
 * than for lack of time.
 *
 * It polls. The controller can raise an interrupt, but on the machine this
 * targets that interrupt travels the PCI interrupt line to an IOAPIC, and this
 * kernel has no IOAPIC, no ACPI to describe the routing, and an interrupt
 * dispatcher that is a hand-written chain of comparisons in isr.c. Polling a
 * completion bit with a timeout is what the driver can do correctly today.
 *
 * It uses one port and one command slot. The hardware offers up to 32 ports and
 * 32 outstanding commands per port; the block layer above asks for one sector at
 * a time and the kernel does not preempt, so a second slot would have no caller.
 *
 * It transfers one sector per command. That is the block interface's shape, not
 * the hardware's - AHCI would happily read 64 sectors in a single command, and a
 * format of a large disk would be dramatically faster for it. Changing that
 * means changing blockdev_t, which is a decision about every driver rather than
 * about this one.
 */

/**
 * @brief The wait budget for anything this driver polls, in timer ticks.
 *
 * Ticks, and the name says so. The IDE driver called the same constant
 * ATA_TIMEOUT_MS for four years while comparing it against a tick count at
 * TIMER_HZ, so the budget it documented was ten times shorter than the budget it
 * enforced.
 */
#define AHCI_TIMEOUT_TICKS 200

/* HBA registers, as offsets from the mapped ABAR. */
#define AHCI_REG_CAP  0x00  /**< Host capabilities.                     */
#define AHCI_REG_GHC  0x04  /**< Global host control.                   */
#define AHCI_REG_IS   0x08  /**< Interrupt status.                      */
#define AHCI_REG_PI   0x0C  /**< Ports implemented, one bit per port.   */
#define AHCI_REG_VS   0x10  /**< Version.                               */

#define AHCI_GHC_HR   0x00000001  /**< HBA reset; self-clearing.        */
#define AHCI_GHC_AE   0x80000000  /**< AHCI enable.                     */

/** @brief Port register blocks start here, one 0x80-byte block per port. */
#define AHCI_PORT_BASE   0x100
#define AHCI_PORT_STRIDE 0x80

/* Port registers, as offsets within a port's block. */
#define AHCI_PORT_CLB  0x00  /**< Command list base, 1 KB aligned.      */
#define AHCI_PORT_CLBU 0x04  /**< Its upper 32 bits; always 0 here.     */
#define AHCI_PORT_FB   0x08  /**< Received FIS base, 256 B aligned.     */
#define AHCI_PORT_FBU  0x0C
#define AHCI_PORT_IS   0x10  /**< Interrupt status; write to clear.     */
#define AHCI_PORT_IE   0x14
#define AHCI_PORT_CMD  0x18  /**< Command and status.                   */
#define AHCI_PORT_TFD  0x20  /**< Task file data; the drive's status.   */
#define AHCI_PORT_SIG  0x24  /**< Device signature.                     */
#define AHCI_PORT_SSTS 0x28  /**< SATA status.                          */
#define AHCI_PORT_SERR 0x30  /**< SATA error; write to clear.           */
#define AHCI_PORT_CI   0x38  /**< Commands issued, one bit per slot.    */

#define AHCI_CMD_ST   0x0001  /**< Start; the port processes commands.  */
#define AHCI_CMD_FRE  0x0010  /**< FIS receive enable.                  */
#define AHCI_CMD_FR   0x4000  /**< FIS receive running.                 */
#define AHCI_CMD_CR   0x8000  /**< Command list running.                */

#define AHCI_TFD_ERR  0x01    /**< The drive reported an error.         */
#define AHCI_TFD_DRQ  0x08
#define AHCI_TFD_BSY  0x80

#define AHCI_PORT_IS_TFES 0x40000000  /**< Task file error.             */

/** @brief SSTS.DET: a device is present and communication is established. */
#define AHCI_DET_PRESENT 0x3

/** @brief SSTS.IPM: the interface is active rather than parked. */
#define AHCI_IPM_ACTIVE  0x1

/** @brief Signature of a plain SATA disk. Anything else is not ours. */
#define AHCI_SIG_ATA 0x00000101

/*
 * ATA commands, issued through the command FIS.
 *
 * The identify command carries the AHCI_ prefix although it is the same 0xEC the
 * IDE driver sends: ata.h defines ATA_CMD_IDENTIFY, kernel.c includes both
 * headers, and two spellings of the same number in one translation unit is a
 * thing to avoid rather than to rely on the preprocessor forgiving.
 */
#define ATA_CMD_READ_DMA_EX  0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define AHCI_ATA_IDENTIFY    0xEC

/** @brief Host-to-device register FIS. */
#define FIS_TYPE_REG_H2D 0x27

/**
 * @brief One entry of the command list.
 *
 * Exactly 32 bytes, and the test module asserts it - the controller indexes this
 * array itself, so a field the compiler padded would move every entry after the
 * first and the hardware would read the wrong one.
 */
typedef struct {
    uint8_t  cfl_flags;      /**< Command FIS length in dwords, bits 0-4. */
    uint8_t  flags2;         /**< Bit 6 of the header's DW0: write.       */
    uint16_t prdtl;          /**< PRDT entry count.                       */
    volatile uint32_t prdbc; /**< Bytes the controller reports it moved.  */
    uint32_t ctba;           /**< Command table base; 128 B aligned.      */
    uint32_t ctbau;          /**< Its upper 32 bits; always 0 here.       */
    uint32_t reserved[4];
} __attribute__((packed)) ahci_cmd_header_t;

/** @brief One scatter-gather entry. 16 bytes. */
typedef struct {
    uint32_t dba;            /**< Data base address.                      */
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;            /**< Byte count minus one, bits 0-21.        */
} __attribute__((packed)) ahci_prdt_entry_t;

/**
 * @brief A command table with room for a single scatter-gather entry.
 *
 * The 0x80 bytes before the PRDT are fixed by the specification: a 64-byte
 * command FIS, a 16-byte ATAPI command this driver never sends, and 48 reserved
 * bytes. They are spelled out rather than skipped so that sizeof() means
 * something.
 */
typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    ahci_prdt_entry_t prdt[1];
} __attribute__((packed)) ahci_cmd_table_t;

/** @brief Host-to-device register FIS. 20 bytes, sent inside cfis. */
typedef struct {
    uint8_t fis_type;
    uint8_t pmport_c;   /**< Bit 7 set marks this as a command.          */
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0, lba1, lba2;
    uint8_t device;
    uint8_t lba3, lba4, lba5;
    uint8_t featureh;
    uint8_t countl, counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} __attribute__((packed)) ahci_fis_h2d_t;

/*
 * Where each structure sits inside the single physical page this driver owns.
 *
 * One page, and the arithmetic is the reason there is no contiguous physical
 * allocator in this tree: a command list is 1 KB, a received FIS area is 256
 * bytes, a command table with one PRDT entry is 144, and a sector is 512 - 2560
 * bytes with every alignment requirement satisfied inside 4096. pmm_alloc_frame()
 * returns exactly that, and a frame is contiguous by definition.
 */
#define AHCI_OFF_CMD_LIST  0x000  /**< 1 KB aligned, as CLB requires.    */
#define AHCI_OFF_FIS       0x400  /**< 256 B aligned, as FB requires.    */
#define AHCI_OFF_CMD_TABLE 0x500  /**< 128 B aligned, as CTBA requires.  */
#define AHCI_OFF_DATA      0x800  /**< The sector being moved.           */

/**
 * @brief Finds a SATA controller, brings up its first usable port, and registers
 *        the disk behind it as the root block device.
 *
 * Called from the boot path only when the IDE probe found nothing, so a machine
 * that has both is left doing exactly what it did before this driver existed.
 *
 * @return E_OK when a disk was registered, E_NODEV when there is no controller
 *         or no port with a disk on it, or a negative errno for a controller
 *         that would not start.
 */
int ahci_init(void);

#endif // AHCI_H
