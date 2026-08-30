/*
 * File: xhci.c
 * Purpose: The USB controller a machine built this decade actually has.
 *
 * This file is part of the esdumanOS test suite.
 *
 * v1.4.0 taught the kernel to ask the bus what it had; v1.5.0 answered the
 * storage half of what came back. This answers the other half, as far as the
 * bus itself: the controller is found, taken from the firmware, reset,
 * programmed, started, and then asked to do the smallest thing it can do so that
 * "it is running" is a measurement rather than an assumption.
 *
 * That smallest thing is a No Op command. It is worth being explicit about why
 * it is here, because it moves no data and looks like ceremony: a controller
 * that has been reset and started will report itself as running with its rings
 * pointed anywhere at all. The command list, the doorbell, the event ring and
 * the cycle-state agreement between driver and hardware are only proven by
 * putting a TRB on one ring and reading its completion off the other. This
 * release ships one command in the controller's whole lifetime, and it is that
 * one.
 *
 * The rule ahci.c keeps is kept here: no wait in this file is unbounded. There
 * are three, and all three carry the same deadline. Two go through xhci_wait().
 * The third is the event wait in xhci_noop_command(), which cannot use it - it
 * is watching a ring for a record with a matching cycle bit rather than a
 * register for a value - and carries its deadline explicitly.
 *
 * What is deliberately absent is argued in xhci.h: no interrupts, no timer-tick
 * polling, no second interrupter, no contiguous multi-frame allocation, and no
 * conversation with any device.
 */
#include "xhci.h"
#include "pci.h"
#include "paging.h"
#include "pmm.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"
#include "rtc.h"
#include "stdio.h"

/** Mapped BAR0. Volatile: every read of it is a question to the hardware. */
static volatile uint8_t *mmio = 0;

/* Offsets of the three register blocks from the mapped base, read out of the
 * capability block at bring-up rather than assumed. Only CAPLENGTH is fixed by
 * the specification; the other two are wherever this controller put them. */
static uint32_t op_base = 0;
static uint32_t rt_base = 0;
static uint32_t db_base = 0;

/** The function on the bus, kept so the inventory can name where it lives. */
static const pci_device_t *xhci_dev = 0;

static uint32_t xhci_version = 0;
static int xhci_ports = 0;
static int xhci_slots = 0;
static int xhci_truncated = 0;
static int xhci_scratchpads = 0;

/**
 * @brief Set only when bring-up finished, including the command that proved it.
 *
 * Separate from the halt bit, because the two answer different questions and
 * only one of them is readable from a register. A controller can be out of reset
 * and running with rings this driver never finished installing - that is exactly
 * the state a failure halfway through xhci_init() leaves - and USBSTS would call
 * that running. Anything asking whether this driver has a working controller has
 * to be told about both.
 */
static int xhci_ready = 0;

/** One frame holding the device context array, the segment table and the
 *  scratchpad array; see the offsets in xhci.h. */
static uint8_t  *dma_virt = 0;
static uint32_t  dma_phys = 0;

/*
 * Both rings are volatile, and that is not decoration.
 *
 * The event ring is polled in a loop that writes nothing the compiler can see
 * changing it, so a plain pointer would let the load of the control field be
 * hoisted out of the loop entirely - the driver would read the ring once, decide
 * it was empty, and spin until the deadline no matter what the controller wrote.
 * The mapping being uncached does not help with that: it stops the CPU caching
 * the line and says nothing about whether the compiler emits the load at all.
 *
 * The command ring is volatile for the ordering rather than the reload. The
 * doorbell is a volatile store, and the guarantee that the TRB is in memory
 * before it is rung only holds between accesses the compiler must not reorder
 * against each other - which is what volatile buys and what a plain store into a
 * static buffer does not. ahci.c reaches the same place by doing every register
 * access through a volatile pointer.
 */
static volatile xhci_trb_t *cmd_ring = 0;
static uint32_t             cmd_ring_phys = 0;

static volatile xhci_trb_t *event_ring = 0;
static uint32_t             event_ring_phys = 0;

/* Where the driver is in the event ring, and which cycle bit marks a record the
 * hardware has written since the last pass. Both start where the controller
 * expects them to after a reset: the ring is empty and the first valid record
 * will carry a one. */
static uint32_t event_dequeue = 0;
static uint32_t event_ccs = 1;

/** What the ports reported, recorded once at bring-up. */
typedef struct {
    uint8_t connected;
    uint8_t enabled;
    uint8_t speed;
} xhci_port_state_t;

static xhci_port_state_t port_state[XHCI_MAX_PORTS];

/* ── Register access ────────────────────────────────────────────────── */

static uint32_t mmio_read(uint32_t off) {
    return *(volatile uint32_t *)(mmio + off);
}

static void mmio_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(mmio + off) = value;
}

static uint32_t op_read(uint32_t reg)  { return mmio_read(op_base + reg); }
static void op_write(uint32_t reg, uint32_t v) { mmio_write(op_base + reg, v); }

/* Write only, and there is no rt_read() beside it. Nothing in this release reads
 * a runtime register: the interrupter is programmed once and never consulted,
 * because the thing that would consult it is the interrupt this driver does not
 * take. A reader written now would be a function with no caller. */
static void rt_write(uint32_t reg, uint32_t v) { mmio_write(rt_base + reg, v); }

/** @brief Absolute offset of a port's PORTSC. Ports are numbered from one. */
static uint32_t portsc_off(int port) {
    return op_base + XHCI_PORT_BASE + ((uint32_t)port * XHCI_PORT_STRIDE) + XHCI_PORTSC;
}

/**
 * @brief Waits for a register's bits to reach a value, or gives up.
 *
 * @param off Absolute register offset from the mapped base.
 * @param mask Bits to look at.
 * @param want The value those bits must reach.
 * @param what Logged if they never do.
 * @return E_OK, or E_IO on timeout.
 */
static int xhci_wait(uint32_t off, uint32_t mask, uint32_t want, const char *what) {
    uint32_t start = timer_get_ticks();

    while ((mmio_read(off) & mask) != want) {
        if ((timer_get_ticks() - start) > XHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "XHCI", what);
            return E_IO;
        }
        asm volatile("pause");
    }
    return E_OK;
}

/* ── Memory the controller reads ────────────────────────────────────── */

/**
 * @brief Takes one frame and maps it where the CPU can reach it uncached.
 *
 * Uncached is not a preference. A structure the controller polls that the CPU
 * left in a dirty cache line is one the controller never sees; a status the
 * controller wrote that the CPU reads out of cache is one that never changes.
 * vmm_map_device() is the mapping with the cache-disable bit in it, which is why
 * device memory goes through the same call the register window does.
 *
 * @param phys_out Receives the physical address the hardware will be given.
 * @return The mapped address, or 0.
 */
static uint8_t *alloc_dma_page(uint32_t *phys_out) {
    uint32_t phys = pmm_alloc_frame();

    if (phys == 0xFFFFFFFFu) return 0;

    uint8_t *virt = (uint8_t *)vmm_map_device(phys, PAGE_SIZE);

    if (virt == 0) {
        pmm_free_frame(phys);
        return 0;
    }

    ft_memset(virt, 0, PAGE_SIZE);
    *phys_out = phys;
    return virt;
}

/**
 * @brief Hands the controller the private buffers it asked for.
 *
 * The count comes out of HCSPARAMS2 and is usually zero - QEMU's controller asks
 * for none, so nothing under any test target in this tree runs this function.
 * Real controllers ask for a handful of pages they use as working storage and
 * never show to anybody.
 *
 * They do not have to be contiguous with each other, which is the case that
 * looks like it needs a contiguous allocator and does not: the array holds each
 * buffer's address on its own. Each buffer is mapped only long enough to be
 * zeroed and then unmapped, because the CPU has no further business in memory
 * the controller owns - and a live uncached mapping of a page nothing will read
 * is address space spent for nothing.
 *
 * @return E_OK, or a negative errno. A controller asking for more than
 *         XHCI_MAX_SCRATCHPAD is refused rather than half-served: an array the
 *         hardware believes is longer than it is gets read past its end.
 */
static int xhci_setup_scratchpad(uint32_t hcsparams2) {
    int count = (int)XHCI_HCS2_SPB(hcsparams2);

    if (count == 0) return E_OK;

    if (count > XHCI_MAX_SCRATCHPAD) {
        klog_int(LOG_LEVEL_ERROR, "XHCI",
                 "Controller asks for more scratchpad buffers than this driver allocates",
                 count);
        return E_NOMEM;
    }

    volatile uint32_t *array = (volatile uint32_t *)(dma_virt + XHCI_OFF_SCRATCHPAD);

    for (int i = 0; i < count; i++) {
        uint32_t phys = pmm_alloc_frame();

        if (phys == 0xFFFFFFFFu) {
            klog_int(LOG_LEVEL_ERROR, "XHCI",
                     "Out of memory allocating scratchpad buffer", i);
            return E_NOMEM;
        }

        void *scratch = vmm_map_device(phys, PAGE_SIZE);

        if (scratch == 0) {
            pmm_free_frame(phys);
            klog(LOG_LEVEL_ERROR, "XHCI", "Could not map a scratchpad buffer to clear it.");
            return E_NOMEM;
        }

        ft_memset(scratch, 0, PAGE_SIZE);
        unmap_page((uint32_t)scratch);

        /* Two 32-bit stores rather than one 64-bit one: this kernel links
         * without libgcc, so a 64-bit store the compiler decides to help with is
         * a link error rather than slow code. The upper half is zero because
         * nothing here is above 4 GB. */
        array[i * 2]     = phys;
        array[i * 2 + 1] = 0;
    }

    /* Entry zero of the device context array is the scratchpad array's address,
     * not a device context. Slot numbering starts at one for exactly this
     * reason. */
    volatile uint32_t *dcbaa = (volatile uint32_t *)(dma_virt + XHCI_OFF_DCBAA);

    dcbaa[0] = dma_phys + XHCI_OFF_SCRATCHPAD;
    dcbaa[1] = 0;

    xhci_scratchpads = count;
    klog_int(LOG_LEVEL_INFO, "XHCI", "Scratchpad buffers handed to the controller", count);
    return E_OK;
}

/* ── Taking the controller away from the firmware ───────────────────── */

/**
 * @brief Asks the firmware to let go, if it is holding on.
 *
 * On a UEFI machine the firmware has been driving this controller since power
 * on, because that is how it read the keyboard in its own boot menu. It does not
 * stop when the operating system starts; it stops when the operating system asks
 * through the USB Legacy Support capability, and until then two drivers are
 * programming the same registers.
 *
 * This is the one function in the file that cannot be tested here. QEMU's
 * controller publishes no such capability, so on every machine this project can
 * run on the walk below finds nothing and returns. That is worth saying plainly
 * rather than leaving to be discovered: it is written from the specification and
 * the first machine to execute it will be one nobody here has.
 *
 * @return E_OK when the capability is absent or ownership was taken, E_IO when
 *         the firmware held on past the deadline.
 */
static int xhci_take_ownership(uint32_t hccparams1) {
    uint32_t offset = XHCI_HCC1_XECP(hccparams1) * 4;

    if (offset == 0) return E_OK;

    /*
     * Bounded twice, and both bounds are load-bearing.
     *
     * The step count is bounded for the reason pci.c's bus walk is: nothing in
     * QEMU builds a capability that points at itself, and a broken or hostile
     * controller on real hardware can, so an unbounded chase would hang the boot.
     *
     * The offset is bounded because every value in this walk came out of the
     * controller. This runs before the register blocks have been located and
     * checked, so it is the first thing in the driver to dereference a number the
     * hardware chose - and the device window has no guard page after this
     * mapping. An offset past the end of it is a read, and then a write, into
     * whatever the window handed the next caller.
     */
    for (int step = 0; step < XHCI_ECAP_MAX && offset != 0; step++) {
        if (offset + 4 > XHCI_MMIO_WINDOW) {
            klog_hex(LOG_LEVEL_WARN, "XHCI",
                     "Extended capability chain leaves the mapped window at offset",
                     offset);
            return E_OK;
        }

        uint32_t cap = mmio_read(offset);

        if ((cap & 0xFF) == XHCI_ECAP_LEGACY) {
            if (!(cap & XHCI_LEGSUP_BIOS_OWNED)) {
                /* Nobody is holding it. Claiming it anyway is still correct and
                 * is what the specification asks for, so that a firmware which
                 * looks later sees an owner. */
                mmio_write(offset, cap | XHCI_LEGSUP_OS_OWNED);
                return E_OK;
            }

            klog(LOG_LEVEL_INFO, "XHCI", "Firmware owns the controller; asking for it.");
            mmio_write(offset, cap | XHCI_LEGSUP_OS_OWNED);

            return xhci_wait(offset, XHCI_LEGSUP_BIOS_OWNED, 0,
                             "Firmware would not release the controller.");
        }

        uint32_t next = (cap >> 8) & 0xFF;

        if (next == 0) break;
        offset += next * 4;
    }

    return E_OK;
}

/* ── The event ring ─────────────────────────────────────────────────── */

/**
 * @brief The next record the hardware has written, or 0 when there is none.
 *
 * "Written" is decided by the cycle bit rather than by a pointer the hardware
 * publishes: the controller flips bit 0 of the control field to the current
 * cycle state as it fills each entry, and the driver flips its own copy every
 * time it wraps. A record whose cycle bit does not match is one from the
 * previous lap, which is to say the ring is empty.
 */
static volatile xhci_trb_t *xhci_next_event(void) {
    volatile xhci_trb_t *trb = &event_ring[event_dequeue];

    if ((trb->control & XHCI_TRB_CYCLE) != event_ccs) return 0;
    return trb;
}

/**
 * @brief Steps past the record just read and tells the controller where we are.
 */
static void xhci_consume_event(void) {
    event_dequeue++;

    if (event_dequeue == XHCI_TRBS_PER_SEGMENT) {
        event_dequeue = 0;
        event_ccs ^= 1;
    }

    /*
     * EHB is written as a one to clear it. Leaving it set tells the controller
     * the driver is still working through the ring, and a controller that
     * believes that will stop raising the interrupt this driver is not using
     * anyway - but it also stops advancing its own view of the ring, which
     * matters even when polling.
     */
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERDP,
             (event_ring_phys + event_dequeue * (uint32_t)sizeof(xhci_trb_t))
             | XHCI_ERDP_EHB);
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERDP + 4, 0);
}

/**
 * @brief Issues the one command this release sends and waits for its answer.
 *
 * The TRB goes in slot zero and the enqueue pointer is not advanced, because
 * there is no second command: a ring index that is only ever zero is not a ring
 * index, and writing one would be writing a wrap case that nothing in this tree
 * can reach and therefore nothing can test. The Link TRB at the end of the
 * segment is still installed by the caller, because the shape of the ring is the
 * hardware's business rather than the driver's - a segment without one is
 * malformed whether or not anybody walks off its end.
 *
 * Port status change events may already be queued ahead of the completion: the
 * ports were powered a moment ago and the controller records that. They are read
 * and dropped. This release reads PORTSC directly rather than reacting to
 * events, so the record has served its purpose by being consumed - but it has to
 * be consumed, or the completion behind it is never reached.
 *
 * @return E_OK when the command completed successfully, E_IO otherwise.
 */
static int xhci_noop_command(void) {
    volatile xhci_trb_t *trb = &cmd_ring[0];

    trb->param_lo = 0;
    trb->param_hi = 0;
    trb->status   = 0;
    trb->control  = XHCI_TRB_SET_TYPE(XHCI_TRB_NOOP_CMD) | XHCI_TRB_CYCLE;

    /*
     * The TRB is written above and the doorbell is rung here, in that order, and
     * on x86 that is enough: stores retire in program order and the page the
     * controller will read is mapped with caching disabled, so there is no dirty
     * line holding the command when the controller goes looking. A weaker
     * architecture would need a barrier between these two statements. ahci.c
     * carries the same note for the same reason.
     *
     * Doorbell zero is the command ring's, and the value it takes is zero.
     */
    mmio_write(db_base, 0);

    uint32_t start = timer_get_ticks();

    for (;;) {
        volatile xhci_trb_t *ev = xhci_next_event();

        if (ev != 0) {
            uint32_t type = XHCI_TRB_TYPE(ev->control);
            uint32_t code = XHCI_TRB_COMPLETION(ev->status);

            xhci_consume_event();

            if (type == XHCI_TRB_CMD_COMPLETE) {
                if (code != XHCI_COMPLETION_SUCCESS) {
                    klog_int(LOG_LEVEL_ERROR, "XHCI",
                             "Command ring answered, with a completion code of", (int)code);
                    return E_IO;
                }
                return E_OK;
            }

            /* Something else - a port that just came up. Dropped, and the
             * deadline below is deliberately not restarted by it. */
            continue;
        }

        if ((timer_get_ticks() - start) > XHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "XHCI",
                 "No completion arrived on the event ring; the rings are not talking.");
            return E_IO;
        }
        asm volatile("pause");
    }
}

/* ── Ports ──────────────────────────────────────────────────────────── */

/**
 * @brief Raises power on every port, then records what answered.
 *
 * Every write here goes through XHCI_PORTSC_PRESERVE. PORTSC is the register on
 * this controller that punishes the ordinary read-modify-write: seven of its
 * bits are write-one-to-clear change flags, and bit 1 is write-one-to-*disable*,
 * so writing back what was read would acknowledge every event and switch off
 * every working port in the same instruction.
 */
static void xhci_bring_up_ports(void) {
    int powered = 0;

    for (int i = 0; i < xhci_ports; i++) {
        uint32_t portsc = mmio_read(portsc_off(i));

        if (!(portsc & XHCI_PORTSC_PP)) {
            mmio_write(portsc_off(i),
                       (portsc & XHCI_PORTSC_PRESERVE) | XHCI_PORTSC_PP);
            powered++;
        }
    }

    /*
     * One settle for all of them rather than one each: they were powered within
     * microseconds of one another and the specification's wait is about the
     * port, not about the loop.
     *
     * And none at all when nothing was switched on. A controller that does not
     * implement port power control reports every port as already powered - which
     * is what QEMU's does, so on every machine this project can run on the whole
     * of this wait would be thirty milliseconds of spinning on each boot to
     * settle a change nobody made.
     */
    if (powered > 0) {
        uint32_t start = timer_get_ticks();

        while ((timer_get_ticks() - start) < XHCI_PORT_SETTLE_TICKS) {
            asm volatile("pause");
        }
    }

    for (int i = 0; i < xhci_ports; i++) {
        uint32_t portsc = mmio_read(portsc_off(i));

        port_state[i].connected = (portsc & XHCI_PORTSC_CCS) ? 1 : 0;
        port_state[i].enabled   = (portsc & XHCI_PORTSC_PED) ? 1 : 0;
        port_state[i].speed     = (uint8_t)XHCI_PORTSC_SPEED(portsc);
    }
}

/**
 * @brief Names the speeds the default speed table assigns.
 *
 * Short on purpose, and for the same reason pci_class_name() is short: a
 * controller may publish a speed table of its own through an extended
 * capability, and this kernel cannot check any of the names it would find there
 * against a machine. What is named is what the default table defines, and
 * anything else gets a number rather than a blank.
 */
static const char *xhci_speed_name(uint8_t speed) {
    switch (speed) {
        case 1:  return "full-speed";
        case 2:  return "low-speed";
        case 3:  return "high-speed";
        case 4:  return "SuperSpeed";
        case 5:  return "SuperSpeed+";
        default: return "unknown speed";
    }
}

/* ── What the rest of the kernel can ask ────────────────────────────── */

/**
 * @brief Abandons the bring-up, leaving the driver's state honest about it.
 *
 * The mapping and the recorded PCI function are kept on purpose. Everything the
 * inventory needs to say "there is a controller here and it did not come up" is
 * in them, and reporting no controller at all on a machine that has one is a
 * worse answer than reporting a broken one. What is cleared is everything that
 * would be believed: the ready flag, and the port count - because a port count
 * left over from a bring-up that then failed is a number some later caller would
 * loop over.
 */
static int xhci_give_up(int err) {
    xhci_ready = 0;
    xhci_ports = 0;
    return err;
}

int xhci_port_count(void) {
    return xhci_ports;
}

int xhci_running(void) {
    /*
     * Both halves. The flag says this driver finished installing the rings and
     * got an answer back through them; the halt bit is asked of the hardware
     * every time, because a controller that stopped after bring-up is not
     * something a remembered flag would ever notice.
     */
    if (mmio == 0 || !xhci_ready) return 0;

    return (op_read(XHCI_OP_USBSTS) & XHCI_STS_HCH) ? 0 : 1;
}

int xhci_format_inventory(char *out, int cap) {
    int used = 0;

    if (out == 0 || cap <= 0) return 0;
    out[0] = '\0';

    if (mmio == 0 || xhci_dev == 0) {
        return kbprintf(out, (uint32_t)cap, 0, "No xHCI controller found.\n");
    }

    used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                    "xHCI %02x:%02x.%d %04x:%04x version %x.%02x, %d ports, %d slots%s\n",
                    xhci_dev->bus, xhci_dev->device, xhci_dev->function,
                    xhci_dev->vendor_id, xhci_dev->device_id,
                    (xhci_version >> 8) & 0xFF, xhci_version & 0xFF,
                    xhci_ports, xhci_slots,
                    xhci_running() ? "" : " [not running]");

    if (xhci_scratchpads > 0) {
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "  %d scratchpad buffers\n", xhci_scratchpads);
    }

    for (int i = 0; i < xhci_ports; i++) {
        if (!port_state[i].connected) {
            used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                            "Port %d: no device\n", i + 1);
            continue;
        }

        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "Port %d: connected, %s, %s\n", i + 1,
                        xhci_speed_name(port_state[i].speed),
                        port_state[i].enabled ? "enabled" : "not enabled");
    }

    /*
     * Said here as well as in the log, because the program that prints this is
     * where somebody counting ports will be looking. pci_format_inventory()
     * makes the same admission for the same reason.
     */
    if (xhci_truncated) {
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "(list truncated at %d ports)\n", XHCI_MAX_PORTS);
    }

    return used;
}

/* ── Bring-up ───────────────────────────────────────────────────────── */

int xhci_init(void) {
    const pci_device_t *dev = pci_find_class_if(PCI_CLASS_SERIAL_BUS,
                                                PCI_SUBCLASS_USB,
                                                PCI_PROGIF_XHCI);

    if (dev == 0) {
        klog(LOG_LEVEL_INFO, "XHCI", "No xHCI controller on the bus.");
        return E_NODEV;
    }

    pci_enable_device(dev);

    /*
     * BAR0 is the register file, and reading it means reading a pair rather than
     * a register. An xHCI controller's first BAR is 64-bit capable, so the BAR
     * after it is the upper half of one address rather than a second region -
     * and where the firmware put that address above 4 GB, the lower half holds
     * nothing but the type bits.
     *
     * Which is why the order of these three checks is the whole of them. Asking
     * "is the low half zero" first calls a controller at 0x380000000000 a
     * controller with no address, and this driver did exactly that until a UEFI
     * machine showed it doing so: OVMF has an above-4 GB aperture and puts
     * 64-bit BARs in it, while SeaBIOS has none and never could. The high half
     * has to be asked about before the low half is judged.
     *
     * The width is read rather than assumed, too. If this BAR were not 64-bit
     * capable then bar[1] would be an unrelated region, and treating it as the
     * top of this address would refuse a perfectly reachable controller on the
     * strength of a number that means something else. ahci.c does not ask
     * because BAR5 is 32-bit by specification; this one is not.
     */
    uint32_t bar0 = dev->bar[0];

    if (bar0 & PCI_BAR_IO) {
        klog_hex(LOG_LEVEL_ERROR, "XHCI",
                 "Controller decodes I/O space rather than memory; BAR0 reads", bar0);
        return E_NODEV;
    }

    int is_64bit    = ((bar0 & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64);
    uint32_t base   = bar0 & PCI_BAR_MEM_MASK;
    uint32_t base_hi = is_64bit ? dev->bar[1] : 0;

    if (base_hi != 0) {
        /*
         * A real limit of a 32-bit kernel, reported as one. There is no PAE
         * here, so a physical address is 32 bits wide and this controller is
         * not addressable at all - not slowly, not partially. Relocating the
         * BAR into the low aperture is what an operating system that cannot
         * reach it would have to do, and it needs BAR sizing and a free-range
         * search this tree deliberately does not have.
         */
        klog_hex(LOG_LEVEL_ERROR, "XHCI",
                 "Controller decodes above 4 GB, which this 32-bit kernel cannot reach; upper half is",
                 base_hi);
        return E_NODEV;
    }

    if (base == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Controller reports no register base.");
        return E_NODEV;
    }

    /*
     * 64 KB. The capability block is at the start, the operational block is
     * within 256 bytes of it, the port array runs from the operational base plus
     * 0x400, and the runtime and doorbell blocks are wherever DBOFF and RTSOFF
     * say - which for every controller in the wild is inside the first 64 KB and
     * for none of them is at a fixed offset. Mapped in one call because the
     * device window is a bump allocator and three mappings of one region would
     * spend three times the address space to describe it.
     */
    mmio = (volatile uint8_t *)vmm_map_device(base, XHCI_MMIO_WINDOW);
    if (mmio == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Could not map the controller's registers.");
        return E_NOMEM;
    }

    xhci_dev = dev;

    uint32_t hccparams1 = mmio_read(XHCI_CAP_HCCPARAMS1);

    /* Before any other register is programmed. Whatever is still driving this
     * controller has to stop first. */
    if (xhci_take_ownership(hccparams1) != E_OK) {
        return xhci_give_up(E_IO);
    }

    op_base = *(volatile uint8_t *)(mmio + XHCI_CAP_CAPLENGTH);
    rt_base = mmio_read(XHCI_CAP_RTSOFF) & ~(uint32_t)0x1F;
    db_base = mmio_read(XHCI_CAP_DBOFF)  & ~(uint32_t)0x03;
    xhci_version = *(volatile uint16_t *)(mmio + XHCI_CAP_HCIVERSION);

    /*
     * All three offsets came out of the controller's own registers, and all
     * three are used to address memory. A zero is a controller that did not
     * answer; a value past the end of the mapping is worse than that, because
     * the device window is a bump allocator with no guard pages - a write past
     * this mapping lands in whatever the next caller was given. The last port's
     * register block is the furthest this driver reaches, so that is what is
     * checked rather than the operational base alone.
     */
    uint32_t port_end = op_base + XHCI_PORT_BASE +
                        ((uint32_t)XHCI_MAX_PORTS * XHCI_PORT_STRIDE);

    if (op_base == 0 || rt_base == 0 || db_base == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Controller reports a register block at offset zero.");
        return xhci_give_up(E_NODEV);
    }

    if (port_end > XHCI_MMIO_WINDOW ||
        rt_base + XHCI_RT_IR0 + 0x20 > XHCI_MMIO_WINDOW ||
        db_base + 4 > XHCI_MMIO_WINDOW) {
        klog_hex(LOG_LEVEL_ERROR, "XHCI",
                 "Controller puts its registers past the mapped window; operational base is",
                 op_base);
        return xhci_give_up(E_NODEV);
    }

    /* The controller may be mid-reset from the firmware's last act. Nothing
     * below is legible until it says otherwise. */
    if (xhci_wait(op_base + XHCI_OP_USBSTS, XHCI_STS_CNR, 0,
                  "Controller never became ready.") != E_OK) {
        return xhci_give_up(E_IO);
    }

    /* Halt, then reset. A reset asked of a running controller is undefined. */
    op_write(XHCI_OP_USBCMD, op_read(XHCI_OP_USBCMD) & ~(uint32_t)XHCI_CMD_RS);

    if (xhci_wait(op_base + XHCI_OP_USBSTS, XHCI_STS_HCH, XHCI_STS_HCH,
                  "Controller would not halt.") != E_OK) {
        return xhci_give_up(E_IO);
    }

    op_write(XHCI_OP_USBCMD, op_read(XHCI_OP_USBCMD) | XHCI_CMD_HCRST);

    /*
     * Two conditions, and both are waited on. HCRST clearing means the reset
     * finished; CNR clearing means the controller is answering again. A driver
     * that waits only for the first reads the registers below out of a
     * controller that has not finished coming back, and gets zeroes that look
     * like answers.
     */
    if (xhci_wait(op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST, 0,
                  "Controller would not come out of reset.") != E_OK) {
        return xhci_give_up(E_IO);
    }

    if (xhci_wait(op_base + XHCI_OP_USBSTS, XHCI_STS_CNR, 0,
                  "Controller stayed unready after its reset.") != E_OK) {
        return xhci_give_up(E_IO);
    }

    /*
     * The page size the controller will index its own structures by. Bit 0 is
     * 4 KB. Everything below hands it frames, and a frame is 4 KB, so a
     * controller that cannot use them is one this driver has nothing to offer.
     */
    if (!(op_read(XHCI_OP_PAGESIZE) & XHCI_PAGESIZE_4K)) {
        klog_hex(LOG_LEVEL_ERROR, "XHCI",
                 "Controller does not accept 4 KB pages; page size register reads",
                 op_read(XHCI_OP_PAGESIZE));
        return xhci_give_up(E_NODEV);
    }

    uint32_t hcsparams1 = mmio_read(XHCI_CAP_HCSPARAMS1);
    uint32_t hcsparams2 = mmio_read(XHCI_CAP_HCSPARAMS2);
    int reported_ports  = (int)XHCI_HCS1_MAXPORTS(hcsparams1);
    int reported_slots  = (int)XHCI_HCS1_MAXSLOTS(hcsparams1);

    xhci_ports = reported_ports;
    if (xhci_ports > XHCI_MAX_PORTS) {
        xhci_ports = XHCI_MAX_PORTS;
        xhci_truncated = 1;
        klog_int(LOG_LEVEL_WARN, "XHCI",
                 "Controller has more ports than this driver records; the list stops at",
                 XHCI_MAX_PORTS);
    }

    xhci_slots = reported_slots > XHCI_MAX_SLOTS ? XHCI_MAX_SLOTS : reported_slots;

    if (xhci_ports == 0) {
        klog(LOG_LEVEL_WARN, "XHCI", "Controller reports no ports.");
        return xhci_give_up(E_NODEV);
    }

    dma_virt = alloc_dma_page(&dma_phys);
    if (dma_virt == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Out of memory allocating the context area.");
        return xhci_give_up(E_NOMEM);
    }

    /*
     * From here a failure abandons what it allocated rather than freeing it, and
     * that is deliberate - ahci.c takes the same position for the same reason.
     * These frames are mapped into the device window, the window has no way to
     * unmap and rewind, and handing a frame back to the allocator while a live
     * uncached mapping still points at it gives somebody else memory that two
     * mappings disagree about. Three leaked pages on a path that ends in a
     * machine with no working USB controller is the cheaper of the two.
     */
    cmd_ring = (volatile xhci_trb_t *)alloc_dma_page(&cmd_ring_phys);
    if (cmd_ring == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Out of memory allocating the command ring.");
        return xhci_give_up(E_NOMEM);
    }

    event_ring = (volatile xhci_trb_t *)alloc_dma_page(&event_ring_phys);
    if (event_ring == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Out of memory allocating the event ring.");
        return xhci_give_up(E_NOMEM);
    }

    if (xhci_setup_scratchpad(hcsparams2) != E_OK) {
        return xhci_give_up(E_NOMEM);
    }

    /* How many slots the controller should size its own bookkeeping for. Written
     * before it runs, because changing it afterwards means resetting it. */
    op_write(XHCI_OP_CONFIG, (uint32_t)xhci_slots);

    op_write(XHCI_OP_DCBAAP,     dma_phys + XHCI_OFF_DCBAA);
    op_write(XHCI_OP_DCBAAP + 4, 0);

    /*
     * The last TRB of the command ring is a Link back to its own start, with the
     * toggle bit set so that a walk arriving here flips its cycle state. Nothing
     * in this release reaches it - one command is issued, in slot zero - but a
     * ring segment without a link is a malformed ring whether or not anybody
     * walks off its end, and the controller is entitled to read ahead.
     */
    volatile xhci_trb_t *link = &cmd_ring[XHCI_TRBS_PER_SEGMENT - 1];

    link->param_lo = cmd_ring_phys;
    link->param_hi = 0;
    link->status   = 0;
    link->control  = XHCI_TRB_SET_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    op_write(XHCI_OP_CRCR,     cmd_ring_phys | XHCI_CRCR_RCS);
    op_write(XHCI_OP_CRCR + 4, 0);

    /*
     * The event ring, and the order of these three writes is the whole of it.
     * ERSTBA is what arms the ring: the controller reads the segment table the
     * moment it is given its address, so the table's size and the driver's
     * dequeue pointer have to already be true when that write lands. Programming
     * ERSTBA first is a controller reading a segment table that describes
     * nothing.
     */
    volatile xhci_erst_entry_t *erst =
        (volatile xhci_erst_entry_t *)(dma_virt + XHCI_OFF_ERST);

    erst->base_lo  = event_ring_phys;
    erst->base_hi  = 0;
    erst->size     = XHCI_TRBS_PER_SEGMENT;
    erst->reserved = 0;

    event_dequeue = 0;
    event_ccs = 1;

    rt_write(XHCI_RT_IR0 + XHCI_IR_ERSTSZ, 1);
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERDP,     event_ring_phys | XHCI_ERDP_EHB);
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERDP + 4, 0);
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERSTBA,     dma_phys + XHCI_OFF_ERST);
    rt_write(XHCI_RT_IR0 + XHCI_IR_ERSTBA + 4, 0);

    /* Interrupts stay masked, at both ends. This driver polls; a raised line
     * nothing services would be an interrupt storm on a shared PCI line. The
     * argument is ahci.h's, and it is the same argument. */
    rt_write(XHCI_RT_IR0 + XHCI_IR_IMAN, 0);
    op_write(XHCI_OP_USBCMD, op_read(XHCI_OP_USBCMD) & ~(uint32_t)XHCI_CMD_INTE);

    op_write(XHCI_OP_USBCMD, op_read(XHCI_OP_USBCMD) | XHCI_CMD_RS);

    if (xhci_wait(op_base + XHCI_OP_USBSTS, XHCI_STS_HCH, 0,
                  "Controller would not start.") != E_OK) {
        return xhci_give_up(E_IO);
    }

    xhci_bring_up_ports();

    /*
     * And now the part that turns "running" from an assumption into a
     * measurement. Everything above can be true of a controller whose rings
     * point at the wrong memory or whose cycle state disagrees with this
     * driver's; a command that goes out one ring and comes back the other cannot
     * be.
     */
    if (xhci_noop_command() != E_OK) {
        klog(LOG_LEVEL_ERROR, "XHCI",
             "Controller is running but its command ring did not answer.");
        return xhci_give_up(E_IO);
    }

    /*
     * Only here. Everything above can be true of a controller this driver cannot
     * actually use, so the flag that says otherwise is set after the one thing
     * that could not be true of it.
     */
    xhci_ready = 1;

    int connected = 0;

    for (int i = 0; i < xhci_ports; i++) {
        if (port_state[i].connected) connected++;
    }

    printk("[XHCI] USB controller %02x:%02x.%d: %d ports, %d connected [Polled]\n",
           dev->bus, dev->device, dev->function, xhci_ports, connected);

    /*
     * And a record as well as a line on the screen, which is not the same thing.
     * printk() writes to the terminal and the serial port and deliberately not
     * to the log ring - a log is a record of events rather than a transcript of
     * the screen - so a driver whose only success output is a printk leaves
     * nothing behind once the screen has scrolled, nothing in dmesg, and nothing
     * in /var/log/kern.log at the next sync.
     *
     * That matters more here than it would for most lines, because "the
     * controller came up" is the whole of what this release claims. pci_init()
     * has recorded its own success since v1.4.0 for the same reason and this
     * follows it. The failure paths above were already recorded; it was only the
     * one that goes right that was invisible.
     */
    klog_int(LOG_LEVEL_INFO, "XHCI",
             "Controller running; ports with a device attached", connected);

    return E_OK;
}
