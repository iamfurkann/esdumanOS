/*
 * File: test_ahci.c
 * Purpose: Device memory, and the SATA controller reached through it.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Every assertion here runs on both machines the suite is executed on and
 * reaches the same verdict on each, which is the constraint that shaped the
 * module. `make test_kernel` runs on i440fx, where there is no SATA controller
 * at all; `make test_kernel_q35` runs on q35, where there is one and the disk is
 * behind it. A module that asserted "AHCI is present" would pass on one and fail
 * on the other, and a module that skipped assertions on i440fx would report two
 * different totals for the same suite.
 *
 * So what is asserted here is the part that does not depend on the machine: the
 * shapes the controller reads out of memory, the mapping the driver needs before
 * it can reach a register at all, and the relationship between what the bus says
 * is present and which driver ended up owning the disk. The driver's actual
 * transfers are tested by the whole of the rest of the suite on q35 - test_vfs
 * and test_bcache do not know they are talking to AHCI, and that is exactly why
 * they are the test.
 *
 * ahci_init() is never called from here. On q35 it has already run and owns a
 * live disk; re-running it would stop the port under a mounted file system.
 */
#include "ktest.h"
#include "ahci.h"
#include "pci.h"
#include "paging.h"
#include "pmm.h"
#include "blockdev.h"
#include "errno.h"
#include "libft.h"

/**
 * @brief Reads a page table entry through the recursive mapping.
 *
 * The flags on a mapping are not observable any other way, and one of them is
 * the difference between a driver that works and one that silently does not.
 */
static uint32_t pte_for(uint32_t vaddr) {
    uint32_t pd_index = vaddr >> 22;
    uint32_t pt_index = (vaddr >> 12) & 0x3FF;
    uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    return pt[pt_index];
}

/**
 * @brief The shapes the controller reads out of memory on its own.
 *
 * These are not style assertions. The command list is an array the hardware
 * indexes itself, so a byte of padding the compiler inserted would move every
 * entry after the first and the controller would read a header that is half of
 * one command and half of the next. Nothing in the driver would notice; the disk
 * would simply do something else.
 */
static void run_layout_assertions(void) {
    KTEST_ASSERT(sizeof(ahci_cmd_header_t) == 32,
                 "[STRICT] [AHCI] a command header is exactly 32 bytes, as the hardware indexes it");
    KTEST_ASSERT(sizeof(ahci_prdt_entry_t) == 16,
                 "[STRICT] [AHCI] a scatter-gather entry is exactly 16 bytes");
    KTEST_ASSERT(sizeof(ahci_cmd_table_t) == 144,
                 "[STRICT] [AHCI] a command table with one entry is 128 bytes plus that entry");
    KTEST_ASSERT(sizeof(ahci_fis_h2d_t) == 20,
                 "[STRICT] [AHCI] a host-to-device register FIS is 20 bytes, five dwords");

    /*
     * The arithmetic that is the reason this tree has no contiguous multi-frame
     * allocator: everything the driver hands the controller fits in one frame,
     * and a frame is contiguous by definition.
     */
    uint32_t end_of_list  = AHCI_OFF_CMD_LIST  + 1024;
    uint32_t end_of_fis   = AHCI_OFF_FIS       + 256;
    uint32_t end_of_table = AHCI_OFF_CMD_TABLE + sizeof(ahci_cmd_table_t);
    uint32_t end_of_data  = AHCI_OFF_DATA      + 512;

    KTEST_ASSERT(end_of_list  <= AHCI_OFF_FIS &&
                 end_of_fis   <= AHCI_OFF_CMD_TABLE &&
                 end_of_table <= AHCI_OFF_DATA &&
                 end_of_data  <= PAGE_SIZE,
                 "[STRICT] [AHCI] all four structures fit one page without overlapping");

    KTEST_ASSERT((AHCI_OFF_CMD_LIST  % 1024) == 0 &&
                 (AHCI_OFF_FIS       % 256)  == 0 &&
                 (AHCI_OFF_CMD_TABLE % 128)  == 0,
                 "[STRICT] [AHCI] each structure sits where its register requires it to");
}

/**
 * @brief The mapping a driver needs before it can read a register at all.
 */
static void run_device_window_assertions(void) {
    KTEST_ASSERT(vmm_map_device(0x1000, 0) == 0,
                 "[STRICT] [VMM] a mapping of nothing is refused rather than handed back empty");

    /*
     * Larger than the whole window, in one request. Chosen over exhausting the
     * window a page at a time because that would work exactly once: the window
     * has no free, so a test that consumed it would take it away from every
     * later boot of this build.
     */
    uint32_t too_much = (DEVICE_WINDOW_END - DEVICE_WINDOW_BASE) + PAGE_SIZE;

    KTEST_ASSERT(vmm_map_device(0x1000, too_much) == 0,
                 "[STRICT] [VMM] a region larger than the window is refused");

    uint32_t frame = pmm_alloc_frame();

    if (frame == 0xFFFFFFFFu) {
        KTEST_ASSERT(0, "[STRICT] [VMM] a frame was available to map into the device window");
        KTEST_ASSERT(0, "[STRICT] [VMM] the offset within the page survives the mapping");
        KTEST_ASSERT(0, "[STRICT] [VMM] device memory is mapped with caching disabled");
        return;
    }

    /* Deliberately not page aligned, so the offset has something to preserve. */
    uint8_t *mapped = (uint8_t *)vmm_map_device(frame + 0x40, 64);

    KTEST_ASSERT(mapped != 0 &&
                 (uint32_t)mapped >= DEVICE_WINDOW_BASE &&
                 (uint32_t)mapped <  DEVICE_WINDOW_END,
                 "[STRICT] [VMM] a frame was available to map into the device window");

    if (mapped == 0) {
        KTEST_ASSERT(0, "[STRICT] [VMM] the offset within the page survives the mapping");
        KTEST_ASSERT(0, "[STRICT] [VMM] device memory is mapped with caching disabled");
        pmm_free_frame(frame);
        return;
    }

    KTEST_ASSERT(((uint32_t)mapped & 0xFFF) == 0x40,
                 "[STRICT] [VMM] the offset within the page survives the mapping");

    /*
     * The one that matters most and is invisible from anywhere else. A control
     * register mapped write-back takes a write into a cache line and the device
     * never sees it; a status register read the same way answers with what the
     * cache remembers. The symptom is a device that ignores the driver, with
     * nothing anywhere reporting an error.
     */
    uint32_t entry = pte_for((uint32_t)mapped & 0xFFFFF000u);

    KTEST_ASSERT((entry & 0x10) != 0 && (entry & 0x01) != 0,
                 "[STRICT] [VMM] device memory is mapped with caching disabled");

    /*
     * Put the frame back. The window page it was mapped into is not recovered -
     * the allocator there is a bump pointer with no free, which is a documented
     * limit rather than an oversight - but leaving the frame allocated as well
     * would be this module quietly costing the system a page per boot.
     */
    unmap_page((uint32_t)mapped & 0xFFFFF000u);
    pmm_free_frame(frame);
}

/**
 * @brief Turning a device on, and not turning anything else off.
 *
 * Run against the host bridge, which every PC has at 00:00.0 and which is
 * already decoding memory long before this test - so the call is idempotent on
 * both machines and changes nothing that was not already true.
 */
static void run_pci_enable_assertions(void) {
    KTEST_ASSERT(pci_enable_device(0) == E_INVAL,
                 "[STRICT] [PCI] enabling nothing is refused rather than acted on");

    const pci_device_t *bridge = pci_find(0, 0, 0);

    if (bridge == 0) {
        KTEST_ASSERT(0, "[STRICT] [PCI] the two bits it is asked to set are set afterwards");
        KTEST_ASSERT(0, "[STRICT] [PCI] and the bits it was not asked about are untouched");
        return;
    }

    uint16_t before = pci_config_read16(0, 0, 0, PCI_OFF_COMMAND);

    pci_enable_device(bridge);

    uint16_t after = pci_config_read16(0, 0, 0, PCI_OFF_COMMAND);

    KTEST_ASSERT((after & (PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER)) ==
                 (PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER),
                 "[STRICT] [PCI] the two bits it is asked to set are set afterwards");

    /*
     * A driver that writes the value it wants rather than the bits it needs
     * takes responsibility for settings it never looked at - interrupt disable,
     * parity checking, whatever the firmware decided.
     */
    uint16_t untouched = (uint16_t)~(PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER);

    KTEST_ASSERT((after & untouched) == (before & untouched),
                 "[STRICT] [PCI] and the bits it was not asked about are untouched");
}

/**
 * @brief What the bus says is present, and which driver ended up with the disk.
 *
 * The same three assertions reach the same verdict on both machines by asking
 * about the relationship rather than about the hardware: on i440fx there is an
 * IDE controller and the root device is the IDE driver's, on q35 there is a SATA
 * controller and it is the AHCI driver's, and in both cases exactly one of the
 * two answered.
 */
static void run_root_device_assertions(void) {
    blockdev_t *root = blockdev_root();

    KTEST_ASSERT(root != 0 && root->sector_count > 0 && root->read != 0,
                 "[STRICT] [AHCI] a disk is registered and reports a capacity");

    if (root == 0) {
        KTEST_ASSERT(0, "[STRICT] [AHCI] the registered disk belongs to one of the two drivers");
        KTEST_ASSERT(0, "[STRICT] [AHCI] and it is the one the bus says this machine has");
        return;
    }

    int is_ide  = (ft_strcmp(root->name, "ata0") == 0);
    int is_sata = (ft_strcmp(root->name, "sata0") == 0);

    KTEST_ASSERT(is_ide != is_sata,
                 "[STRICT] [AHCI] the registered disk belongs to one of the two drivers");

    /*
     * IDE first, and this is the assertion that says the boot path kept that
     * order. A machine with an IDE controller must still be using it: the whole
     * argument for adding a second storage driver without risk was that the
     * first one is tried first and a machine that had a disk before still has
     * the same one.
     */
    int has_ide  = (pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE) != 0);
    int has_sata = (pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA) != 0);

    KTEST_ASSERT((has_ide && is_ide) || (!has_ide && has_sata && is_sata),
                 "[STRICT] [AHCI] and it is the one the bus says this machine has");
}

void run_ahci_tests(void) {
    printk("\n--- AHCI and Device Memory Tests ---\n");

    run_layout_assertions();
    run_device_window_assertions();
    run_pci_enable_assertions();
    run_root_device_assertions();
}
