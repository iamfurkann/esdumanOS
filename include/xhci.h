#ifndef XHCI_H
#define XHCI_H

#include "types.h"

/**
 * @file xhci.h
 * @brief The bus the keyboard and the stick are both on.
 *
 * v1.6.0 gave the screen a second backend and v1.7.0 gave the image a second way
 * to be booted. What is left between this kernel and a real machine is that its
 * keyboard reads port 0x60 and its disk is a controller a laptop does not have.
 * Both of those are USB on the machine this project is aiming at, and this is
 * the controller they arrive through.
 *
 * v1.8.0 brought the bus up and proved it with a No Op command. v1.9.0 talks to
 * what is on it: every connected port is reset, its device given a slot and an
 * address, and asked to describe itself. What comes back is a vendor, a product,
 * a class and a count of interfaces and endpoints - which is what turns lsusb
 * from a list of ports into a list of devices.
 *
 * It stops there. No interface is selected, no configuration is set, and no
 * endpoint other than the control one is ever opened, so nothing on this bus can
 * yet send anything. That is the keyboard's release, and the reason it is
 * separate is that this file was already 944 lines before any of the above.
 *
 * Four absences, each argued rather than pending.
 *
 * It polls, and it polls inline. The same reason ahci.h gives: the interrupt
 * this controller can raise travels a PCI line to an IOAPIC, and this kernel has
 * no IOAPIC, no ACPI to describe the routing, and an interrupt dispatcher that
 * is a hand-written chain of comparisons in isr.c. The roadmap put the event
 * ring's timer-tick poll in v1.8.0 and it had no caller there; it still has
 * none, because every transfer here is issued on the boot path and waited for.
 * It arrives with the interrupt endpoint that needs it.
 *
 * It asks the physical allocator for one frame at a time. The roadmap also said
 * this release needed contiguous multi-frame allocation, and the arithmetic
 * below says otherwise: the largest thing here is a 256-entry ring segment,
 * which is 4096 bytes, and the only placement rule a segment has is that it may
 * not cross a 64 KB boundary - which a 4 KB-aligned frame cannot do. The
 * scratchpad buffers are the case that looks like it needs contiguity and does
 * not: the array holds each buffer's address separately, so they may be
 * anywhere. mm/pmm.c is untouched by this release, exactly as it was by v1.5.0
 * when the same sentence was written about AHCI.
 *
 * It uses one interrupter and one event ring segment. The hardware offers up to
 * 1024 interrupters; with interrupts masked there is nothing for a second one to
 * do.
 *
 * It takes the controller away from the firmware. That is the one thing here
 * that cannot be tested on this project's machines: QEMU's controller publishes
 * no USB Legacy Support capability, so the handoff below is written from the
 * specification and never executes under the test suite. On a UEFI machine the
 * firmware has been driving this controller to read its own boot keyboard, and a
 * driver that starts programming registers without asking for them first is
 * fighting something that is still running.
 */

/**
 * @brief The wait budget for anything this driver polls, in timer ticks.
 *
 * Ticks, and the name says so - ahci.h explains why that matters, and the IDE
 * driver spent four years being the example.
 */
#define XHCI_TIMEOUT_TICKS 200

/**
 * @brief How long the ports are given to settle after they are powered.
 *
 * The specification asks for 20 ms between raising port power and believing the
 * connect status. The PIT runs at TIMER_HZ, which is 100, so a tick is 10 ms and
 * three of them is 30 - rounded up rather than down, because the cost of waiting
 * one tick too long is a third of a frame and the cost of one tick too few is a
 * device that is attached and reported as absent.
 */
#define XHCI_PORT_SETTLE_TICKS 3

/**
 * @brief The most ports this driver will record and report.
 *
 * A bounded static table, for the reason PCI_MAX_DEVICES is bounded: the limit
 * is then a documented number rather than a memory condition discovered at boot.
 * The register field is eight bits wide, so a controller may claim up to 255;
 * one that claims more than this gets the first XHCI_MAX_PORTS reported and a
 * line saying the list stopped.
 */
#define XHCI_MAX_PORTS 32

/**
 * @brief The most device slots this driver will enable.
 *
 * This is what CONFIG.MaxSlotsEn is programmed with, and it sizes the device
 * context array. It is the ceiling on slots the controller will hand out; how
 * many of them this driver is prepared to follow up on with four frames each is
 * XHCI_MAX_DEVICES, which is smaller and is the number that actually binds.
 */
#define XHCI_MAX_SLOTS 32

/**
 * @brief The most scratchpad buffers this driver will hand the controller.
 *
 * The controller asks for these for its own private use and the count comes out
 * of HCSPARAMS2. QEMU's asks for none, so the path that allocates them does not
 * run under any test target in this tree; real controllers ask for up to about
 * 32. A controller asking for more than this is refused with its number in the
 * log rather than half-served, because a scratchpad array the hardware believes
 * is longer than it is would be read past its end by the hardware.
 */
#define XHCI_MAX_SCRATCHPAD 64

/**
 * @brief The most devices this driver will address and describe.
 *
 * Four frames go to each one - a device context, an input context, a transfer
 * ring for endpoint zero and a buffer to read descriptors into - so this is a
 * memory budget as much as a table size. It is well below XHCI_MAX_SLOTS because
 * enabling a slot is cheap and addressing a device is not, and nothing in this
 * kernel yet has a use for more than a keyboard and a stick at once.
 */
#define XHCI_MAX_DEVICES 8

/**
 * @brief Buffer the rendered inventory needs, terminator included.
 *
 * Derived from the tables rather than written out, so raising XHCI_MAX_PORTS or
 * XHCI_MAX_DEVICES cannot leave the rendering silently truncated at the old
 * number of lines. 48 bytes covers the longest port line and 72 the longest
 * device line; the 128 covers the controller's own line and the truncation
 * notice.
 *
 * apps/bin/lsusb.c carries a buffer that has to match this. A short one there is
 * not a fault - the kernel copies at most what it is given - but it would lose
 * the end of the list, which is the half somebody running lsusb is least likely
 * to already know about.
 */
#define USBINFO_BUF (XHCI_MAX_PORTS * 48 + XHCI_MAX_DEVICES * 72 + 128)

/*
 * The PCI identity this driver looks for lives in pci.h with the rest of the
 * class codes, not here. Class and subclass are not enough to find it - UHCI,
 * OHCI, EHCI and xHCI are all 0x0C/0x03 and differ only in prog_if - which is
 * the reason pci_find_class_if() exists.
 */

/**
 * @brief How much of the controller's BAR this driver maps.
 *
 * Only the capability block is at a fixed offset. The operational block is at
 * CAPLENGTH, and the runtime and doorbell blocks are wherever DBOFF and RTSOFF
 * say - all three read out of registers rather than known in advance. 64 KB
 * covers every controller these have been observed on and is a bound rather than
 * a guess: the driver checks the offsets it read against this before using them,
 * because an offset past the end of the mapping is a write into whatever the
 * device window handed the next caller.
 */
#define XHCI_MMIO_WINDOW 0x10000

/* ── Capability registers, at offset 0 from the mapped BAR ──────────── */

#define XHCI_CAP_CAPLENGTH   0x00  /**< u8: where the operational block starts. */
#define XHCI_CAP_HCIVERSION  0x02  /**< u16: BCD, 0x0100 is 1.0, 0x0110 is 1.1. */
#define XHCI_CAP_HCSPARAMS1  0x04
#define XHCI_CAP_HCSPARAMS2  0x08
#define XHCI_CAP_HCCPARAMS1  0x10
#define XHCI_CAP_DBOFF       0x14  /**< Doorbell array offset; low 2 bits rsvd.  */
#define XHCI_CAP_RTSOFF      0x18  /**< Runtime block offset; low 5 bits rsvd.   */

/* HCSPARAMS1 fields. */
#define XHCI_HCS1_MAXSLOTS(v) ((v) & 0xFF)
#define XHCI_HCS1_MAXPORTS(v) (((v) >> 24) & 0xFF)

/*
 * HCSPARAMS2's scratchpad count, which is split across the register.
 *
 * The high five bits live at 25:21 and the low five at 31:27, and they are not
 * adjacent. A reader who assumes one contiguous field gets the low half and
 * silently under-allocates, which is the shape of bug that shows up as a
 * controller writing over memory it was never given.
 */
#define XHCI_HCS2_SPB(v) (((((v) >> 21) & 0x1F) << 5) | (((v) >> 27) & 0x1F))

/* HCCPARAMS1 fields. */
#define XHCI_HCC1_AC64(v) ((v) & 0x1)          /**< 64-bit addressing capable.  */
#define XHCI_HCC1_CSZ(v)  (((v) >> 2) & 0x1)   /**< Context size: 1 means 64 B. */
#define XHCI_HCC1_XECP(v) (((v) >> 16) & 0xFFFF) /**< Extended caps, in dwords. */

/* ── Operational registers, at CAPLENGTH from the mapped BAR ────────── */

#define XHCI_OP_USBCMD    0x00
#define XHCI_OP_USBSTS    0x04
#define XHCI_OP_PAGESIZE  0x08
#define XHCI_OP_CRCR      0x18  /**< 64-bit; the upper half is at +0x04.  */
#define XHCI_OP_DCBAAP    0x30  /**< 64-bit; the upper half is at +0x04.  */
#define XHCI_OP_CONFIG    0x38

#define XHCI_CMD_RS       0x00000001  /**< Run/Stop.                      */
#define XHCI_CMD_HCRST    0x00000002  /**< Host controller reset.         */
#define XHCI_CMD_INTE     0x00000004  /**< Interrupter enable.            */

#define XHCI_STS_HCH      0x00000001  /**< Halted.                        */
#define XHCI_STS_HCE      0x00001000  /**< Host controller error.         */
#define XHCI_STS_CNR      0x00000800  /**< Controller not ready.          */

/** @brief PAGESIZE bit 0: the controller supports 4 KB pages. */
#define XHCI_PAGESIZE_4K  0x00000001

/** @brief CRCR bit 0: the ring cycle state the driver is producing with. */
#define XHCI_CRCR_RCS     0x00000001

/** @brief Port register blocks start here, relative to the operational base. */
#define XHCI_PORT_BASE    0x400
#define XHCI_PORT_STRIDE  0x10

/* PORTSC, the only port register this driver reads. */
#define XHCI_PORTSC       0x00

#define XHCI_PORTSC_CCS   0x00000001  /**< Current connect status.        */
#define XHCI_PORTSC_PED   0x00000002  /**< Port enabled. Write 1 disables.*/
#define XHCI_PORTSC_PR    0x00000010  /**< Port reset.                    */
#define XHCI_PORTSC_PP    0x00000200  /**< Port power.                    */
#define XHCI_PORTSC_CSC   0x00020000  /**< Connect status change; W1C.    */
#define XHCI_PORTSC_PRC   0x00200000  /**< Port reset change; W1C.        */
#define XHCI_PORTSC_SPEED(v) (((v) >> 10) & 0x0F)

/**
 * @brief The wait budget for a port reset, in timer ticks.
 *
 * Separate from XHCI_TIMEOUT_TICKS and shorter, because a reset that is going to
 * happen happens in tens of milliseconds and a port with nothing usable behind
 * it will simply never raise PRC. Enumeration walks every connected port, so a
 * dead one must not spend the whole general budget before the next is tried.
 */
#define XHCI_RESET_TICKS 20

/* The speeds the default speed table assigns, named where the driver has to
 * branch on them rather than only print them. Endpoint zero's maximum packet
 * size is decided by this and by nothing else until the device descriptor has
 * been read - which cannot be read without it, which is why the first read is a
 * short one. */
#define XHCI_SPEED_FULL   1
#define XHCI_SPEED_LOW    2
#define XHCI_SPEED_HIGH   3
#define XHCI_SPEED_SUPER  4
#define XHCI_SPEED_SUPER_PLUS 5

/**
 * @brief The PORTSC bits a read-modify-write must not carry back in.
 *
 * Bits 17 to 23 are the change flags and they are write-one-to-clear, so writing
 * back what was read acknowledges every event that had been recorded. Bit 1 is
 * worse than that: PED is write-one-to-*disable*, so the obvious
 * read-modify-write to set port power turns off every port that was working.
 *
 * Every write to PORTSC in this file goes through this mask. It is the register
 * in the whole controller that punishes the ordinary idiom.
 */
#define XHCI_PORTSC_PRESERVE (~(uint32_t)(0x00FE0000 | XHCI_PORTSC_PED))

/* ── Runtime registers, at RTSOFF from the mapped BAR ───────────────── */

/** @brief Interrupter register sets begin here; each is 32 bytes. */
#define XHCI_RT_IR0       0x20

#define XHCI_IR_IMAN      0x00
#define XHCI_IR_ERSTSZ    0x08
#define XHCI_IR_ERSTBA    0x10  /**< 64-bit; the upper half is at +0x04.  */
#define XHCI_IR_ERDP      0x18  /**< 64-bit; the upper half is at +0x04.  */

/** @brief ERDP bit 3: event handler busy, write-one-to-clear. */
#define XHCI_ERDP_EHB     0x00000008

/* ── Transfer request blocks ────────────────────────────────────────── */

/** @brief TRBs per ring segment: 4096 bytes divided by 16. */
#define XHCI_TRBS_PER_SEGMENT 256

/* The TRB types this driver produces or consumes. */
#define XHCI_TRB_NORMAL        1
#define XHCI_TRB_SETUP_STAGE   2
#define XHCI_TRB_DATA_STAGE    3
#define XHCI_TRB_STATUS_STAGE  4
#define XHCI_TRB_LINK          6
#define XHCI_TRB_ENABLE_SLOT   9
#define XHCI_TRB_ADDRESS_DEV  11
#define XHCI_TRB_CONFIG_EP    12
#define XHCI_TRB_EVAL_CTX     13
#define XHCI_TRB_NOOP_CMD     23
#define XHCI_TRB_TRANSFER_EV  32
#define XHCI_TRB_CMD_COMPLETE 33
#define XHCI_TRB_PORT_STATUS  34

#define XHCI_TRB_TYPE(control) (((control) >> 10) & 0x3F)
#define XHCI_TRB_SET_TYPE(t)   ((uint32_t)(t) << 10)

#define XHCI_TRB_CYCLE 0x00000001  /**< Bit 0 of the control field.       */
#define XHCI_TRB_TC    0x00000002  /**< Link TRB: toggle cycle.           */
#define XHCI_TRB_IOC   0x00000020  /**< Interrupt on completion; bit 5.   */
#define XHCI_TRB_IDT   0x00000040  /**< Immediate data; bit 6.            */
#define XHCI_TRB_DIR_IN 0x00010000 /**< Data/Status stage: device to host.*/

/* Setup Stage's transfer type, bits 17:16. "No data" is not the same as "in
 * with zero length", and getting it wrong makes the controller wait for a data
 * stage that is never queued. */
#define XHCI_TRT_NO_DATA  0x00000000
#define XHCI_TRT_OUT      0x00020000
#define XHCI_TRT_IN       0x00030000

/** @brief The slot a Command Completion Event is about, in its control field. */
#define XHCI_TRB_SLOT(control) (((control) >> 24) & 0xFF)

/**
 * @brief The endpoint a Transfer Event is about, as its device context index.
 *
 * Bits 20:16 of the control field. With one endpoint per device this was not
 * needed - anything that completed was the keyboard's. With a keyboard and a
 * disk on the same bus it is the only thing that says which of them finished.
 */
#define XHCI_TRB_EP_ID(control) (((control) >> 16) & 0x1F)

/** @brief Puts a slot id into a command TRB's control field. */
#define XHCI_TRB_SET_SLOT(s)   ((uint32_t)(s) << 24)

/**
 * @brief Bytes a transfer did not move, out of a Transfer Event's status.
 *
 * The field is the *residual*, not the length: a control read of 18 bytes that
 * moved all 18 reports zero here. Reading it as a length is the mistake it is
 * shaped to invite, and the one place this driver needs it is deciding whether a
 * descriptor arrived whole.
 */
#define XHCI_TRB_RESIDUAL(status) ((status) & 0x00FFFFFF)

/** @brief The completion code a command that worked reports. */
#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_TRB_COMPLETION(status) (((status) >> 24) & 0xFF)

/**
 * @brief One transfer request block.
 *
 * Exactly 16 bytes, and the test module asserts it: the controller indexes a
 * ring itself, so a field the compiler padded would move every entry after the
 * first and the hardware would read half of one TRB and half of the next. The
 * same argument ahci_cmd_header_t carries.
 */
typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

/**
 * @brief One entry of the event ring segment table.
 *
 * Also exactly 16 bytes, and also indexed by the hardware. There is one of these
 * in this driver, because there is one segment.
 */
typedef struct {
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size;      /**< TRBs in the segment, in the low 16 bits. */
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

/* ── Device and input contexts ──────────────────────────────────────── */

/**
 * @brief One context entry: a slot context, or an endpoint context.
 *
 * Eight doublewords, and that is the whole of the structure. What it is not is
 * the distance between two of them - see xhci_context_stride().
 */
typedef struct {
    uint32_t dword[8];
} __attribute__((packed)) xhci_context_t;

/**
 * @brief Entries in a device context: the slot, then endpoints 1 through 31.
 *
 * Endpoint zero is entry 1, because entry 0 is the slot context. The two halves
 * of an endpoint - in and out - share an entry only for endpoint zero, which is
 * bidirectional by definition; every other endpoint number has two.
 */
#define XHCI_DEV_CTX_ENTRIES   32

/** @brief Entries in an input context: an input control context, then the above. */
#define XHCI_IN_CTX_ENTRIES    33

/* Slot context, dword 0. */
#define XHCI_SLOT_SPEED(s)        ((uint32_t)(s) << 20)
#define XHCI_SLOT_CTX_ENTRIES(n)  ((uint32_t)(n) << 27)

/* Slot context, dword 1: which root hub port the device is on, numbered from 1. */
#define XHCI_SLOT_ROOT_PORT(p)    ((uint32_t)(p) << 16)

/* What a USB mass storage device says it is, at the interface: Mass Storage,
 * SCSI transparent command set, Bulk-Only Transport. All three, because the
 * transport is what this driver implements and a device offering a different one
 * is not something it can talk to. */
#define USB_CLASS_STORAGE         0x08
#define USB_SUBCLASS_SCSI         0x06
#define USB_PROTOCOL_BULK_ONLY    0x50

#define USB_EP_XFER_BULK          0x02

/* Endpoint context, dword 1. */
#define XHCI_EP_CERR(n)           ((uint32_t)(n) << 1)
#define XHCI_EP_TYPE(t)           ((uint32_t)(t) << 3)
#define XHCI_EP_MAX_PACKET(n)     ((uint32_t)(n) << 16)

#define XHCI_EP_TYPE_CONTROL      4
#define XHCI_EP_TYPE_BULK_OUT     2
#define XHCI_EP_TYPE_BULK_IN      6
#define XHCI_EP_TYPE_INT_IN       7

/** @brief Endpoint context dwords 2-3: the ring, with its cycle state in bit 0. */
#define XHCI_EP_DCS               0x00000001

/* Input control context, dword 1: which entries the command should act on. A0
 * is the slot context and A1 is endpoint zero, and Address Device wants both. */
#define XHCI_IN_ADD_SLOT          0x00000001
#define XHCI_IN_ADD_EP0           0x00000002

/**
 * @brief Endpoint zero's maximum packet size, before the device has said.
 *
 * It is decided by the link speed for every speed but one. A full-speed device
 * may use 8, 16, 32 or 64 and the only way to find out is to read the first
 * eight bytes of its device descriptor - which fits in one packet whatever the
 * answer turns out to be. So endpoint zero is opened at 8, the descriptor's
 * bMaxPacketSize0 is read, and the endpoint is re-evaluated if it disagrees.
 *
 * "QEMU's keyboard and mouse are full-speed devices that answer 8, so the
 * re-evaluation never runs here" is what this said until v1.11.0, and it stopped
 * being true the moment a USB stick was attached to the test bench: QEMU gives it
 * a SuperSpeed port. At SuperSpeed bMaxPacketSize0 is not a byte count but its
 * base-2 logarithm, so a device using 512-byte packets answers 9 - and a driver
 * that re-evaluates on any speed but full opens endpoint zero at nine bytes.
 * xhci_ep0_size_reported() is where that is decided now.
 */
#define XHCI_EP0_MPS_DEFAULT      8
#define XHCI_EP0_MPS_HIGH        64
#define XHCI_EP0_MPS_SUPER      512

/* ── USB, above the controller ──────────────────────────────────────── */

/* Setup packet: direction, type and recipient in one byte. Only the two forms
 * this release sends are named. */
#define USB_DIR_IN                0x80
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_CONFIGURATION 0x09

/* HID class request, and the request type that carries it: host to device,
 * class, recipient interface. SET_PROTOCOL with a value of zero is what puts a
 * keyboard into boot protocol, which is the fixed eight-byte report this kernel
 * reads and the reason it does not parse a report descriptor. */
#define USB_REQ_SET_PROTOCOL      0x0B
#define USB_REQTYPE_CLASS_IFACE   0x21
#define USB_PROTOCOL_BOOT         0

/* What a HID boot keyboard says it is, at the interface. A device that answers
 * these three is one whose reports have a shape this kernel can rely on. */
#define USB_CLASS_HID             0x03
#define USB_SUBCLASS_BOOT         0x01
#define USB_PROTOCOL_KEYBOARD     0x01

/* Endpoint descriptor: the direction bit in bEndpointAddress, and the transfer
 * type in the low two bits of bmAttributes. */
#define USB_EP_DIR_IN             0x80
#define USB_EP_NUMBER(addr)       ((addr) & 0x0F)
#define USB_EP_TYPE_MASK          0x03
#define USB_EP_XFER_INTERRUPT     0x03

/*
 * There is no SET_ADDRESS here and that is not an omission. On this controller
 * the Address Device command performs it; a driver that also sent the standard
 * request would be addressing the device twice.
 */

#define USB_DESC_DEVICE           1
#define USB_DESC_CONFIG           2
#define USB_DESC_INTERFACE        4
#define USB_DESC_ENDPOINT         5

/** @brief Bytes in a device descriptor, and in a configuration descriptor's head. */
#define USB_DEVICE_DESC_LEN       18
#define USB_CONFIG_DESC_LEN        9

/**
 * @brief The most descriptor bytes this driver will read for one device.
 *
 * A configuration descriptor is followed by its interfaces and endpoints, and
 * the total is whatever wTotalLength says - a number that comes from the device.
 * It is clamped to this rather than believed.
 *
 * Written as 4096 rather than as PAGE_SIZE because this header includes only
 * types.h and a macro defined in terms of one the header does not guarantee is a
 * dependency nobody declared. The test module asserts the two are equal, which
 * is the coupling made visible instead of assumed.
 */
#define XHCI_DESC_BUF_LEN         4096

/**
 * @brief What the walk over a configuration descriptor found.
 *
 * Deliberately small. This release reports what is attached; choosing an
 * interface and an endpoint to talk to is the keyboard's problem, one release
 * later.
 */
typedef struct {
    uint16_t total_length;   /**< wTotalLength, as the device reported it.   */
    uint8_t  interfaces;     /**< Interface descriptors seen.                */
    uint8_t  endpoints;      /**< Endpoint descriptors seen.                 */
    uint8_t  iface_class;    /**< The first interface's class...             */
    uint8_t  iface_subclass; /**< ...subclass...                             */
    uint8_t  iface_protocol; /**< ...and protocol.                           */

    /**
     * @brief Which configuration to select, from bConfigurationValue.
     *
     * Read rather than assumed to be 1. It usually is, and "usually" is the word
     * that makes a driver work on the device it was written against.
     */
    uint8_t  config_value;

    /*
     * The boot keyboard, if this device has one. Found during the same walk
     * rather than by a second pass: an endpoint descriptor belongs to the
     * interface most recently seen above it, which is a fact about the order the
     * bytes arrive in and is lost the moment the walk ends.
     */
    uint8_t  kbd_iface;      /**< Interface number, or XHCI_NO_IFACE.        */
    uint8_t  kbd_ep_addr;    /**< bEndpointAddress, 0 when there is none.    */
    uint8_t  kbd_interval;   /**< bInterval, in the units its speed uses.    */
    uint16_t kbd_mps;        /**< wMaxPacketSize.                            */

    /*
     * And the same for a Bulk-Only mass storage interface. Two endpoints rather
     * than one, because bulk endpoints are unidirectional and a transport that
     * sends a command and reads a reply needs both halves.
     */
    uint8_t  msc_iface;      /**< Interface number, or XHCI_NO_IFACE.        */
    uint8_t  msc_in_addr;    /**< Bulk IN endpoint, 0 when there is none.    */
    uint8_t  msc_out_addr;   /**< Bulk OUT endpoint, 0 when there is none.   */
    uint16_t msc_in_mps;
    uint16_t msc_out_mps;
} xhci_config_info_t;

/** @brief kbd_iface when the device has no boot keyboard interface. */
#define XHCI_NO_IFACE 0xFF

/**
 * @brief Where a keyboard's reports land inside the device's descriptor page.
 *
 * The same frame, at a different offset, because the two are never wanted at the
 * same time: descriptors are read during enumeration and reports only once the
 * endpoint is open. Reusing the page saves a frame per device; the offset rather
 * than offset zero is so that the reuse is visible in a hex dump instead of
 * being two things that happen to alias.
 */
#define XHCI_OFF_REPORT 0x800

/**
 * @brief The most events one poll will drain before returning.
 *
 * A bound, because xhci_poll() runs in the timer interrupt. A controller
 * producing events faster than they are consumed - through a fault or through a
 * device that will not stop - must not be able to hold the interrupt handler
 * forever; whatever is left waits for the next tick, a hundredth of a second
 * later. Every unbounded loop this driver has is on the boot path where a
 * deadline can be enforced, and this is not that path.
 */
#define XHCI_POLL_MAX_EVENTS 16

/**
 * @brief One device this driver addressed and asked about itself.
 */
typedef struct {
    uint8_t  port;           /**< Root hub port, numbered from 1.            */
    uint8_t  slot;           /**< Slot the controller assigned.              */
    uint8_t  speed;          /**< PORTSC speed id, valid after the reset.    */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  dev_class;      /**< 0 means "look at the interface instead".   */
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    xhci_config_info_t config;
} xhci_device_t;

/*
 * Where each structure sits inside the single frame this driver owns for them.
 *
 * The device context array needs 64-byte alignment and (slots + 1) * 8 bytes,
 * which is 264 at XHCI_MAX_SLOTS. The segment table needs 64-byte alignment and
 * 16 bytes. The scratchpad array needs 64-byte alignment and 8 bytes per buffer,
 * which is 512 at XHCI_MAX_SCRATCHPAD. Everything is a multiple of 64 from the
 * start of a 4096-byte frame, so every alignment is satisfied by arithmetic
 * rather than by hope, and 3584 bytes of a 4096-byte frame is the whole of it.
 *
 * This is the arithmetic that is the reason mm/pmm.c is untouched by this
 * release. The rings are the other half: a segment is 4096 bytes and a frame is
 * 4096 bytes.
 */
#define XHCI_OFF_DCBAA      0x000  /**< 64 B aligned, as DCBAAP requires.  */
#define XHCI_OFF_ERST       0x800  /**< 64 B aligned, as ERSTBA requires.  */
#define XHCI_OFF_SCRATCHPAD 0xC00  /**< 64 B aligned, as DCBAA[0] requires.*/

/* ── Extended capabilities ──────────────────────────────────────────── */

/** @brief Extended capability ID 1: USB Legacy Support. */
#define XHCI_ECAP_LEGACY 1

#define XHCI_LEGSUP_BIOS_OWNED 0x00010000  /**< Bit 16.                   */
#define XHCI_LEGSUP_OS_OWNED   0x01000000  /**< Bit 24.                   */

/**
 * @brief The most extended capabilities the chain walk will follow.
 *
 * A bounded walk for the reason pci.c's bus worklist is bounded: nothing in QEMU
 * builds a cycle, and a broken or hostile controller on real hardware can point
 * a capability at itself. The bound turns an unbounded hang into a logged stop.
 */
#define XHCI_ECAP_MAX 64

/* ── What the rest of the kernel can ask ────────────────────────────── */

/**
 * @brief Finds an xHCI controller, resets it, and brings its rings up.
 *
 * Called from the boot path and gated on nothing. A machine without an xHCI
 * controller gets one log line and no change of behaviour; a controller that
 * will not start is reported and left alone. Nothing above this depends on the
 * answer, which is deliberate - the v1.2.0 lesson is that two defensible
 * decisions with an unwatched link between them is how a working disk
 * disappears, and this release adds no such link.
 *
 * @return E_OK when the controller is running and its command ring answered,
 *         E_NODEV when there is no xHCI controller on the bus, or a negative
 *         errno for one that would not come up.
 */
int xhci_init(void);

/** @brief Ports the controller reported, capped at XHCI_MAX_PORTS. 0 if none. */
int xhci_port_count(void);

/** @brief Devices addressed and described at boot, capped at XHCI_MAX_DEVICES. */
int xhci_device_count(void);

/**
 * @brief The device at @p index.
 *
 * @param index 0 to xhci_device_count() - 1.
 * @return The entry, or 0 when @p index addresses nothing.
 */
const xhci_device_t *xhci_get_device(int index);

/**
 * @brief The distance between two context entries, in bytes.
 *
 * 32 or 64, and it comes out of HCCPARAMS1's CSZ bit rather than out of
 * sizeof(xhci_context_t). Those two are the same number on every machine this
 * project can run on and different on plenty it cannot, which is exactly the
 * shape of the defect ata_identify() hid for thirty-six releases: correct on the
 * machine it was written against and wrong on the machine it was aimed at.
 *
 * A driver that indexed contexts by sizeof() would read every one of them from
 * the wrong offset on a controller with 64-byte contexts, and QEMU's has 32-byte
 * ones, so nothing here would ever say so.
 *
 * @return 32 or 64; 0 before xhci_init() has read the register.
 */
uint32_t xhci_context_stride(void);

/**
 * @brief Walks a configuration descriptor and reports what is in it.
 *
 * Separated from the transfer that fetches it so that it can be driven with a
 * buffer of one's choosing - the same reason keyboard_handle_scancode() is
 * separate from the port read above it. Everything it walks came from the
 * device, including every length it steps by.
 *
 * @param buf The bytes as they arrived.
 * @param len How many of them there are.
 * @param out Filled in on success; untouched on failure.
 * @return E_OK, or E_INVAL for a buffer too short to hold a configuration
 *         descriptor, one whose first descriptor is not a configuration, or one
 *         carrying a zero length - which is not a malformed value to skip past
 *         but a walk that would never end.
 */
int xhci_parse_config(const uint8_t *buf, uint32_t len, xhci_config_info_t *out);

/** @brief Non-zero once the controller is out of reset and not halted. */
int xhci_running(void);

/**
 * @brief Drains whatever the controller has finished, and queues the next read.
 *
 * Called from the timer interrupt, a hundred times a second, and that placement
 * is the load-bearing decision in this release rather than a detail.
 *
 * It runs in interrupt context, so it blocks on nothing: there is no deadline
 * loop and no call to xhci_wait() anywhere beneath it. It looks at the event
 * ring, handles what is there, re-arms the transfer that produced it, and
 * returns - and when the ring is empty it returns immediately, which is what it
 * does on ninety-nine ticks out of a hundred.
 *
 * It does nothing at all until xhci_init() has finished. During enumeration the
 * boot path owns the event ring and waits on it with interrupts enabled, so a
 * tick that helpfully drained the ring would consume the very completion the
 * boot path was waiting for - and the failure would look like a controller that
 * stopped working when a line was added to the timer, with nothing pointing at
 * the timer. One owner at a time, in both periods.
 */
void xhci_poll(void);

/**
 * @brief What endpoint zero's packet size should become, given what a device said.
 *
 * Split out of the enumeration so that it can be asserted without a device, which
 * is the only way the interesting cases are reachable: this project has never met
 * a full-speed device that answers anything but 8, and until v1.11.0 it had never
 * enumerated a SuperSpeed one at all. Both of those are ordinary on the machine
 * this is aimed at, and one of them cost a release.
 *
 * @param speed The link speed, as PORTSC reported it.
 * @param reported bMaxPacketSize0, straight out of the device descriptor.
 * @return The size to program, or 0 to keep what the speed already fixed - which
 *         is the answer at every speed but full, and for a full-speed device that
 *         names a size no device may use.
 */
uint32_t xhci_ep0_size_reported(uint8_t speed, uint8_t reported);

/* ── What a device driver above this one may ask ────────────────────── */

/**
 * @brief The next enumerated Bulk-Only mass storage device after @p after.
 *
 * This returned only the first one until v1.11.0, the way the SATA driver takes
 * the first port with a disk on it. That was a fair reading of "there is a stick"
 * and the wrong one for the machine this release is aimed at, which has two: the
 * one the system booted from and the one it is meant to use. Handing back
 * whichever enumerated first means the answer depends on which port a person
 * happened to choose, and half the time it is the wrong disk.
 *
 * A cursor rather than a count and an index, because a count and an index are two
 * numbers that can disagree about the same table. Walk it with -1 and stop at -1.
 *
 * @param after The last index returned, or -1 to start.
 * @return The next storage device's index, or -1 when there are no more.
 */
int xhci_storage_device_next(int after);

/**
 * @brief Selects the configuration and opens the device's two bulk endpoints.
 *
 * Per device throughout: the rings and the contexts live in devres[index], so
 * two sticks opened one after the other do not share anything but the
 * controller.
 *
 * @param index A device index from xhci_storage_device_next().
 * @return E_OK, or a negative errno.
 */
int xhci_open_storage(int index);

/**
 * @brief One bulk transfer, and it does not return until the device answers.
 *
 * Synchronous, because the block layer above is: blockdev_read() may not return
 * before the sector has arrived. That sits awkwardly next to an event ring whose
 * only reader runs in the timer interrupt, and the way the two are reconciled is
 * the design decision of this release - the wait calls xhci_poll() itself rather
 * than waiting for a tick to call it. Waiting for the tick would also work and
 * would cost ten milliseconds a transfer; a sector is three transfers, so a
 * format would take hours.
 *
 * Must not be called from interrupt context. That is the rule bcache.c already
 * states about block device handlers - "called from ordinary kernel context and
 * nowhere else" - and this is the driver that makes it load-bearing.
 *
 * @param index Device index.
 * @param in Non-zero to read from the device, zero to write to it.
 * @param phys Physical address of the buffer; must be DMA-reachable.
 * @param len Bytes to move.
 * @param residual Receives the bytes not moved; may be 0.
 * @return E_OK, E_IO for a failed or timed-out transfer, E_INVAL for a device
 *         with no bulk endpoints open.
 */
int xhci_bulk_transfer(int index, int in, uint32_t phys, uint32_t len,
                       uint32_t *residual);

/**
 * @brief One frame of DMA-reachable memory, mapped uncached.
 *
 * Exposed so that a driver above this one does not have to repeat the
 * pmm_alloc_frame() and vmm_map_device() pair, and more importantly so that it
 * cannot get the caching flags wrong: a buffer a device reads that the CPU left
 * in a dirty line is a buffer the device never sees.
 *
 * @param phys_out Receives the physical address to hand the controller.
 * @return The mapped address, or 0.
 */
uint8_t *xhci_dma_page(uint32_t *phys_out);

/**
 * @brief Renders the controller and its ports as text, one port per line.
 *
 * Used by SYSCALL_USBINFO, in the division of labour v0.9.2 established and
 * v1.4.0 reused: the kernel renders and the program writes, so `lsusb > file`
 * produces a file with something in it.
 *
 * @param out Destination buffer.
 * @param cap Its capacity, including room for the terminator.
 * @return Bytes written, not counting the terminator.
 */
int xhci_format_inventory(char *out, int cap);

#endif // XHCI_H
