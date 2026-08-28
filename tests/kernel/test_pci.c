/*
 * File: test_pci.c
 * Purpose: The bus, and what the machine says is attached to it.
 *
 * This file is part of the esdumanOS test suite.
 *
 * There is no fake device here, and that is a limitation rather than a choice.
 * The block layer could be tested against an invented disk because blockdev_t is
 * a seam with function pointers in it; configuration space is two I/O ports, and
 * nothing this side of QEMU can stand between the driver and them. So these
 * assertions are about the enumeration rather than about the hardware: that what
 * was recorded matches what a fresh read of the same registers says, that the
 * absent answers are recognised as absent, and that the walk obeyed the rule
 * that decides whether a function exists at all.
 *
 * The few that do assume the machine assume only what the test target already
 * guarantees: make test_kernel starts QEMU with an IDE disk attached, so a mass
 * storage controller is present by construction, and the host bridge at 00:00.0
 * is present on every PC that has a PCI bus.
 *
 * Nothing here writes to configuration space. Not even a BAR size probe, which
 * is the one write that looks harmless and is not - it moves where a device
 * decodes for as long as it takes to read the answer back.
 */
#include "ktest.h"
#include "pci.h"

/**
 * @brief The bus answered, and it answered within the table.
 */
static void run_enumeration_assertions(void) {
    int count = pci_device_count();

    KTEST_ASSERT(count > 0,
                 "[STRICT] [PCI] the bus was enumerated and something answered");
    KTEST_ASSERT(count <= PCI_MAX_DEVICES,
                 "[STRICT] [PCI] the recorded count never exceeds the table it is stored in");
}

/**
 * @brief Present and absent, told apart by the answer the hardware gives.
 *
 * 00:00.0 is the host bridge; a PC with a PCI bus has one. Device 31 function 7
 * is chosen for the other side because nothing on this machine type sits there,
 * and an address nobody decodes is what 0xFFFF is specified to mean.
 */
static void run_presence_assertions(void) {
    KTEST_ASSERT(pci_config_read16(0, 0, 0, PCI_OFF_VENDOR_ID) != PCI_NO_DEVICE,
                 "[STRICT] [PCI] the host bridge at 00:00.0 reports a real vendor");
    KTEST_ASSERT(pci_config_read16(0, 31, 7, PCI_OFF_VENDOR_ID) == PCI_NO_DEVICE,
                 "[STRICT] [PCI] an address nothing decodes reads back as all ones");
    KTEST_ASSERT(pci_find(0, 31, 7) == 0,
                 "[STRICT] [PCI] and that address was not recorded as a device");
}

/**
 * @brief What was stored is what the registers hold.
 *
 * The failure this catches is the quiet one: a shift or an offset that is wrong
 * by two bytes still fills the table with plausible-looking numbers. Reading the
 * same fields again through a different path and comparing is the only way to
 * tell a correct table from a consistent misreading of it.
 */
static void run_fidelity_assertions(void) {
    const pci_device_t *d = pci_get_device(0);

    if (d == 0) {
        KTEST_ASSERT(0, "[STRICT] [PCI] the first entry exists to be checked against the bus");
        return;
    }

    uint16_t vendor = pci_config_read16(d->bus, d->device, d->function, PCI_OFF_VENDOR_ID);
    uint8_t  cls    = pci_config_read8(d->bus, d->device, d->function, PCI_OFF_CLASS);
    uint8_t  sub    = pci_config_read8(d->bus, d->device, d->function, PCI_OFF_SUBCLASS);

    KTEST_ASSERT(d->vendor_id == vendor,
                 "[STRICT] [PCI] the stored vendor id matches a fresh read of the same register");
    KTEST_ASSERT(d->class_code == cls && d->subclass == sub,
                 "[STRICT] [PCI] the stored class and subclass match a fresh read of theirs");
}

/**
 * @brief The lookups, in both directions.
 */
static void run_lookup_assertions(void) {
    KTEST_ASSERT(pci_find_class(PCI_CLASS_MASS_STORAGE, 0xFF) != 0,
                 "[STRICT] [PCI] a mass storage controller is found, as the test machine has a disk");
    KTEST_ASSERT(pci_find_class(0xFE, 0xFE) == 0,
                 "[STRICT] [PCI] a class nothing implements is not found anyway");
    KTEST_ASSERT(pci_get_device(0) != 0,
                 "[STRICT] [PCI] index zero addresses a recorded device");
    KTEST_ASSERT(pci_get_device(pci_device_count()) == 0,
                 "[STRICT] [PCI] one past the last index addresses nothing");
    KTEST_ASSERT(pci_get_device(-1) == 0,
                 "[STRICT] [PCI] a negative index addresses nothing rather than reading backwards");
}

/**
 * @brief Every entry is a device, and every entry was allowed to be scanned.
 *
 * The second of these is the correctness property of the walk itself. A device
 * that does not set the multifunction bit may alias all eight functions onto
 * function 0, so a scanner that asks about functions 1-7 regardless records the
 * same part up to eight times. QEMU does not alias, which is exactly why this
 * has to be asserted rather than observed - the machine that punishes the
 * mistake is the one this code is being written for and cannot be run on.
 */
static void run_walk_assertions(void) {
    int count = pci_device_count();
    int phantoms = 0;
    int unannounced = 0;

    for (int i = 0; i < count; i++) {
        const pci_device_t *d = pci_get_device(i);

        if (d->vendor_id == PCI_NO_DEVICE) phantoms++;
        if (d->function == 0) continue;

        const pci_device_t *fn0 = pci_find(d->bus, d->device, 0);
        if (fn0 == 0 || !(fn0->header_type & PCI_HEADER_MULTIFUNCTION)) unannounced++;
    }

    KTEST_ASSERT(phantoms == 0,
                 "[STRICT] [PCI] no recorded entry is the empty answer stored as a device");
    KTEST_ASSERT(unannounced == 0,
                 "[STRICT] [PCI] no function above zero was recorded unless function zero announced it");
}

/**
 * @brief Naming, including the case where there is no name.
 */
static void run_naming_assertions(void) {
    const char *ide = pci_class_name(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE);
    const char *unknown = pci_class_name(0xFE, 0xFE);

    KTEST_ASSERT(ide != 0 && ide[0] == 'I' && ide[1] == 'D' && ide[2] == 'E',
                 "[STRICT] [PCI] a class this kernel knows is named as itself");
    KTEST_ASSERT(unknown != 0 && unknown[0] != '\0' && unknown != ide,
                 "[STRICT] [PCI] a class it does not know still gets a name rather than a blank");
}

/**
 * @brief Enumerating twice describes the machine once.
 *
 * init_fs() had to learn this in v1.3.0 when a test called it twice and the
 * second call allocated over the first one's tables. The same shape is available
 * here for free - a scan that appends instead of replacing would double the
 * count - so it is asserted before it can be discovered.
 */
static void run_reentry_assertions(void) {
    int first = pci_device_count();
    int second = pci_init();

    KTEST_ASSERT(second == first,
                 "[STRICT] [PCI] a second enumeration reports the same machine, not twice as much of it");
    KTEST_ASSERT(pci_device_count() == first,
                 "[STRICT] [PCI] and the table is left as the rest of the suite found it");
}

void run_pci_tests(void) {
    printk("\n--- PCI Bus Tests ---\n");

    run_enumeration_assertions();
    run_presence_assertions();
    run_fidelity_assertions();
    run_lookup_assertions();
    run_walk_assertions();
    run_naming_assertions();
    run_reentry_assertions();
}
