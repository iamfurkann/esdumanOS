/*
 * File: test_xhci.c
 * Purpose: The USB controller, and the arithmetic that decided how it is fed.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Every kernel test target now starts QEMU with an xHCI controller, a USB
 * keyboard and a USB mouse on it - test_kernel, test_kernel_q35,
 * test_kernel_uefi and test_smap all get the same three devices from one
 * variable in the Makefile. Before this release none of them had a USB
 * controller at all, so a module written the way test_ahci.c is written - with
 * every assertion reaching the same verdict on a machine that has the hardware
 * and one that does not - would have been asserting almost nothing. Giving all
 * four the same hardware is what lets this module ask direct questions and still
 * report the same count everywhere, which is the constraint the whole suite
 * lives under.
 *
 * Two thirds of what is here is arithmetic rather than hardware, and that is the
 * point of it. The roadmap said this release needed a contiguous multi-frame
 * allocator; the numbers below are why mm/pmm.c was not touched, and they are
 * asserted rather than written in a comment because a comment would not have
 * failed when somebody raised XHCI_MAX_SLOTS past the room there is for it.
 *
 * xhci_init() is never called from here. On every one of these machines it has
 * already run and the controller is live with its rings installed; re-running it
 * would halt and reset a controller mid-suite. test_ahci.c refuses the same
 * thing for the same reason.
 */
#include "ktest.h"
#include "xhci.h"
#include "pci.h"
#include "pmm.h"
#include "libft.h"
#include "errno.h"

/**
 * @brief The shapes the controller reads out of memory on its own.
 *
 * A ring is an array the hardware indexes itself. A byte of padding the compiler
 * decided to insert would move every entry after the first, and the controller
 * would read half of one record and half of the next - with nothing in the
 * driver noticing and the device simply doing something else. The same argument
 * ahci_cmd_header_t carries, arrived at from the same direction.
 */
static void run_layout_assertions(void) {
    KTEST_ASSERT(sizeof(xhci_trb_t) == 16,
                 "[STRICT] [XHCI] a transfer request block is exactly 16 bytes, as the hardware indexes it");
    KTEST_ASSERT(sizeof(xhci_erst_entry_t) == 16,
                 "[STRICT] [XHCI] an event ring segment table entry is exactly 16 bytes");

    /*
     * The first half of the reason there is no contiguous allocator in this
     * tree: a ring segment is not merely small enough to fit a frame, it is
     * exactly a frame.
     */
    KTEST_ASSERT(XHCI_TRBS_PER_SEGMENT * sizeof(xhci_trb_t) == PAGE_SIZE,
                 "[STRICT] [XHCI] a ring segment is exactly one page, not merely smaller than one");

    uint32_t end_of_dcbaa      = XHCI_OFF_DCBAA + ((XHCI_MAX_SLOTS + 1) * 8);
    uint32_t end_of_erst       = XHCI_OFF_ERST + sizeof(xhci_erst_entry_t);
    uint32_t end_of_scratchpad = XHCI_OFF_SCRATCHPAD + (XHCI_MAX_SCRATCHPAD * 8);

    KTEST_ASSERT(end_of_dcbaa <= XHCI_OFF_ERST &&
                 end_of_erst  <= XHCI_OFF_SCRATCHPAD &&
                 end_of_scratchpad <= PAGE_SIZE,
                 "[STRICT] [XHCI] the context array, the segment table and the scratchpad array fit one page without overlapping");

    KTEST_ASSERT((XHCI_OFF_DCBAA % 64) == 0 &&
                 (XHCI_OFF_ERST % 64) == 0 &&
                 (XHCI_OFF_SCRATCHPAD % 64) == 0,
                 "[STRICT] [XHCI] each of the three sits where its register requires it to");
}

/**
 * @brief The placement rule a ring segment actually has, against a real frame.
 *
 * The specification does not ask for a ring segment to be page aligned. It asks
 * for it not to cross a 64 KB boundary, which is a different and weaker rule -
 * and the whole of why one call to pmm_alloc_frame() is enough here. A 4 KB
 * region that begins on a 4 KB boundary lies entirely within one 64 KB block by
 * arithmetic, and the frame allocator only ever returns 4 KB boundaries.
 *
 * Asserted against a frame the allocator actually handed out rather than as pure
 * algebra, because the claim being made is about pmm_alloc_frame() and not about
 * the number 4096.
 */
static void run_segment_placement_assertions(void) {
    uint32_t frame = pmm_alloc_frame();

    if (frame == 0xFFFFFFFFu) {
        KTEST_ASSERT(0, "[STRICT] [XHCI] a frame holds a ring segment without crossing a 64 KB boundary");
        return;
    }

    uint32_t segment_bytes = XHCI_TRBS_PER_SEGMENT * (uint32_t)sizeof(xhci_trb_t);

    KTEST_ASSERT((frame % PAGE_SIZE) == 0 &&
                 ((frame & 0xFFFF) + segment_bytes) <= 0x10000,
                 "[STRICT] [XHCI] a frame holds a ring segment without crossing a 64 KB boundary");

    pmm_free_frame(frame);
}

/**
 * @brief The two register fields that are not where they look like they are.
 *
 * The scratchpad buffer count is split across HCSPARAMS2 and the halves are not
 * adjacent: five bits at 25:21 are the high half and five bits at 31:27 are the
 * low. A reader who assumes one contiguous field gets a plausible small number
 * and under-allocates, and the controller then writes into memory it was never
 * given - which is the sort of defect that surfaces as something unrelated
 * failing much later.
 *
 * PORTSC is the other. Seven of its bits are write-one-to-clear change flags and
 * bit 1 is write-one-to-*disable*, so the ordinary read-modify-write used
 * everywhere else in this driver would acknowledge every recorded event and
 * switch off every working port in one instruction. XHCI_PORTSC_PRESERVE is what
 * stands between those two facts, and it is one constant that nothing else would
 * catch being edited.
 */
static void run_register_field_assertions(void) {
    KTEST_ASSERT(XHCI_HCS2_SPB(0) == 0 && XHCI_HCS2_SPB((uint32_t)1 << 27) == 1,
                 "[STRICT] [XHCI] the scratchpad count's low half is read from bits 31:27");
    KTEST_ASSERT(XHCI_HCS2_SPB((uint32_t)1 << 21) == 32,
                 "[STRICT] [XHCI] and its high half is read from bits 25:21 and is the high half");

    KTEST_ASSERT((XHCI_PORTSC_PRESERVE & 0x00FE0000u) == 0,
                 "[STRICT] [XHCI] the PORTSC write mask drops the write-one-to-clear change bits");
    KTEST_ASSERT((XHCI_PORTSC_PRESERVE & XHCI_PORTSC_PED) == 0 &&
                 (XHCI_PORTSC_PRESERVE & XHCI_PORTSC_PP) != 0,
                 "[STRICT] [XHCI] and it drops the bit that disables a port while keeping the one that powers it");

    KTEST_ASSERT(XHCI_TRB_TYPE(XHCI_TRB_SET_TYPE(XHCI_TRB_NOOP_CMD)) == XHCI_TRB_NOOP_CMD,
                 "[STRICT] [XHCI] a TRB type written into the control field reads back as itself");
    KTEST_ASSERT((XHCI_TRB_SET_TYPE(XHCI_TRB_CMD_COMPLETE) & XHCI_TRB_CYCLE) == 0,
                 "[STRICT] [XHCI] and it does not overlap the cycle bit the hardware owns");
}

/**
 * @brief Finding a controller that class and subclass alone cannot identify.
 *
 * UHCI, OHCI, EHCI and xHCI all report class 0x0C subclass 0x03 and are four
 * unrelated register files. A driver that used pci_find_class() here would map
 * whichever one the walk reached first and then quietly do nothing, on a machine
 * that has more than one - which is most machines built in the years these
 * generations overlapped.
 */
static void run_lookup_assertions(void) {
    const pci_device_t *xhci = pci_find_class_if(PCI_CLASS_SERIAL_BUS,
                                                 PCI_SUBCLASS_USB,
                                                 PCI_PROGIF_XHCI);

    KTEST_ASSERT(xhci != 0 && xhci->prog_if == PCI_PROGIF_XHCI,
                 "[STRICT] [XHCI] an xHCI controller is on the bus, found by its programming interface");

    KTEST_ASSERT(pci_find_class_if(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, 0xEE) == 0,
                 "[STRICT] [XHCI] a programming interface nothing implements is not found anyway");

    KTEST_ASSERT(pci_find_class_if(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB,
                                   PCI_MATCH_ANY) == xhci,
                 "[STRICT] [XHCI] the wildcard interface matches what the exact one matched");

    /*
     * The refactor's own assertion. pci_find_class() stopped having a loop of
     * its own this release and became a call to the three-field search with the
     * third wildcarded; this is the line that says the two still answer the same
     * question. It is asked about storage rather than USB because storage is
     * what pci_find_class()'s real callers ask about.
     */
    KTEST_ASSERT(pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_MATCH_ANY) ==
                 pci_find_class_if(PCI_CLASS_MASS_STORAGE, PCI_MATCH_ANY, PCI_MATCH_ANY),
                 "[STRICT] [PCI] the two-field search still answers as the three-field one with a wildcard");
}

/**
 * @brief The controller the boot path left running.
 *
 * xhci_running() asks the hardware whether it is halted rather than reporting
 * that bring-up finished, so this is a live question about the controller and
 * not a flag the driver set about itself.
 */
static void run_controller_assertions(void) {
    KTEST_ASSERT(xhci_running(),
                 "[STRICT] [XHCI] the controller is out of reset and not halted");

    int ports = xhci_port_count();

    KTEST_ASSERT(ports > 0 && ports <= XHCI_MAX_PORTS,
                 "[STRICT] [XHCI] the port count is within the table that records it");
}

/**
 * @brief The rendering, including what it does with a buffer that is too small.
 *
 * The buffer test is not decoration. This text is rendered into a kmalloc'd
 * buffer inside a syscall and copied out to a user pointer, so a renderer that
 * wrote past what it was given would be writing past a heap block on the way to
 * user space. kbprintf() is the thing that stops it, and this is the assertion
 * that says the renderer is actually going through it.
 */
static void run_inventory_assertions(void) {
    static char report[USBINFO_BUF];

    int n = xhci_format_inventory(report, sizeof(report));

    KTEST_ASSERT(n > 0 && n < (int)sizeof(report) &&
                 report[n] == '\0' && (int)ft_strlen(report) == n,
                 "[STRICT] [XHCI] the inventory renders, terminates, and reports the length it wrote");

    /*
     * A device is attached on every machine this suite runs on - the Makefile
     * gives all four targets a USB keyboard and a USB mouse - so at least one
     * port line has to say so. This is the assertion that would fail if the
     * ports were powered and never read, or read before they had settled.
     */
    KTEST_ASSERT(ft_strstr(report, "connected") != 0,
                 "[STRICT] [XHCI] and at least one port reports a device attached to it");

    /*
     * Sixteen bytes, with a known pattern behind them. A renderer that ignored
     * its capacity would overwrite the pattern; one that ignored it by a single
     * byte would overwrite exactly one, which is why the guard is checked byte
     * by byte rather than by looking at its first element.
     */
    char guarded[32];
    int untouched = 1;

    ft_memset(guarded, 0x5A, sizeof(guarded));

    int small = xhci_format_inventory(guarded, 16);

    for (int i = 16; i < 32; i++) {
        if ((unsigned char)guarded[i] != 0x5A) untouched = 0;
    }

    KTEST_ASSERT(untouched && small <= 15 && guarded[15] == '\0',
                 "[STRICT] [XHCI] a buffer smaller than the report is filled to its capacity and no further");

    KTEST_ASSERT(xhci_format_inventory(0, 64) == 0 &&
                 xhci_format_inventory(report, 0) == 0,
                 "[STRICT] [XHCI] a null buffer and a zero capacity are refused rather than written to");
}

/**
 * @brief The one number in this driver that must not come from sizeof().
 *
 * A context entry is eight doublewords on every controller ever made. The
 * distance between two of them is 32 or 64 bytes depending on HCCPARAMS1's CSZ
 * bit, and those are the same number on QEMU's controller and different on a
 * great many others. A driver that indexed contexts by the size of the structure
 * would read every one of them from the wrong offset on such a machine, and
 * nothing in this tree would ever fail - which is precisely the shape of defect
 * ata_identify() carried from v0.1.0 to v1.4.0.
 *
 * So the stride is asserted to be what the register says rather than what the
 * structure weighs, and the placement arithmetic is checked at *both* strides,
 * including the one this machine does not use. That second half is the only
 * thing in the suite that exercises the 64-byte layout at all.
 */
static void run_context_assertions(void) {
    KTEST_ASSERT(sizeof(xhci_context_t) == 32,
                 "[STRICT] [XHCI] a context entry is eight doublewords");

    uint32_t stride = xhci_context_stride();

    KTEST_ASSERT(stride == 32 || stride == 64,
                 "[STRICT] [XHCI] the context stride is one of the two the specification allows");
    KTEST_ASSERT(stride != 0 && (stride == 32) != (stride == 64),
                 "[STRICT] [XHCI] and the driver read it rather than leaving it unset");

    /*
     * Both strides, and the second is the point. At 64 bytes a device context is
     * 2048 and an input context 2112 - each fits a frame on its own, and the two
     * together are 4160, which is sixty-four bytes more than there is. That is
     * the whole argument for allocating them separately, and it is false at 32
     * bytes, so a test that only checked this machine's stride would prove
     * nothing about it.
     */
    for (uint32_t s = 32; s <= 64; s += 32) {
        uint32_t dev_ctx = XHCI_DEV_CTX_ENTRIES * s;
        uint32_t in_ctx  = XHCI_IN_CTX_ENTRIES * s;

        KTEST_ASSERT(dev_ctx <= PAGE_SIZE && in_ctx <= PAGE_SIZE,
                     s == 32
                       ? "[STRICT] [XHCI] at a 32-byte stride each context fits one frame"
                       : "[STRICT] [XHCI] and at a 64-byte stride each still does");
    }

    KTEST_ASSERT((XHCI_DEV_CTX_ENTRIES * 64) + (XHCI_IN_CTX_ENTRIES * 64) > PAGE_SIZE,
                 "[STRICT] [XHCI] but the two together do not, which is why they get a frame each");

    KTEST_ASSERT(XHCI_DESC_BUF_LEN == PAGE_SIZE,
                 "[STRICT] [XHCI] the descriptor buffer is exactly the frame it is allocated from");
}

/**
 * @brief The walk over bytes the device chose, including the ones it should not.
 *
 * Every length stepped by in a configuration descriptor came from the device.
 * A zero length is not a malformed field to step over - it is a walk that never
 * ends - and this is the same bound the extended capability chain got in v1.8.0,
 * reached from the other side of the machine. Driven with buffers made up here,
 * because a controller cannot be asked to produce a broken one.
 */
static void run_descriptor_walk_assertions(void) {
    xhci_config_info_t info;

    /* A configuration, one interface (HID boot keyboard), one endpoint. */
    static const uint8_t good[] = {
        0x09, USB_DESC_CONFIG, 25, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        0x09, USB_DESC_INTERFACE, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
        0x07, USB_DESC_ENDPOINT, 0x81, 0x03, 0x08, 0x00, 0x0A
    };

    KTEST_ASSERT(xhci_parse_config(good, sizeof(good), &info) == E_OK &&
                 info.interfaces == 1 && info.endpoints == 1 &&
                 info.total_length == 25,
                 "[STRICT] [XHCI] a configuration descriptor is walked and its parts counted");

    KTEST_ASSERT(info.iface_class == 0x03 && info.iface_subclass == 0x01 &&
                 info.iface_protocol == 0x01,
                 "[STRICT] [XHCI] and the first interface's class, subclass and protocol are kept");

    /* The same bytes with the interface claiming no length. */
    static const uint8_t zero_len[] = {
        0x09, USB_DESC_CONFIG, 25, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        0x00, USB_DESC_INTERFACE, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00
    };

    KTEST_ASSERT(xhci_parse_config(zero_len, sizeof(zero_len), &info) == E_INVAL,
                 "[STRICT] [XHCI] a descriptor claiming zero length is refused, not stepped over");

    static const uint8_t not_config[] = {
        0x09, USB_DESC_INTERFACE, 25, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32
    };

    KTEST_ASSERT(xhci_parse_config(not_config, sizeof(not_config), &info) == E_INVAL,
                 "[STRICT] [XHCI] a buffer that does not begin with a configuration is refused");

    KTEST_ASSERT(xhci_parse_config(good, 4, &info) == E_INVAL &&
                 xhci_parse_config(0, 32, &info) == E_INVAL,
                 "[STRICT] [XHCI] so are a buffer too short to hold one and no buffer at all");

    /*
     * A last descriptor the transfer cut in half. What arrived whole is counted
     * and the fragment is not, and nothing reads past the length given - which
     * is the difference between a short read and a buffer overrun.
     */
    KTEST_ASSERT(xhci_parse_config(good, sizeof(good) - 3, &info) == E_OK &&
                 info.interfaces == 1 && info.endpoints == 0,
                 "[STRICT] [XHCI] a descriptor cut short by the transfer is dropped, not read past");
}

/**
 * @brief The devices the boot path addressed.
 *
 * Every kernel test target attaches a USB keyboard and a USB mouse, so two
 * devices is what these machines have and one is the floor this asserts.
 */
static void run_device_assertions(void) {
    int n = xhci_device_count();

    KTEST_ASSERT(n > 0 && n <= XHCI_MAX_DEVICES,
                 "[STRICT] [XHCI] at least one device was given a slot and an address");

    KTEST_ASSERT(xhci_get_device(-1) == 0 && xhci_get_device(n) == 0,
                 "[STRICT] [XHCI] a negative index and one past the last address nothing");

    const xhci_device_t *d = xhci_get_device(0);

    if (d == 0) {
        KTEST_ASSERT(0, "[STRICT] [XHCI] the first device answers with a vendor and a product");
        KTEST_ASSERT(0, "[STRICT] [XHCI] and it sits on a port that reported it connected");
        KTEST_ASSERT(0, "[STRICT] [XHCI] and its configuration descriptor held an interface");
        return;
    }

    KTEST_ASSERT(d->vendor_id != 0 && d->vendor_id != 0xFFFF,
                 "[STRICT] [XHCI] the first device answers with a vendor and a product");
    KTEST_ASSERT(d->port >= 1 && d->port <= XHCI_MAX_PORTS && d->slot >= 1,
                 "[STRICT] [XHCI] and it sits on a port that reported it connected");
    KTEST_ASSERT(d->config.interfaces > 0 && d->config.endpoints > 0,
                 "[STRICT] [XHCI] and its configuration descriptor held an interface and an endpoint");

    /*
     * The observable consequence of the port reset this release added. Before
     * one, PORTSC's speed field is undefined and v1.8.0's lsusb printed "unknown
     * speed" for a device that was plainly attached.
     */
    KTEST_ASSERT(d->speed >= XHCI_SPEED_FULL && d->speed <= XHCI_SPEED_SUPER_PLUS,
                 "[STRICT] [XHCI] and its speed is a named one, which it only is after a reset");
}

void run_xhci_tests(void) {
    printk("\n--- XHCI and USB Bus Tests ---\n");

    run_layout_assertions();
    run_segment_placement_assertions();
    run_context_assertions();
    run_register_field_assertions();
    run_descriptor_walk_assertions();
    run_lookup_assertions();
    run_controller_assertions();
    run_device_assertions();
    run_inventory_assertions();
}
