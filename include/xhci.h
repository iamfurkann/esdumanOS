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
 * It is deliberately the smallest driver that brings the bus up and can prove
 * it did: the controller is reset, the device context array and both rings are
 * installed, the ports are powered, and a No Op command is issued and its
 * completion read back off the event ring. Nothing here talks to a device.
 * Enable Slot, Address Device and the control transfers that fetch a descriptor
 * are the next release's, because the first thing that needs them is a keyboard.
 *
 * Four absences, each argued rather than pending.
 *
 * It polls, and it polls inline. The same reason ahci.h gives: the interrupt
 * this controller can raise travels a PCI line to an IOAPIC, and this kernel has
 * no IOAPIC, no ACPI to describe the routing, and an interrupt dispatcher that
 * is a hand-written chain of comparisons in isr.c. The roadmap said the event
 * ring would be polled from the timer tick; nothing in this release needs to be
 * serviced while something else runs, so that hook would have no caller. It
 * arrives with the interrupt endpoint that needs it.
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
 * Nothing in this release uses a slot - no device is addressed - so this only
 * sizes the device context array. It is written now rather than left at zero
 * because CONFIG.MaxSlotsEn is programmed before the controller runs and a
 * controller told it has no slots is one the next release would have to reset to
 * change its mind about.
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
 * @brief Buffer the rendered inventory needs, terminator included.
 *
 * Derived from the port table rather than written out, so raising XHCI_MAX_PORTS
 * cannot leave the rendering silently truncated at the old number of lines. The
 * 128 covers the controller's own line and the truncation notice; 48 bytes
 * covers the longest port line this file can produce.
 */
#define USBINFO_BUF (XHCI_MAX_PORTS * 48 + 128)

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
#define XHCI_PORTSC_SPEED(v) (((v) >> 10) & 0x0F)

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
#define XHCI_TRB_LINK          6
#define XHCI_TRB_NOOP_CMD     23
#define XHCI_TRB_CMD_COMPLETE 33
#define XHCI_TRB_PORT_STATUS  34

#define XHCI_TRB_TYPE(control) (((control) >> 10) & 0x3F)
#define XHCI_TRB_SET_TYPE(t)   ((uint32_t)(t) << 10)

#define XHCI_TRB_CYCLE 0x00000001  /**< Bit 0 of the control field.       */
#define XHCI_TRB_TC    0x00000002  /**< Link TRB: toggle cycle.           */

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

/** @brief Non-zero once the controller is out of reset and not halted. */
int xhci_running(void);

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
