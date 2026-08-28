/*
 * File: pci.c
 * Purpose: Enumerate the PCI bus, so the kernel can be told what machine it is
 *          running on instead of assuming.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Configuration space is reached the old way, through the two ports at 0xCF8 and
 * 0xCFC, rather than through the memory-mapped ECAM window a modern chipset also
 * offers. The ports work on every PC that has ever had PCI, including the ones
 * with ECAM, and they need nothing this kernel does not already have: ECAM would
 * need the physical window mapped, and mapping device memory is a thing this
 * release deliberately does not do yet.
 *
 * Nothing here drives anything. It reads, it records, and it can be asked what
 * it found. That is the whole of it, and the reason to build it now rather than
 * alongside the first driver is that both drivers this project needs - AHCI for
 * a SATA disk, XHCI for a USB keyboard - start by asking this exact question.
 */
#include "pci.h"
#include "io.h"
#include "klog.h"
#include "libft.h"
#include "stdio.h"

static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_count = 0;

/**
 * @brief Whether the enumeration ran out of table before it ran out of bus.
 *
 * Kept so the count can be reported honestly. A truncated inventory is still a
 * useful inventory; one that says nothing about being truncated is a lie.
 */
static int pci_truncated = 0;

/*
 * The buses still to walk, and the ones already walked.
 *
 * A worklist rather than recursion. Bridges can in principle chain far enough to
 * matter, and this kernel's stack is 8 KB with interrupts landing on it - the
 * same reason sys_stack_dump() stages its rendering on the heap. The queue is
 * 256 bytes of static, the visited set is 32 more, and neither grows.
 *
 * The visited set is what makes a bridge that reports itself as its own
 * secondary bus a bounded mistake instead of an unbounded one. Nothing in QEMU
 * does that; a broken or malicious device on real hardware can.
 */
static uint8_t  bus_queue[256];
static int      bus_queue_len = 0;
static uint32_t bus_visited[8];

static int bus_seen(uint8_t bus) {
    return (bus_visited[bus >> 5] >> (bus & 31)) & 1;
}

static void bus_mark(uint8_t bus) {
    bus_visited[bus >> 5] |= (uint32_t)1 << (bus & 31);
}

/**
 * @brief Adds a bus to the worklist unless it has already been walked.
 */
static void bus_enqueue(uint8_t bus) {
    if (bus_seen(bus)) return;
    bus_mark(bus);
    if (bus_queue_len < (int)sizeof(bus_queue)) {
        bus_queue[bus_queue_len++] = bus;
    }
}

/**
 * @brief Builds the value port 0xCF8 wants.
 *
 * Bit 31 is the enable bit; without it the read goes nowhere and returns all
 * ones, which is indistinguishable from an empty slot. The low two bits of the
 * offset are cleared because the hardware addresses doublewords - passing an
 * unaligned offset is a caller's arithmetic, handled by the shifts below.
 */
static uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function,
                                   uint8_t offset) {
    return (uint32_t)0x80000000
         | ((uint32_t)bus      << 16)
         | ((uint32_t)(device & 0x1F) << 11)
         | ((uint32_t)(function & 0x07) << 8)
         | ((uint32_t)offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read32(bus, device, function, offset);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read32(bus, device, function, offset);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

/**
 * @brief Records one function, and queues the bus behind it if it is a bridge.
 *
 * @return 1 when the entry was recorded, 0 when the table is full.
 */
static int pci_record(uint8_t bus, uint8_t device, uint8_t function, uint16_t vendor) {
    if (pci_count >= PCI_MAX_DEVICES) {
        pci_truncated = 1;
        return 0;
    }

    pci_device_t *d = &pci_devices[pci_count];

    d->bus         = bus;
    d->device      = device;
    d->function    = function;
    d->vendor_id   = vendor;
    d->device_id   = pci_config_read16(bus, device, function, PCI_OFF_DEVICE_ID);
    d->revision    = pci_config_read8(bus, device, function, PCI_OFF_REVISION);
    d->prog_if     = pci_config_read8(bus, device, function, PCI_OFF_PROG_IF);
    d->subclass    = pci_config_read8(bus, device, function, PCI_OFF_SUBCLASS);
    d->class_code  = pci_config_read8(bus, device, function, PCI_OFF_CLASS);
    d->header_type = pci_config_read8(bus, device, function, PCI_OFF_HEADER_TYPE);

    /*
     * Read, not decoded. A type 1 header - a bridge - only has two of these and
     * the rest of that space means something else entirely, so the loop stops at
     * two for one. What is stored is what the register held; deciding whether a
     * BAR is memory or I/O, and how wide, belongs to whoever eventually maps it.
     */
    int bars = ((d->header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) ? 2 : 6;
    for (int i = 0; i < 6; i++) {
        d->bar[i] = (i < bars)
                  ? pci_config_read32(bus, device, function, (uint8_t)(PCI_OFF_BAR0 + i * 4))
                  : 0;
    }

    pci_count++;

    /*
     * Everything behind a bridge is on another bus, and nothing finds it unless
     * this walk goes there. The secondary bus number is the byte the bridge
     * publishes for exactly this purpose.
     */
    if ((d->header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
        bus_enqueue(pci_config_read8(bus, device, function, PCI_OFF_SECONDARY));
    }

    return 1;
}

/**
 * @brief Walks the eight possible functions of one device.
 *
 * Function 0 decides whether the other seven are asked about at all. A device
 * that does not set the multifunction bit is allowed to alias every function
 * onto function 0, so scanning all eight regardless would record the same part
 * eight times - and on real hardware, unlike in QEMU, some of those aliases
 * answer.
 */
static void pci_scan_device(uint8_t bus, uint8_t device) {
    uint16_t vendor = pci_config_read16(bus, device, 0, PCI_OFF_VENDOR_ID);
    if (vendor == PCI_NO_DEVICE) return;

    uint8_t header = pci_config_read8(bus, device, 0, PCI_OFF_HEADER_TYPE);

    if (!pci_record(bus, device, 0, vendor)) return;
    if (!(header & PCI_HEADER_MULTIFUNCTION)) return;

    for (uint8_t function = 1; function < 8; function++) {
        uint16_t fn_vendor = pci_config_read16(bus, device, function, PCI_OFF_VENDOR_ID);
        if (fn_vendor == PCI_NO_DEVICE) continue;
        if (!pci_record(bus, device, function, fn_vendor)) return;
    }
}

int pci_init(void) {
    /*
     * Emptied first, so a second call answers about the machine rather than
     * about the machine twice. init_fs() learned this in v1.3.0 when a test
     * called it twice; the test module for this file does the same on purpose.
     */
    pci_count = 0;
    pci_truncated = 0;
    bus_queue_len = 0;
    ft_memset(bus_visited, 0, sizeof(bus_visited));
    ft_memset(pci_devices, 0, sizeof(pci_devices));

    bus_enqueue(0);

    for (int i = 0; i < bus_queue_len; i++) {
        uint8_t bus = bus_queue[i];
        for (uint8_t device = 0; device < 32; device++) {
            pci_scan_device(bus, device);
        }
    }

    if (pci_count == 0) {
        klog(LOG_LEVEL_WARN, "PCI", "No devices answered on the bus.");
    } else {
        klog_int(LOG_LEVEL_INFO, "PCI", "Bus enumerated. Functions found", pci_count);
    }

    if (pci_truncated) {
        klog_int(LOG_LEVEL_WARN, "PCI",
                 "More functions exist than the table holds; the list stops at",
                 PCI_MAX_DEVICES);
    }

    return pci_count;
}

int pci_device_count(void) {
    return pci_count;
}

const pci_device_t *pci_get_device(int index) {
    if (index < 0 || index >= pci_count) return 0;
    return &pci_devices[index];
}

const pci_device_t *pci_find(uint8_t bus, uint8_t device, uint8_t function) {
    for (int i = 0; i < pci_count; i++) {
        if (pci_devices[i].bus == bus &&
            pci_devices[i].device == device &&
            pci_devices[i].function == function) {
            return &pci_devices[i];
        }
    }
    return 0;
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_count; i++) {
        if (pci_devices[i].class_code != class_code) continue;
        if (subclass != 0xFF && pci_devices[i].subclass != subclass) continue;
        return &pci_devices[i];
    }
    return 0;
}

/**
 * @brief Names the pairs this kernel has a reason to distinguish.
 *
 * Short on purpose. A full class list is a few hundred rows of table that would
 * be wrong in the same quiet way the vendor names would: nobody here can check
 * them against a machine. What is named is what this kernel either uses, refuses
 * to use, or is going to have to learn to use, and everything else gets a name
 * built from its class rather than a blank.
 */
const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case PCI_CLASS_MASS_STORAGE:
            if (subclass == PCI_SUBCLASS_IDE)  return "IDE controller";
            if (subclass == PCI_SUBCLASS_SATA) return "SATA controller";
            if (subclass == 0x08)              return "NVMe controller";
            return "Storage controller";
        case 0x02: return "Network controller";
        case 0x03: return "Display controller";
        case 0x04: return "Multimedia controller";
        case PCI_CLASS_BRIDGE:
            if (subclass == 0x00)                    return "Host bridge";
            if (subclass == 0x01)                    return "ISA bridge";
            if (subclass == PCI_SUBCLASS_PCI_BRIDGE) return "PCI bridge";
            return "Bridge";
        case 0x0C:
            if (subclass == 0x03) return "USB controller";
            return "Serial bus controller";
        case 0x00: return "Unclassified device";
        default:   return "Device";
    }
}

int pci_format_inventory(char *out, int cap) {
    int used = 0;

    if (out == 0 || cap <= 0) return 0;
    out[0] = '\0';

    if (pci_count == 0) {
        return kbprintf(out, (uint32_t)cap, 0, "No PCI devices found.\n");
    }

    for (int i = 0; i < pci_count; i++) {
        const pci_device_t *d = &pci_devices[i];

        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "%02x:%02x.%d %04x:%04x %s\n",
                        d->bus, d->device, d->function,
                        d->vendor_id, d->device_id,
                        pci_class_name(d->class_code, d->subclass));
    }

    /*
     * Said here as well as in the log, because the program that prints this is
     * where somebody counting devices will be looking.
     */
    if (pci_truncated) {
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "(list truncated at %d entries)\n", PCI_MAX_DEVICES);
    }

    return used;
}
