#ifndef PCI_H
#define PCI_H

#include "types.h"

/**
 * @file pci.h
 * @brief The first subsystem in this kernel that asks the machine what it is.
 *
 * Everything else assumes. The disk is at 0x1F0 because IDE controllers were,
 * the keyboard is at 0x60 because PS/2 controllers were, and the screen is at
 * 0xB8000 because VGA text mode was. Each of those assumptions is true of the
 * machine this project has always been tested on and false of the machine it is
 * aiming at, and until now there was no way for the kernel to find out which one
 * it had booted on.
 *
 * This does not fix that. It is the thing that has to exist before anything can:
 * neither AHCI nor XHCI can be written without a way to find a controller and
 * read where it decodes, and both of those are what "boot from a USB stick on a
 * real machine" eventually means.
 *
 * What is deliberately not here: nothing in this header maps anything. The base
 * address registers are read and reported because they are what the enumeration
 * found, not because anything is ready to use them. A driver is the next
 * release's problem, and this one is honest about being an inventory.
 */

/**
 * @brief The most devices the enumeration will record.
 *
 * A bounded static table rather than a heap allocation, for the same reason the
 * rest of this kernel's tables are bounded: the limit is then a documented
 * number instead of a memory condition discovered at boot. A machine with more
 * than this many functions gets the first PCI_MAX_DEVICES of them and a log line
 * saying so - the count stops, it does not wrap and it does not overflow.
 */
#define PCI_MAX_DEVICES 32

/**
 * @brief Buffer the rendered inventory needs, terminator included.
 *
 * Derived from the table size rather than written out, so that raising
 * PCI_MAX_DEVICES cannot leave the rendering silently truncated at the old
 * number of lines. 48 bytes covers "bb:dd.f vvvv:dddd " plus the longest name
 * pci_class_name() returns, and the slack on the end covers the truncation
 * notice.
 */
#define PCIINFO_BUF (PCI_MAX_DEVICES * 48 + 64)

/* Configuration space ports. Address out, data back, doublewords only. */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* Offsets into the configuration header this code reads. */
#define PCI_OFF_VENDOR_ID   0x00
#define PCI_OFF_DEVICE_ID   0x02
#define PCI_OFF_REVISION    0x08
#define PCI_OFF_PROG_IF     0x09
#define PCI_OFF_SUBCLASS    0x0A
#define PCI_OFF_CLASS       0x0B
#define PCI_OFF_HEADER_TYPE 0x0E
#define PCI_OFF_BAR0        0x10
#define PCI_OFF_SECONDARY   0x19

/**
 * @brief The answer a configuration read gives when nothing is there.
 *
 * An x86 port nobody decodes reads back all ones, and PCI leans on that: a
 * vendor ID of 0xFFFF is the specified way to say "no function here". It is the
 * same 0xFF that hung ata_identify() until v1.4.0, read as a word.
 */
#define PCI_NO_DEVICE 0xFFFF

/** @brief Set in the header type byte when functions 1-7 may also exist. */
#define PCI_HEADER_MULTIFUNCTION 0x80

/** @brief Header type of a PCI-to-PCI bridge, whose secondary bus is scanned. */
#define PCI_HEADER_TYPE_BRIDGE 0x01

/* The class codes this kernel names or looks for. */
#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_CLASS_BRIDGE       0x06
#define PCI_SUBCLASS_IDE       0x01
#define PCI_SUBCLASS_SATA      0x06
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

/**
 * @brief One function found on the bus.
 *
 * A function rather than a device: a physical part may present up to eight of
 * them and they are independent - on the machine this is tested on, the IDE
 * controller and the ISA bridge are two functions of the same device.
 */
typedef struct {
    uint8_t  bus;          /**< Bus number the function answered on.        */
    uint8_t  device;       /**< Device number, 0-31.                        */
    uint8_t  function;     /**< Function number, 0-7.                       */
    uint16_t vendor_id;    /**< Never PCI_NO_DEVICE for a recorded entry.   */
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;  /**< With the multifunction bit still in it.     */
    uint32_t bar[6];       /**< As read. Not decoded, not sized, not mapped.*/
} pci_device_t;

/**
 * @brief Reads a doubleword from a function's configuration space.
 *
 * @param bus Bus number.
 * @param device Device number, 0-31.
 * @param function Function number, 0-7.
 * @param offset Byte offset into the 256-byte header; the low two bits are
 *               ignored, as the hardware ignores them.
 * @return The doubleword, or 0xFFFFFFFF when nothing decodes the address.
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/** @brief The 16-bit read at @p offset. Errors as pci_config_read32(). */
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/** @brief The 8-bit read at @p offset. Errors as pci_config_read32(). */
uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/**
 * @brief Walks the bus and records what answered.
 *
 * Safe to call more than once: the table is emptied first. That is not
 * hypothetical tidiness - init_fs() had to learn the same thing in v1.3.0 when a
 * test called it twice, and the test module here calls this twice on purpose.
 *
 * @return The number of functions recorded.
 */
int pci_init(void);

/** @brief How many functions the last enumeration recorded. */
int pci_device_count(void);

/**
 * @brief The recorded function at @p index.
 *
 * @param index 0 to pci_device_count() - 1.
 * @return The entry, or 0 when @p index addresses nothing.
 */
const pci_device_t *pci_get_device(int index);

/**
 * @brief Finds a recorded function by its address on the bus.
 *
 * @return The entry, or 0 when that address recorded nothing.
 */
const pci_device_t *pci_find(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Finds the first recorded function of a class.
 *
 * @param class_code The class to look for.
 * @param subclass The subclass, or 0xFF for any subclass of @p class_code.
 * @return The entry, or 0 when nothing of that class was found.
 */
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);

/**
 * @brief A short human name for a class and subclass pair.
 *
 * Never 0 and never empty: an unrecognised pair gets a generic name rather than
 * a blank column, because a device this kernel cannot name is still a device the
 * reader needs to see.
 */
const char *pci_class_name(uint8_t class_code, uint8_t subclass);

/**
 * @brief Renders the recorded functions as text, one per line.
 *
 * The format is "bb:dd.f vvvv:dddd Name". Used by SYSCALL_PCIINFO, and by the
 * boot log through a caller of its own.
 *
 * @param out Destination buffer.
 * @param cap Its capacity, including room for the terminator.
 * @return Bytes written, not counting the terminator.
 */
int pci_format_inventory(char *out, int cap);

#endif // PCI_H
