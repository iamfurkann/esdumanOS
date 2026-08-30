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
#include "usbkbd.h"
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

/**
 * @brief Whether the timer tick may touch the event ring.
 *
 * Cleared until xhci_init() has finished, and it is the whole defence against
 * the release's one genuine trap. Enumeration waits on the event ring with
 * interrupts enabled, so a tick that drained the ring while that was happening
 * would consume the completion the boot path was waiting for - and the symptom
 * would be a controller that worked until a line was added to the timer, with
 * nothing anywhere pointing at the timer.
 *
 * One owner at a time: the boot path until it is done, the tick from then on.
 */
static int xhci_poll_armed = 0;

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

/* The same pair for the command ring, from the producing side. CRCR was
 * programmed with RCS set, so the controller is looking for a one. */
static uint32_t cmd_enqueue = 0;
static uint32_t cmd_pcs = 1;

/**
 * @brief Bytes between two context entries, from HCCPARAMS1's CSZ bit.
 *
 * Not sizeof(xhci_context_t). The structure is eight doublewords on every
 * controller; the *stride* is 32 or 64 depending on a bit the controller
 * publishes, and the two are equal on QEMU's. A driver that indexed by sizeof()
 * would read every context from the wrong offset on a controller with 64-byte
 * ones and nothing in this tree would ever say so - which is the exact shape of
 * the defect ata_identify() carried for thirty-six releases.
 */
static uint32_t ctx_stride = 0;

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
 * @brief Waits for one event of a given type and copies it out of the ring.
 *
 * Everything else that arrives is read and dropped, and the deadline is
 * deliberately not restarted by it. Port status change events queue up behind
 * every reset this driver performs, and a waiter that stopped at the first
 * record would take one of those for the answer it was waiting for.
 *
 * The record is copied before it is consumed. Once the dequeue pointer moves the
 * controller is free to write over that entry, so reading a field afterwards is
 * reading whatever arrived next - which for a slot id is the difference between
 * addressing a device and addressing whatever the controller happened to write.
 *
 * @param want_type The TRB type to stop on.
 * @param out Receives the record; may be 0.
 * @param what Logged if nothing of that type arrives in time.
 * @return E_OK, or E_IO on timeout.
 */
static int xhci_wait_event(uint32_t want_type, xhci_trb_t *out, const char *what) {
    uint32_t start = timer_get_ticks();

    for (;;) {
        volatile xhci_trb_t *ev = xhci_next_event();

        if (ev != 0) {
            xhci_trb_t copy;

            copy.param_lo = ev->param_lo;
            copy.param_hi = ev->param_hi;
            copy.status   = ev->status;
            copy.control  = ev->control;

            xhci_consume_event();

            if (XHCI_TRB_TYPE(copy.control) == want_type) {
                if (out != 0) *out = copy;
                return E_OK;
            }
            continue;
        }

        if ((timer_get_ticks() - start) > XHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "XHCI", what);
            return E_IO;
        }
        asm volatile("pause");
    }
}

/**
 * @brief Puts one command on the ring, rings the doorbell, waits for the answer.
 *
 * v1.8.0 issued exactly one command in the controller's lifetime and wrote it
 * into slot zero without a ring pointer, on the argument that an index which is
 * only ever zero is not an index. Enumeration issues two or three per device, so
 * the pointer is real now - and with it the wrap, which follows the Link TRB and
 * flips the producer's cycle state.
 *
 * That wrap cannot be reached today: XHCI_MAX_DEVICES is 8, each device costs at
 * most three commands, and the segment holds 255 usable entries. It is written
 * anyway for the reason the Link TRB was installed in the first place - the
 * shape of a ring is the hardware's business, and a producer that walks off the
 * end of a segment without following the link is wrong whether or not it ever
 * gets there.
 *
 * @param ev Receives the Command Completion Event; may be 0.
 * @return E_OK, or E_IO for a timeout or a completion code that is not success.
 */
static int xhci_command(uint32_t param_lo, uint32_t param_hi, uint32_t control,
                        xhci_trb_t *ev, const char *what) {
    if (cmd_enqueue == XHCI_TRBS_PER_SEGMENT - 1) {
        volatile xhci_trb_t *link = &cmd_ring[cmd_enqueue];

        link->control = (link->control & ~(uint32_t)XHCI_TRB_CYCLE) | cmd_pcs;
        cmd_enqueue = 0;
        cmd_pcs ^= 1;
    }

    volatile xhci_trb_t *trb = &cmd_ring[cmd_enqueue];

    trb->param_lo = param_lo;
    trb->param_hi = param_hi;
    trb->status   = 0;
    trb->control  = control | cmd_pcs;

    cmd_enqueue++;

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

    xhci_trb_t got;

    if (xhci_wait_event(XHCI_TRB_CMD_COMPLETE, &got, what) != E_OK) return E_IO;

    uint32_t code = XHCI_TRB_COMPLETION(got.status);

    if (code != XHCI_COMPLETION_SUCCESS) {
        klog_int(LOG_LEVEL_ERROR, "XHCI",
                 "Command ring answered, with a completion code of", (int)code);
        return E_IO;
    }

    if (ev != 0) *ev = got;
    return E_OK;
}

/**
 * @brief The command that proves the rings work, and moves nothing.
 *
 * A controller that has been reset and started reports itself as running with
 * its rings pointed at any memory at all. The doorbell, both rings and the
 * cycle-state agreement between driver and hardware are only demonstrated by
 * putting a TRB on one and reading its completion off the other.
 */
static int xhci_noop_command(void) {
    return xhci_command(0, 0, XHCI_TRB_SET_TYPE(XHCI_TRB_NOOP_CMD), 0,
                        "No completion arrived on the event ring; the rings are not talking.");
}

/* ── Devices ────────────────────────────────────────────────────────── */

/**
 * @brief The frames one addressed device costs, and where it is in its rings.
 *
 * Four frames each, and not one of them has to be contiguous with another: a
 * device context is at most 2048 bytes, an input context at most 2112, a
 * transfer ring segment is exactly a page, and the descriptor buffer is a page
 * because that is the clamp on what a device may claim its descriptors weigh.
 *
 * The two contexts do not share a frame, and the arithmetic is why rather than
 * taste: 2048 and 2112 are 4160 together, which is sixty-four bytes more than
 * there is. test_xhci.c asserts that, because it is the one place in this driver
 * where two structures nearly fit and do not.
 */
typedef struct {
    volatile uint8_t    *dev_ctx;
    uint32_t             dev_ctx_phys;
    volatile uint8_t    *in_ctx;
    uint32_t             in_ctx_phys;
    volatile xhci_trb_t *ep0_ring;
    uint32_t             ep0_ring_phys;
    uint32_t             ep0_enqueue;
    uint32_t             ep0_pcs;
    uint8_t             *desc_buf;
    uint32_t             desc_buf_phys;

    /* The fifth frame, and only for a device that turned out to be a keyboard.
     * A device that is not one never pays for it. */
    volatile xhci_trb_t *int_ring;
    uint32_t             int_ring_phys;
    uint32_t             int_enqueue;
    uint32_t             int_pcs;
    uint8_t              kbd_dci;   /**< Device context index of the endpoint. */

    /*
     * And two more frames for a device that turned out to be a disk. Bulk
     * endpoints are unidirectional, so a transport that sends a command and
     * reads a reply needs a ring each way.
     */
    volatile xhci_trb_t *bulk_in_ring;
    uint32_t             bulk_in_phys;
    uint32_t             bulk_in_enqueue;
    uint32_t             bulk_in_pcs;
    uint8_t              bulk_in_dci;

    volatile xhci_trb_t *bulk_out_ring;
    uint32_t             bulk_out_phys;
    uint32_t             bulk_out_enqueue;
    uint32_t             bulk_out_pcs;
    uint8_t              bulk_out_dci;

    /*
     * What a bulk transfer is waiting for. Written by xhci_poll(), which runs in
     * the timer interrupt, and read by the transfer that is spinning - so
     * volatile, or the compiler hoists the load out of the wait and the spin
     * never ends. The same reason the event ring itself is volatile.
     */
    volatile int      bulk_done;
    volatile uint32_t bulk_code;
    volatile uint32_t bulk_residual;
} xhci_devres_t;

static xhci_device_t devices[XHCI_MAX_DEVICES];
static xhci_devres_t devres[XHCI_MAX_DEVICES];
static int device_count = 0;
static int device_truncated = 0;

uint32_t xhci_context_stride(void) {
    return ctx_stride;
}

int xhci_device_count(void) {
    return device_count;
}

const xhci_device_t *xhci_get_device(int index) {
    if (index < 0 || index >= device_count) return 0;
    return &devices[index];
}

/** @brief The device recorded on a root hub port, numbered from one, or 0. */
static const xhci_device_t *xhci_device_on_port(int port) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].port == (uint8_t)port) return &devices[i];
    }
    return 0;
}

/**
 * @brief The doubleword array of one context entry.
 *
 * Every context access in this file goes through here, so the stride is applied
 * in one place rather than at each call site. That is the whole defence against
 * indexing by sizeof().
 */
static volatile uint32_t *ctx_entry(volatile uint8_t *base, int index) {
    return (volatile uint32_t *)(base + (uint32_t)index * ctx_stride);
}

int xhci_parse_config(const uint8_t *buf, uint32_t len, xhci_config_info_t *out) {
    if (buf == 0 || out == 0) return E_INVAL;
    if (len < USB_CONFIG_DESC_LEN) return E_INVAL;

    /*
     * The first descriptor has to be the configuration itself. Anything else is
     * a buffer that did not come from the transfer this function is for, and
     * walking it would be reading lengths out of whatever it actually is.
     */
    if (buf[1] != USB_DESC_CONFIG) return E_INVAL;

    xhci_config_info_t found;

    ft_memset(&found, 0, sizeof(found));
    found.total_length = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    found.config_value = buf[5];
    found.kbd_iface    = XHCI_NO_IFACE;
    found.msc_iface    = XHCI_NO_IFACE;

    /*
     * Which interface the walk is currently inside. An endpoint descriptor
     * belongs to the interface most recently seen above it - that is the whole
     * of how the two are associated, and it is a fact about the order the bytes
     * arrive in rather than anything either descriptor states. Finding the
     * keyboard's endpoint in a second pass would mean reconstructing it.
     */
    int in_boot_kbd = 0;
    int in_storage = 0;
    uint32_t off = 0;

    while (off + 2 <= len) {
        uint8_t dlen  = buf[off];
        uint8_t dtype = buf[off + 1];

        /*
         * A zero length is not a malformed descriptor to step over. It is a walk
         * that never ends, and every byte being stepped by came from the device -
         * so this is the same bound the extended capability chain got in v1.8.0,
         * arrived at from the other side of the machine. Refused rather than
         * skipped, because a descriptor set containing one is not describing
         * anything.
         */
        if (dlen == 0) return E_INVAL;

        /* The last descriptor cut short by the transfer. What was read is still
         * usable; what was not is simply not counted. */
        if (off + dlen > len) break;

        if (dtype == USB_DESC_INTERFACE && dlen >= 9) {
            if (found.interfaces == 0) {
                found.iface_class    = buf[off + 5];
                found.iface_subclass = buf[off + 6];
                found.iface_protocol = buf[off + 7];
            }
            if (found.interfaces < 0xFF) found.interfaces++;

            /*
             * Boot protocol, and only boot protocol. A keyboard that offers it
             * promises a fixed eight-byte report, which is what lets this
             * kernel read one without parsing a report descriptor. One that
             * does not is left alone rather than guessed at.
             *
             * The first one wins, the way the SATA driver takes the first port
             * with a disk on it. A machine with two keyboards has one this
             * kernel types on.
             */
            in_boot_kbd = (buf[off + 5] == USB_CLASS_HID &&
                           buf[off + 6] == USB_SUBCLASS_BOOT &&
                           buf[off + 7] == USB_PROTOCOL_KEYBOARD &&
                           found.kbd_iface == XHCI_NO_IFACE);

            if (in_boot_kbd) found.kbd_iface = buf[off + 2];

            /*
             * And the other interface this kernel can talk to. All three fields
             * are required rather than just the class: Bulk-Only Transport is
             * the protocol this driver implements, and a stick offering
             * something else - there are two other transports in the
             * specification - is one it cannot speak to at all.
             */
            in_storage = (buf[off + 5] == USB_CLASS_STORAGE &&
                          buf[off + 6] == USB_SUBCLASS_SCSI &&
                          buf[off + 7] == USB_PROTOCOL_BULK_ONLY &&
                          found.msc_iface == XHCI_NO_IFACE);

            if (in_storage) found.msc_iface = buf[off + 2];
        } else if (dtype == USB_DESC_ENDPOINT) {
            if (found.endpoints < 0xFF) found.endpoints++;

            /* An interrupt endpoint pointing at the host, inside the interface
             * found above. Anything else on that interface is not where the
             * reports come from. */
            if (in_boot_kbd && dlen >= 7 && found.kbd_ep_addr == 0 &&
                (buf[off + 2] & USB_EP_DIR_IN) &&
                (buf[off + 3] & USB_EP_TYPE_MASK) == USB_EP_XFER_INTERRUPT) {
                found.kbd_ep_addr  = buf[off + 2];
                found.kbd_mps      = (uint16_t)(buf[off + 4] |
                                                ((uint16_t)buf[off + 5] << 8));
                found.kbd_interval = buf[off + 6];
            }

            /*
             * Storage takes two, and which is which is the direction bit. A
             * transport that sends a command and reads a reply needs both halves
             * because a bulk endpoint only ever runs one way.
             */
            if (in_storage && dlen >= 7 &&
                (buf[off + 3] & USB_EP_TYPE_MASK) == USB_EP_XFER_BULK) {
                uint16_t mps = (uint16_t)(buf[off + 4] |
                                          ((uint16_t)buf[off + 5] << 8));

                if ((buf[off + 2] & USB_EP_DIR_IN) && found.msc_in_addr == 0) {
                    found.msc_in_addr = buf[off + 2];
                    found.msc_in_mps  = mps;
                } else if (!(buf[off + 2] & USB_EP_DIR_IN) &&
                           found.msc_out_addr == 0) {
                    found.msc_out_addr = buf[off + 2];
                    found.msc_out_mps  = mps;
                }
            }
        }

        off += dlen;
    }

    *out = found;
    return E_OK;
}

/**
 * @brief Resets one port and leaves it enabled, or reports that it would not.
 *
 * A port reset is what makes the speed field mean anything: before one, PORTSC's
 * speed is undefined, which is why v1.8.0's lsusb could print "unknown speed"
 * for a device that was plainly attached. It is also what a device needs before
 * it will answer to a slot.
 *
 * The write goes through XHCI_PORTSC_PRESERVE like every other write to this
 * register, and the acknowledgement afterwards names the two change bits a reset
 * raises rather than writing back everything that was set.
 */
static int xhci_port_reset(int port) {
    uint32_t portsc = mmio_read(portsc_off(port));

    mmio_write(portsc_off(port), (portsc & XHCI_PORTSC_PRESERVE) | XHCI_PORTSC_PR);

    uint32_t start = timer_get_ticks();

    while (!(mmio_read(portsc_off(port)) & XHCI_PORTSC_PRC)) {
        if ((timer_get_ticks() - start) > XHCI_RESET_TICKS) {
            klog_int(LOG_LEVEL_WARN, "XHCI", "Port would not finish resetting", port + 1);
            return E_IO;
        }
        asm volatile("pause");
    }

    portsc = mmio_read(portsc_off(port));
    mmio_write(portsc_off(port),
               (portsc & XHCI_PORTSC_PRESERVE) | XHCI_PORTSC_PRC | XHCI_PORTSC_CSC);

    if (!(mmio_read(portsc_off(port)) & XHCI_PORTSC_PED)) {
        klog_int(LOG_LEVEL_WARN, "XHCI", "Port did not enable after its reset", port + 1);
        return E_IO;
    }

    return E_OK;
}

/** @brief Endpoint zero's packet size before the device has been asked. */
static uint32_t ep0_packet_size(uint8_t speed) {
    if (speed == XHCI_SPEED_HIGH) return XHCI_EP0_MPS_HIGH;
    if (speed == XHCI_SPEED_SUPER || speed == XHCI_SPEED_SUPER_PLUS) {
        return XHCI_EP0_MPS_SUPER;
    }
    return XHCI_EP0_MPS_DEFAULT;
}

/** @brief Puts one TRB on a device's control ring, following the link at the end. */
static void ep0_push(xhci_devres_t *r, uint32_t lo, uint32_t hi,
                     uint32_t status, uint32_t control) {
    if (r->ep0_enqueue == XHCI_TRBS_PER_SEGMENT - 1) {
        volatile xhci_trb_t *link = &r->ep0_ring[r->ep0_enqueue];

        link->control = (link->control & ~(uint32_t)XHCI_TRB_CYCLE) | r->ep0_pcs;
        r->ep0_enqueue = 0;
        r->ep0_pcs ^= 1;
    }

    volatile xhci_trb_t *t = &r->ep0_ring[r->ep0_enqueue];

    t->param_lo = lo;
    t->param_hi = hi;
    t->status   = status;
    t->control  = control | r->ep0_pcs;

    r->ep0_enqueue++;
}

/**
 * @brief One device-to-host control transfer into the device's own buffer.
 *
 * Three stages, and the third is where the direction stops being obvious. A
 * status stage runs opposite to the data stage it follows, so a read ends with
 * an OUT; a transfer with no data stage has no opposite and its status stage
 * must be IN. Getting that backwards is a transfer the controller accepts and
 * never completes.
 *
 * Only the last TRB carries interrupt-on-completion, so one event comes back for
 * the whole transfer rather than three.
 *
 * @return E_OK, or E_IO. On success the bytes are in r->desc_buf.
 */
static int xhci_control(int index, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t windex, uint16_t length) {
    xhci_devres_t *r = &devres[index];
    uint8_t slot = devices[index].slot;

    /*
     * The direction is the request type's to state, not this function's to
     * assume. v1.9.0 only ever read descriptors and hard-coded IN; configuring
     * an interface and putting a keyboard into boot protocol are both writes
     * with no data at all, and a transfer whose stages disagree with its setup
     * packet is one the controller accepts and never completes.
     */
    int dir_in = (request_type & USB_DIR_IN) != 0;

    uint32_t setup_lo = (uint32_t)request_type
                      | ((uint32_t)request << 8)
                      | ((uint32_t)value << 16);
    uint32_t setup_hi = (uint32_t)windex | ((uint32_t)length << 16);

    ep0_push(r, setup_lo, setup_hi, 8,
             XHCI_TRB_SET_TYPE(XHCI_TRB_SETUP_STAGE) | XHCI_TRB_IDT
             | (length ? (dir_in ? XHCI_TRT_IN : XHCI_TRT_OUT) : XHCI_TRT_NO_DATA));

    if (length) {
        ep0_push(r, r->desc_buf_phys, 0, length,
                 XHCI_TRB_SET_TYPE(XHCI_TRB_DATA_STAGE)
                 | (dir_in ? XHCI_TRB_DIR_IN : 0));
    }

    /*
     * A status stage runs opposite to the data stage it follows, and a transfer
     * with no data stage has no opposite - the specification says its status
     * stage is IN. So the direction bit is set unless there was an IN data
     * stage to be the opposite of.
     */
    ep0_push(r, 0, 0, 0,
             XHCI_TRB_SET_TYPE(XHCI_TRB_STATUS_STAGE) | XHCI_TRB_IOC
             | ((length && dir_in) ? 0 : XHCI_TRB_DIR_IN));

    /* Doorbell for this slot, target 1: endpoint zero. Doorbell zero belongs to
     * the command ring and every other index is a slot. */
    mmio_write(db_base + (uint32_t)slot * 4, 1);

    xhci_trb_t ev;

    if (xhci_wait_event(XHCI_TRB_TRANSFER_EV, &ev,
                        "A control transfer never completed.") != E_OK) {
        return E_IO;
    }

    uint32_t code = XHCI_TRB_COMPLETION(ev.status);

    /*
     * Short packet is a success. A device that has less to say than it was asked
     * for ends the transfer early and reports the shortfall as a residual, which
     * is the normal way a descriptor read of "give me everything" terminates.
     */
    if (code != XHCI_COMPLETION_SUCCESS && code != 13 /* Short Packet */) {
        klog_int(LOG_LEVEL_WARN, "XHCI", "Control transfer failed with code", (int)code);
        return E_IO;
    }

    return E_OK;
}

/**
 * @brief Builds the input context Address Device reads, and issues it.
 *
 * A0 and A1: the slot context and endpoint zero, which is the whole of what this
 * command is allowed to be given. The slot context carries the speed, the root
 * hub port the device is on, and a context-entries count of one - meaning the
 * device has exactly endpoint zero, which is true until an interface is chosen.
 */
static int xhci_address_device(int index, uint8_t speed, int port) {
    xhci_devres_t *r = &devres[index];

    /* Both contexts arrive zeroed - alloc_dma_page() clears the frame - so what
     * follows sets the fields that must be set and leaves the rest at the zero
     * the specification wants them at. There is no second pass over a device
     * here: nothing re-enumerates. */
    volatile uint32_t *control = ctx_entry(r->in_ctx, 0);
    volatile uint32_t *slot    = ctx_entry(r->in_ctx, 1);
    volatile uint32_t *ep0     = ctx_entry(r->in_ctx, 2);

    control[1] = XHCI_IN_ADD_SLOT | XHCI_IN_ADD_EP0;

    slot[0] = XHCI_SLOT_SPEED(speed) | XHCI_SLOT_CTX_ENTRIES(1);
    slot[1] = XHCI_SLOT_ROOT_PORT(port + 1);

    ep0[1] = XHCI_EP_TYPE(XHCI_EP_TYPE_CONTROL)
           | XHCI_EP_CERR(3)
           | XHCI_EP_MAX_PACKET(ep0_packet_size(speed));
    ep0[2] = r->ep0_ring_phys | XHCI_EP_DCS;
    ep0[3] = 0;
    ep0[4] = 8;   /* average TRB length; the specification asks for 8 here */

    return xhci_command(r->in_ctx_phys, 0,
                        XHCI_TRB_SET_TYPE(XHCI_TRB_ADDRESS_DEV)
                        | XHCI_TRB_SET_SLOT(devices[index].slot),
                        0, "Address Device never completed.");
}

uint32_t xhci_ep0_size_reported(uint8_t speed, uint8_t reported) {
    /*
     * Full speed and nothing else, because full speed is the only one where
     * bMaxPacketSize0 is a byte count that the device gets to choose. At every
     * other speed the specification fixes it - 8 at low, 64 at high, 512 at
     * super - and ep0_packet_size() has already programmed that.
     *
     * At SuperSpeed the field is not a byte count at all: it holds the base-2
     * logarithm, so a stick that uses 512-byte packets writes 9. Believing that
     * number opens endpoint zero at nine bytes, which is not a legal packet size
     * at any speed, and the descriptors read through it come back as something
     * that is not a descriptor. Nothing fails loudly; the device simply turns out
     * to have no interfaces worth anything.
     */
    if (speed != XHCI_SPEED_FULL) return 0;

    /*
     * And a full-speed device only gets to choose among four. A value outside
     * them is refused rather than programmed: endpoint zero is already open at
     * 8, which every speed permits, so ignoring a bad answer costs some
     * throughput and keeps the device usable.
     */
    if (reported == 8 || reported == 16 || reported == 32 || reported == 64) {
        return reported;
    }
    return 0;
}

/**
 * @brief Re-opens endpoint zero at the packet size the device actually uses.
 *
 * Only a full-speed device can surprise us here: every other speed fixes the
 * size, and this one may answer 8, 16, 32 or 64. Endpoint zero is opened at 8 so
 * that the first eight bytes can be read whatever the answer is, and this is
 * what corrects it afterwards.
 *
 * That paragraph was here before v1.11.0 and the code below did not enforce a
 * word of it. It compared the reported value against the programmed one at every
 * speed, and until this release every device on the test bench was a full-speed
 * keyboard or mouse answering 8 - so the comparison was always equal and the
 * function never ran. A USB stick is the first SuperSpeed device this project has
 * ever enumerated, it answers 9 meaning 512, and this took that 9 literally. The
 * decision moved into xhci_ep0_size_reported(), where it can be asserted without
 * a device.
 */
static int xhci_refine_ep0(int index, uint8_t speed, uint32_t reported) {
    xhci_devres_t *r = &devres[index];
    volatile uint32_t *ep0_in = ctx_entry(r->in_ctx, 2);
    uint32_t current = (ep0_in[1] >> 16) & 0xFFFF;
    uint32_t want = xhci_ep0_size_reported(speed, (uint8_t)reported);

    if (want == 0 && speed == XHCI_SPEED_FULL && reported != 0) {
        klog_int(LOG_LEVEL_WARN, "XHCI",
                 "Ignoring an endpoint zero packet size no full-speed device may use",
                 (int)reported);
    }

    if (want == 0 || want == current) return E_OK;

    klog_int(LOG_LEVEL_INFO, "XHCI",
             "Re-opening endpoint zero at the size the device reported", (int)want);

    volatile uint32_t *control = ctx_entry(r->in_ctx, 0);

    control[0] = 0;
    control[1] = XHCI_IN_ADD_EP0;

    ep0_in[1] = (ep0_in[1] & 0x0000FFFF) | XHCI_EP_MAX_PACKET(want);

    return xhci_command(r->in_ctx_phys, 0,
                        XHCI_TRB_SET_TYPE(XHCI_TRB_EVAL_CTX)
                        | XHCI_TRB_SET_SLOT(devices[index].slot),
                        0, "Evaluate Context never completed.");
}

/**
 * @brief Gives one connected port's device a slot, an address, and a name.
 *
 * Every failure here abandons the device rather than the enumeration: a port
 * that will not reset, a slot the controller will not enable or a device that
 * will not describe itself costs that port and nothing else. A machine where one
 * of four devices is broken should report the other three.
 *
 * @return E_OK when the device was recorded.
 */
static int xhci_enumerate_port(int port) {
    if (device_count >= XHCI_MAX_DEVICES) {
        device_truncated = 1;
        return E_NOMEM;
    }

    if (xhci_port_reset(port) != E_OK) return E_IO;

    uint32_t portsc = mmio_read(portsc_off(port));
    uint8_t  speed  = (uint8_t)XHCI_PORTSC_SPEED(portsc);

    xhci_trb_t ev;

    if (xhci_command(0, 0, XHCI_TRB_SET_TYPE(XHCI_TRB_ENABLE_SLOT), &ev,
                     "Enable Slot never completed.") != E_OK) {
        return E_IO;
    }

    uint8_t slot = (uint8_t)XHCI_TRB_SLOT(ev.control);

    if (slot == 0 || slot > XHCI_MAX_SLOTS) {
        klog_int(LOG_LEVEL_ERROR, "XHCI", "Controller returned an unusable slot id", slot);
        return E_IO;
    }

    int index = device_count;
    xhci_devres_t *r = &devres[index];

    ft_memset(&devices[index], 0, sizeof(xhci_device_t));
    devices[index].port  = (uint8_t)(port + 1);
    devices[index].slot  = slot;
    devices[index].speed = speed;

    r->dev_ctx = (volatile uint8_t *)alloc_dma_page(&r->dev_ctx_phys);
    r->in_ctx  = (volatile uint8_t *)alloc_dma_page(&r->in_ctx_phys);
    r->ep0_ring = (volatile xhci_trb_t *)alloc_dma_page(&r->ep0_ring_phys);
    r->desc_buf = alloc_dma_page(&r->desc_buf_phys);

    if (r->dev_ctx == 0 || r->in_ctx == 0 || r->ep0_ring == 0 || r->desc_buf == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Out of memory addressing a device.");
        return E_NOMEM;
    }

    /* The control ring is a ring, so it ends in a link back to its own start -
     * the same shape the command ring has and for the same reason. */
    volatile xhci_trb_t *link = &r->ep0_ring[XHCI_TRBS_PER_SEGMENT - 1];

    link->param_lo = r->ep0_ring_phys;
    link->param_hi = 0;
    link->status   = 0;
    link->control  = XHCI_TRB_SET_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    r->ep0_enqueue = 0;
    r->ep0_pcs = 1;

    /* Entry zero of the device context array is the scratchpad array; slots are
     * numbered from one, and this is why. */
    volatile uint32_t *dcbaa = (volatile uint32_t *)(dma_virt + XHCI_OFF_DCBAA);

    dcbaa[slot * 2]     = r->dev_ctx_phys;
    dcbaa[slot * 2 + 1] = 0;

    if (xhci_address_device(index, speed, port) != E_OK) return E_IO;

    /*
     * Eight bytes first, and the eighth is why: bMaxPacketSize0. It cannot be
     * read without a working endpoint zero and endpoint zero cannot be sized
     * without it, and eight bytes is the amount that fits in one packet at every
     * size the field can hold.
     */
    if (xhci_control(index, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (uint16_t)(USB_DESC_DEVICE << 8), 0, 8) != E_OK) {
        return E_IO;
    }

    if (xhci_refine_ep0(index, speed, r->desc_buf[7]) != E_OK) return E_IO;

    if (xhci_control(index, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (uint16_t)(USB_DESC_DEVICE << 8), 0,
                        USB_DEVICE_DESC_LEN) != E_OK) {
        return E_IO;
    }

    const uint8_t *d = r->desc_buf;

    devices[index].dev_class    = d[4];
    devices[index].dev_subclass = d[5];
    devices[index].dev_protocol = d[6];
    devices[index].vendor_id    = (uint16_t)(d[8]  | ((uint16_t)d[9]  << 8));
    devices[index].product_id   = (uint16_t)(d[10] | ((uint16_t)d[11] << 8));

    /*
     * The configuration descriptor twice: nine bytes to learn wTotalLength, then
     * that many. The length comes from the device, so it is clamped to the
     * buffer rather than believed - a device claiming 60000 bytes of descriptors
     * would otherwise be a write past a page.
     */
    /*
     * Whatever happens below, the device keeps its slot and its name. A device
     * whose configuration could not be read is still a device that is plugged
     * in, and lsusb saying so is better than lsusb pretending the port is empty.
     *
     * What it must not do is happen quietly. Every one of these three steps used
     * to fail into an empty config with no line anywhere, so a device the driver
     * could not read was indistinguishable from one with nothing on it worth
     * driving - and when a USB stick landed in exactly that state, the only
     * evidence was a disk that never appeared. That is the v1.8.0 lesson from the
     * other side: there, the path that left no record was the one that worked.
     */
    int described = 0;

    if (xhci_control(index, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (uint16_t)(USB_DESC_CONFIG << 8), 0,
                        USB_CONFIG_DESC_LEN) == E_OK) {
        uint32_t total = (uint32_t)(r->desc_buf[2] | ((uint32_t)r->desc_buf[3] << 8));

        if (total > XHCI_DESC_BUF_LEN) total = XHCI_DESC_BUF_LEN;
        if (total < USB_CONFIG_DESC_LEN) total = USB_CONFIG_DESC_LEN;

        if (xhci_control(index, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            (uint16_t)(USB_DESC_CONFIG << 8), 0,
                            (uint16_t)total) == E_OK) {
            if (xhci_parse_config(r->desc_buf, total,
                                  &devices[index].config) == E_OK) {
                described = 1;
            } else {
                klog_int(LOG_LEVEL_WARN, "XHCI",
                         "A configuration descriptor this driver could not read, on port",
                         port + 1);
            }
        }
    }

    if (!described) {
        klog_int(LOG_LEVEL_WARN, "XHCI",
                 "Device enumerated with no usable configuration, on port", port + 1);
    }

    device_count++;
    return E_OK;
}

/* ── The keyboard's endpoint ────────────────────────────────────────── */

/** @brief The device holding a slot id, or -1. */
static int device_for_slot(uint32_t slot) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].slot == (uint8_t)slot) return i;
    }
    return -1;
}

/** @brief Puts one TRB on a device's interrupt ring, following the link. */
static void int_push(xhci_devres_t *r, uint32_t lo, uint32_t hi,
                     uint32_t status, uint32_t control) {
    if (r->int_enqueue == XHCI_TRBS_PER_SEGMENT - 1) {
        volatile xhci_trb_t *link = &r->int_ring[r->int_enqueue];

        link->control = (link->control & ~(uint32_t)XHCI_TRB_CYCLE) | r->int_pcs;
        r->int_enqueue = 0;
        r->int_pcs ^= 1;
    }

    volatile xhci_trb_t *t = &r->int_ring[r->int_enqueue];

    t->param_lo = lo;
    t->param_hi = hi;
    t->status   = status;
    t->control  = control | r->int_pcs;

    r->int_enqueue++;
}

/**
 * @brief Asks the keyboard for its next report.
 *
 * One transfer outstanding at a time, which is all this driver needs: it looks
 * at the ring once per timer tick, and a keyboard polled every ten milliseconds
 * has nothing to gain from a second request queued behind the first.
 */
static void xhci_queue_report(int index) {
    xhci_devres_t *r = &devres[index];

    int_push(r, r->desc_buf_phys + XHCI_OFF_REPORT, 0, USBKBD_REPORT_LEN,
             XHCI_TRB_SET_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);

    /* An endpoint's doorbell target is its device context index. Zero is the
     * command ring's and belongs to no slot at all. */
    mmio_write(db_base + (uint32_t)devices[index].slot * 4, r->kbd_dci);
}

/**
 * @brief The interval field, which is not the number the descriptor carries.
 *
 * The endpoint context wants an exponent: the period is 2^interval microframes
 * of 125 microseconds. At high speed and above bInterval is already such an
 * exponent and is simply one larger. At full and low speed it is a count of
 * one-millisecond frames, and one millisecond is eight microframes - so the
 * exponent starts at three and rises with the count.
 *
 * Reading bInterval straight into the field would ask a full-speed keyboard
 * reporting every 10 ms to report every 1.25 microseconds.
 */
static uint32_t ep_interval(uint8_t speed, uint8_t binterval) {
    if (speed == XHCI_SPEED_HIGH || speed == XHCI_SPEED_SUPER ||
        speed == XHCI_SPEED_SUPER_PLUS) {
        return binterval > 0 ? (uint32_t)(binterval - 1) : 0;
    }

    uint32_t frames = binterval > 0 ? binterval : 1;
    uint32_t exponent = 3;

    while (frames > 1 && exponent < 15) {
        frames >>= 1;
        exponent++;
    }
    return exponent;
}

/**
 * @brief Opens one endpoint on a configured device.
 *
 * The shape xhci_open_keyboard() had, lifted out so that the disk can use it
 * too. What differs between an interrupt endpoint and a bulk one is three
 * fields; what is the same is the input context, the add flags, the context
 * entries count and the Configure Endpoint command, and having that in one place
 * is the difference between one thing to get right and two.
 *
 * @param ring_out Receives the transfer ring; the caller keeps the enqueue state.
 * @return E_OK, or a negative errno.
 */
static int xhci_open_endpoint(int index, uint8_t ep_addr, uint32_t ep_type,
                              uint16_t mps, uint32_t interval,
                              volatile xhci_trb_t **ring_out,
                              uint32_t *ring_phys_out, uint8_t *dci_out) {
    xhci_devres_t *r = &devres[index];
    volatile xhci_trb_t *ring;
    uint32_t ring_phys = 0;

    ring = (volatile xhci_trb_t *)alloc_dma_page(&ring_phys);
    if (ring == 0) {
        klog(LOG_LEVEL_ERROR, "XHCI", "Out of memory opening an endpoint.");
        return E_NOMEM;
    }

    volatile xhci_trb_t *link = &ring[XHCI_TRBS_PER_SEGMENT - 1];

    link->param_lo = ring_phys;
    link->param_hi = 0;
    link->status   = 0;
    link->control  = XHCI_TRB_SET_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

    /*
     * The device context index of an IN endpoint is twice its number plus one,
     * and of an OUT endpoint twice its number. That is the whole of why a bulk
     * pair occupies two entries rather than one.
     */
    uint8_t dci = (uint8_t)(USB_EP_NUMBER(ep_addr) * 2 +
                            ((ep_addr & USB_EP_DIR_IN) ? 1 : 0));

    volatile uint32_t *control = ctx_entry(r->in_ctx, 0);
    volatile uint32_t *slot    = ctx_entry(r->in_ctx, 1);
    volatile uint32_t *ep      = ctx_entry(r->in_ctx, 1 + dci);

    control[0] = 0;
    control[1] = XHCI_IN_ADD_SLOT | ((uint32_t)1 << dci);

    /*
     * Context Entries is the highest index in use, so it only ever rises. A
     * second endpoint opened after the first must not lower it, or the
     * controller stops believing in the one already there.
     */
    uint32_t entries = (slot[0] >> 27) & 0x1F;

    if (dci > entries) entries = dci;
    slot[0] = (slot[0] & 0x07FFFFFFu) | XHCI_SLOT_CTX_ENTRIES(entries);

    ep[0] = interval << 16;
    ep[1] = XHCI_EP_TYPE(ep_type) | XHCI_EP_CERR(3) | XHCI_EP_MAX_PACKET(mps);
    ep[2] = ring_phys | XHCI_EP_DCS;
    ep[3] = 0;
    ep[4] = ((uint32_t)mps << 16) | mps;

    if (xhci_command(r->in_ctx_phys, 0,
                     XHCI_TRB_SET_TYPE(XHCI_TRB_CONFIG_EP)
                     | XHCI_TRB_SET_SLOT(devices[index].slot),
                     0, "Configure Endpoint never completed.") != E_OK) {
        return E_IO;
    }

    *ring_out = ring;
    *ring_phys_out = ring_phys;
    *dci_out = dci;
    return E_OK;
}

/**
 * @brief Selects the configuration, opens the interrupt endpoint, starts reads.
 *
 * The first boot keyboard on the bus wins and the rest are left alone, which is
 * the decision the SATA driver makes about ports for the same reason: a system
 * that types on one keyboard needs one, and choosing between two is a policy
 * nothing here has an opinion to base on.
 *
 * @return E_OK when the keyboard is configured and its first read is queued.
 */
static int xhci_open_keyboard(int index) {
    xhci_device_t *d = &devices[index];
    xhci_devres_t *r = &devres[index];

    if (d->config.kbd_ep_addr == 0) return E_NODEV;
    if (usbkbd_attached()) return E_NODEV;

    if (xhci_control(index, 0, USB_REQ_SET_CONFIGURATION,
                     d->config.config_value, 0, 0) != E_OK) {
        klog(LOG_LEVEL_WARN, "XHCI", "Keyboard would not accept its configuration.");
        return E_IO;
    }

    if (xhci_open_endpoint(index, d->config.kbd_ep_addr, XHCI_EP_TYPE_INT_IN,
                           d->config.kbd_mps,
                           ep_interval(d->speed, d->config.kbd_interval),
                           &r->int_ring, &r->int_ring_phys,
                           &r->kbd_dci) != E_OK) {
        return E_IO;
    }

    r->int_enqueue = 0;
    r->int_pcs = 1;

    /*
     * Boot protocol, and this is what makes the reports a fixed eight bytes.
     * Without it a keyboard is in report protocol and its reports have whatever
     * shape its report descriptor declares - which this kernel does not read.
     */
    if (xhci_control(index, USB_REQTYPE_CLASS_IFACE, USB_REQ_SET_PROTOCOL,
                     USB_PROTOCOL_BOOT, d->config.kbd_iface, 0) != E_OK) {
        klog(LOG_LEVEL_WARN, "XHCI", "Keyboard would not enter boot protocol.");
        return E_IO;
    }

    ft_memset(r->desc_buf + XHCI_OFF_REPORT, 0, USBKBD_REPORT_LEN);

    usbkbd_attach();
    xhci_queue_report(index);

    printk("[XHCI] USB keyboard on port %d, endpoint %d, every %d ms\n",
           d->port, USB_EP_NUMBER(d->config.kbd_ep_addr), d->config.kbd_interval);
    klog_int(LOG_LEVEL_INFO, "XHCI", "USB keyboard configured on port", d->port);

    return E_OK;
}

/**
 * @brief Guards the event ring against being drained twice at once.
 *
 * xhci_poll() has two callers now and one of them can interrupt the other. A
 * bulk transfer spins calling it from ordinary kernel context; the timer tick
 * calls it from IRQ0, and a tick lands in the middle of that spin a hundred
 * times a second. Two readers stepping the same dequeue pointer would each
 * consume records the other was about to look at, and the ring's cycle state
 * would stop meaning anything.
 *
 * The flag is tested and set with interrupts off - the tree's own idiom, the one
 * elf.c and entropy.c use - and then interrupts go back to whatever they were,
 * so the window is a few instructions rather than the whole drain.
 */
static volatile int poll_busy = 0;

void xhci_poll(void) {
    uint32_t eflags;

    if (!xhci_poll_armed) return;

    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    if (poll_busy) {
        if (eflags & 0x200) asm volatile("sti");
        return;
    }
    poll_busy = 1;

    if (eflags & 0x200) asm volatile("sti");

    /*
     * Bounded, and the bound is the interrupt handler's. Everything else in this
     * driver waits with a deadline because it runs on the boot path; this runs a
     * hundred times a second inside IRQ0, so it does not wait at all - it takes
     * what is there, up to a limit, and leaves the rest for the next tick.
     */
    for (int drained = 0; drained < XHCI_POLL_MAX_EVENTS; drained++) {
        volatile xhci_trb_t *ev = xhci_next_event();

        if (ev == 0) break;

        uint32_t type = XHCI_TRB_TYPE(ev->control);
        uint32_t slot = XHCI_TRB_SLOT(ev->control);
        uint32_t endpoint = XHCI_TRB_EP_ID(ev->control);
        uint32_t code = XHCI_TRB_COMPLETION(ev->status);
        uint32_t residual = XHCI_TRB_RESIDUAL(ev->status);

        xhci_consume_event();

        /* Port status changes still arrive here and are still dropped. This
         * release does not react to a device appearing, and saying so is better
         * than a handler that looks like it might. */
        if (type != XHCI_TRB_TRANSFER_EV) continue;

        int index = device_for_slot(slot);

        if (index < 0) continue;

        xhci_devres_t *r = &devres[index];

        /*
         * Which endpoint finished, and that question did not exist until this
         * release. With one endpoint per device anything that completed was the
         * keyboard's; with a keyboard and a disk on the same bus the endpoint id
         * in the event is the only thing that says which.
         */
        if (r->int_ring != 0 && endpoint == r->kbd_dci) {
            /*
             * Success, or a short packet - which is what a keyboard with nothing
             * to say produces and is not a failure. Anything else means the
             * report did not arrive, and the read is re-armed without one being
             * handed on: a stale buffer read as a report would look like keys
             * being released.
             */
            if (code == XHCI_COMPLETION_SUCCESS || code == 13) {
                usbkbd_report(r->desc_buf + XHCI_OFF_REPORT);
            }
            xhci_queue_report(index);
            continue;
        }

        if (endpoint == r->bulk_in_dci || endpoint == r->bulk_out_dci) {
            /*
             * Recorded rather than acted on. The transfer that queued this is
             * spinning on bulk_done in ordinary kernel context, and it is the
             * one that knows what to do with the answer - an interrupt handler
             * deciding what a failed disk read means is an interrupt handler
             * doing the block layer's job.
             */
            r->bulk_residual = residual;
            r->bulk_code = code;
            r->bulk_done = 1;
        }
    }

    poll_busy = 0;
}

/* ── Bulk endpoints, and the disk above them ────────────────────────── */

uint8_t *xhci_dma_page(uint32_t *phys_out) {
    if (phys_out == 0) return 0;
    return alloc_dma_page(phys_out);
}

int xhci_storage_device_next(int after) {
    /*
     * Starts one past whatever was handed in, so -1 starts at the beginning and
     * the caller never has to know that. A negative other than -1 would be a
     * caller that lost its place; it is treated the same way rather than allowed
     * to index backwards into the table.
     */
    int start = (after < 0) ? 0 : after + 1;

    for (int i = start; i < device_count; i++) {
        if (devices[i].config.msc_in_addr != 0 &&
            devices[i].config.msc_out_addr != 0) {
            return i;
        }
    }
    return -1;
}

int xhci_open_storage(int index) {
    xhci_device_t *d;
    xhci_devres_t *r;

    if (index < 0 || index >= device_count) return E_INVAL;

    d = &devices[index];
    r = &devres[index];

    if (d->config.msc_in_addr == 0 || d->config.msc_out_addr == 0) return E_NODEV;
    if (r->bulk_in_ring != 0) return E_OK;   /* already open */

    if (xhci_control(index, 0, USB_REQ_SET_CONFIGURATION,
                     d->config.config_value, 0, 0) != E_OK) {
        klog(LOG_LEVEL_WARN, "XHCI", "Storage device would not accept its configuration.");
        return E_IO;
    }

    if (xhci_open_endpoint(index, d->config.msc_in_addr, XHCI_EP_TYPE_BULK_IN,
                           d->config.msc_in_mps, 0,
                           &r->bulk_in_ring, &r->bulk_in_phys,
                           &r->bulk_in_dci) != E_OK) {
        return E_IO;
    }
    r->bulk_in_enqueue = 0;
    r->bulk_in_pcs = 1;

    if (xhci_open_endpoint(index, d->config.msc_out_addr, XHCI_EP_TYPE_BULK_OUT,
                           d->config.msc_out_mps, 0,
                           &r->bulk_out_ring, &r->bulk_out_phys,
                           &r->bulk_out_dci) != E_OK) {
        return E_IO;
    }
    r->bulk_out_enqueue = 0;
    r->bulk_out_pcs = 1;

    klog_int(LOG_LEVEL_INFO, "XHCI", "Storage endpoints open on port", d->port);
    return E_OK;
}

/** @brief Puts one TRB on a bulk ring, following the link at the end. */
static void bulk_push(volatile xhci_trb_t *ring, uint32_t *enqueue, uint32_t *pcs,
                      uint32_t ring_phys, uint32_t lo, uint32_t status,
                      uint32_t control) {
    if (*enqueue == XHCI_TRBS_PER_SEGMENT - 1) {
        volatile xhci_trb_t *link = &ring[*enqueue];

        link->param_lo = ring_phys;
        link->control = (link->control & ~(uint32_t)XHCI_TRB_CYCLE) | *pcs;
        *enqueue = 0;
        *pcs ^= 1;
    }

    volatile xhci_trb_t *t = &ring[*enqueue];

    t->param_lo = lo;
    t->param_hi = 0;
    t->status   = status;
    t->control  = control | *pcs;

    (*enqueue)++;
}

int xhci_bulk_transfer(int index, int in, uint32_t phys, uint32_t len,
                       uint32_t *residual) {
    if (index < 0 || index >= device_count) return E_INVAL;

    xhci_devres_t *r = &devres[index];

    if (r->bulk_in_ring == 0 || r->bulk_out_ring == 0) return E_INVAL;

    r->bulk_done = 0;
    r->bulk_code = 0;
    r->bulk_residual = 0;

    if (in) {
        bulk_push(r->bulk_in_ring, &r->bulk_in_enqueue, &r->bulk_in_pcs,
                  r->bulk_in_phys, phys, len,
                  XHCI_TRB_SET_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);
        mmio_write(db_base + (uint32_t)devices[index].slot * 4, r->bulk_in_dci);
    } else {
        bulk_push(r->bulk_out_ring, &r->bulk_out_enqueue, &r->bulk_out_pcs,
                  r->bulk_out_phys, phys, len,
                  XHCI_TRB_SET_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);
        mmio_write(db_base + (uint32_t)devices[index].slot * 4, r->bulk_out_dci);
    }

    /*
     * And here is the release's one real design decision, spelled out because it
     * is not obvious and the obvious alternative is much worse.
     *
     * The event ring has one reader, xhci_poll(), and it normally runs from the
     * timer tick. This wait could simply spin until a tick happened to drain the
     * completion - that is what ata_wait_irq() does with its IRQ - and it would
     * be correct and cost up to ten milliseconds per transfer. A Bulk-Only
     * sector read is three transfers, so a sector would take thirty
     * milliseconds and formatting a disk would take hours.
     *
     * So the wait drives the poll itself. The ring still has exactly one reader
     * at a time - poll_busy sees to that - but it is read as fast as this loop
     * goes rather than a hundred times a second.
     */
    uint32_t start = timer_get_ticks();

    while (!r->bulk_done) {
        if ((timer_get_ticks() - start) > XHCI_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "XHCI", "A bulk transfer never completed.");
            return E_IO;
        }
        xhci_poll();
        asm volatile("pause");
    }

    if (residual != 0) *residual = r->bulk_residual;

    /* Short packet is not a failure: a device with less to say than it was asked
     * for ends the transfer early and reports the shortfall. */
    if (r->bulk_code != XHCI_COMPLETION_SUCCESS && r->bulk_code != 13) {
        klog_int(LOG_LEVEL_WARN, "XHCI", "Bulk transfer failed with code",
                 (int)r->bulk_code);
        return E_IO;
    }

    return E_OK;
}

/**
 * @brief Walks every port that reported a device and enumerates what is there.
 *
 * Reported by xhci_bring_up_ports(), which ran before the No Op command. A port
 * that fails is logged and skipped; the walk continues.
 */
static void xhci_enumerate(void) {
    for (int i = 0; i < xhci_ports; i++) {
        if (!port_state[i].connected) continue;
        if (device_count >= XHCI_MAX_DEVICES) {
            device_truncated = 1;
            klog_int(LOG_LEVEL_WARN, "XHCI",
                     "More devices are attached than this driver addresses; the list stops at",
                     XHCI_MAX_DEVICES);
            break;
        }

        if (xhci_enumerate_port(i) != E_OK) continue;

        /* The speed field only means something after the reset, so the port
         * record is corrected from what enumeration learned. */
        port_state[i].speed   = devices[device_count - 1].speed;
        port_state[i].enabled = 1;

        /*
         * And if it turned out to be a keyboard, open it. A device that is not
         * one returns E_NODEV here and costs nothing; one that is and then
         * fails is logged and left enumerated, because a keyboard that would
         * not configure is still a device worth naming in lsusb.
         */
        xhci_open_keyboard(device_count - 1);
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
    xhci_poll_armed = 0;
    xhci_ports = 0;
    device_count = 0;
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

        /*
         * The device on that port, if enumeration got that far. A port that
         * reports a device and has no line under it is a device that would not
         * answer, which is worth being able to see rather than smoothing over -
         * so the port line is printed either way and this one is extra.
         */
        const xhci_device_t *d = xhci_device_on_port(i + 1);

        if (d == 0) continue;

        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "  slot %d %04x:%04x class %02x/%02x/%02x\n",
                        d->slot, d->vendor_id, d->product_id,
                        d->dev_class, d->dev_subclass, d->dev_protocol);

        /*
         * A device class of zero means the device declines to answer at that
         * level and the interface is where the answer is - which is the case for
         * every HID device, so it is the common case rather than the odd one.
         */
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "  %d interface%s, %d endpoint%s, interface class %02x/%02x/%02x\n",
                        d->config.interfaces, d->config.interfaces == 1 ? "" : "s",
                        d->config.endpoints, d->config.endpoints == 1 ? "" : "s",
                        d->config.iface_class, d->config.iface_subclass,
                        d->config.iface_protocol);
    }

    if (device_truncated) {
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "(devices not addressed past %d)\n", XHCI_MAX_DEVICES);
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

    /*
     * The distance between two context entries, and the one number in this
     * driver that must not be taken from sizeof(). A controller with 64-byte
     * contexts lays them out twice as far apart as the structure is long, and
     * QEMU's uses 32 - so a driver that indexed by the structure would be
     * correct on every machine this project can run on and wrong on a great many
     * it cannot. ata_identify() was that shape of wrong for thirty-six releases.
     */
    ctx_stride = XHCI_HCC1_CSZ(hccparams1) ? 64 : 32;
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
     *
     * Set before enumeration rather than after, and deliberately: a bus that
     * works is what this flag is about, and a device that refuses to describe
     * itself does not make the controller stop working. Enumeration reports its
     * own failures per port and takes nothing else down with them.
     */
    xhci_ready = 1;

    xhci_enumerate();

    /*
     * And only now may the timer touch the event ring. Everything above waited
     * on it with interrupts enabled; from here nothing does, and the tick is its
     * only reader. The flag is the handover.
     */
    xhci_poll_armed = 1;

    int connected = 0;

    for (int i = 0; i < xhci_ports; i++) {
        if (port_state[i].connected) connected++;
    }

    printk("[XHCI] USB controller %02x:%02x.%d: %d ports, %d connected, %d addressed, keyboard %s [Polled]\n",
           dev->bus, dev->device, dev->function, xhci_ports, connected, device_count,
           usbkbd_attached() ? "yes" : "no");

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
             "Controller running; devices addressed and described", device_count);

    return E_OK;
}
