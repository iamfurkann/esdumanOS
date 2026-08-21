/*
 * File: sys_mem.c
 * Purpose: User-space dynamic memory - the program break and anonymous mappings.
 *
 * Until now a program's memory was whatever its ELF image asked for plus a fixed
 * 32-page stack, decided once by the loader and never changed again. Every tool
 * in /bin works from arrays sized at compile time, and the shell in particular
 * carries a handful of them, because there has been nothing else to work from.
 *
 * Two ways to ask for more, and they are deliberately different shapes:
 *
 *   brk   moves one boundary. The heap is a single run that grows upwards from
 *         the end of the program's image, and an allocator built on it can only
 *         return memory to the kernel from the top down. Cheap, and the right
 *         primitive for a malloc's small allocations.
 *
 *   mmap  hands out an independent run somewhere below the stack, which munmap
 *         releases on its own whatever else has happened since. The right
 *         primitive for one large buffer that outlives the allocations around
 *         it - a text editor's file buffer, which is what this release exists to
 *         make possible.
 *
 * Both hand back zeroed pages, and that is a security property rather than a
 * convenience: a frame the allocator has just handed out holds whatever its last
 * owner left in it, and giving a program that memory unzeroed is giving it
 * another process's data.
 *
 * Neither keeps a list of what it has handed out. The page tables already record
 * exactly that, they are already walked by fork() and by process teardown, and a
 * second record of the same facts is a second record to keep in step. What that
 * costs is a walk rather than a lookup when mmap() picks an address, which on a
 * machine with sixteen processes is not a cost worth a subsystem.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "registers.h"
#include "stdio.h"
#include "process.h"
#include "paging.h"
#include "pmm.h"
#include "errno.h"
#include "klog.h"
#include "uaccess.h"

/*
 * Source of zeroes for a freshly mapped page.
 *
 * The ELF loader keeps its own copy of this for the same purpose. Sharing one
 * would mean exporting it from a file whose job is loading programs, and 4 KB of
 * read-only data is a smaller price than that dependency.
 */
static const uint8_t zero_page[PAGE_SIZE] = {0};

/**
 * @brief Reports whether nothing is mapped at a user address.
 *
 * @param page Page-aligned address in the running address space.
 * @return Non-zero when the address has no present translation.
 */
static int page_is_free(uint32_t page) {
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t pde = page >> 22;

    /* No page table for the 4 MB the address falls in means the whole range is
     * untouched, which is the common answer while the region is empty. */
    if ((pd[pde] & 1) == 0) return 1;

    uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
    return (pt[(page >> 12) & 0x3FF] & 1) == 0;
}

/**
 * @brief Maps one zeroed, user-writable page at a virtual address.
 *
 * @param page Page-aligned destination address.
 * @return E_OK, or a negative errno with nothing left mapped.
 */
static int map_zeroed_page(uint32_t page) {
    uint32_t frame = pmm_alloc_frame();
    if (frame == 0xFFFFFFFF) return E_NOMEM;

    if (map_page(page, frame, PAGE_USER_ACCESS) != E_OK) {
        pmm_free_frame(frame);
        return E_NOMEM;
    }

    /*
     * copy_to_user() rather than a plain memset, because this is Ring 0 writing
     * to a user-accessible page and SMAP forbids that outside the window the
     * copy helpers open. The ELF loader clears its segment and stack pages the
     * same way, for the same reason.
     */
    if (copy_to_user((void *)page, zero_page, PAGE_SIZE) != E_OK) {
        pmm_free_frame(frame);
        unmap_page(page);
        return E_FAULT;
    }

    return E_OK;
}

/**
 * @brief Releases every present page in a user address range.
 *
 * pmm_free_frame() drops one owner rather than releasing the frame outright, so
 * a page still shared with a forked child survives this and goes when that child
 * does. Unmapping it here is still correct: what is being given up is this
 * address space's claim on it.
 *
 * @param from Page-aligned first address.
 * @param to   Page-aligned end address, exclusive.
 */
static void release_user_range(uint32_t from, uint32_t to) {
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;

    for (uint32_t page = from; page < to; page += PAGE_SIZE) {
        uint32_t pde = page >> 22;
        if ((pd[pde] & 1) == 0) continue;

        uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
        uint32_t pte = (page >> 12) & 0x3FF;

        if (pt[pte] & 1) {
            pmm_free_frame(pt[pte] & 0xFFFFF000);
        }
        unmap_page(page);
    }
}

/**
 * @brief Finds a free run of pages in the mmap region.
 *
 * Searches downwards from the top of the region. Highest-first means a mapping
 * made early and kept stays near the stack, and the scan for the next one walks
 * over it once rather than through the whole region - and it keeps the growing
 * heap and the descending mappings as far apart as the region allows.
 *
 * When a candidate is blocked, the search resumes at the blocking page rather
 * than one page down: every run that would have contained it fails for the same
 * reason, and stepping through them one at a time is the difference between a
 * scan and a crawl.
 *
 * @param pages Number of contiguous pages required.
 * @return Base address of the run, or 0 when the region cannot hold it.
 */
static uint32_t find_free_run(uint32_t pages) {
    uint32_t span = pages * PAGE_SIZE;
    uint32_t top = USER_MMAP_TOP;

    while (top >= USER_MMAP_FLOOR + span) {
        uint32_t base = top - span;
        uint32_t p = 0;

        while (p < pages && page_is_free(base + (p * PAGE_SIZE))) p++;

        if (p == pages) return base;

        top = base + (p * PAGE_SIZE);
    }
    return 0;
}

/**
 * @brief Syscall handler for moving the program break.
 *
 * Raw brk semantics: the resulting break comes back, whether or not it is the
 * one that was asked for. A caller finds out it failed by comparing. That also
 * makes `brk(0)` the way to read the current break - zero can never be granted,
 * so the unchanged value comes back - which is the idiom sbrk() is built on and
 * the reason there is no second syscall for it.
 *
 * @param regs Saved register state; ebx holds the requested break.
 */
void sys_brk(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = 0;
        return;
    }

    uint32_t start = current_task->brk_start;
    uint32_t current = current_task->brk_current;

    /*
     * A task that was not built from an ELF image has no heap. The idle task and
     * anything the test suite creates by hand land here, and inventing a break
     * for them would put one wherever their address space happened to be empty.
     */
    if (start == 0) {
        regs->eax = 0;
        return;
    }

    /* The break the caller sees is always a page boundary, so an allocator that
     * asks for an odd number of bytes gets the whole page it is really given. */
    uint32_t target = (regs->ebx + PAGE_SIZE - 1) & 0xFFFFF000;

    /* Below its own start, or into the region mmap owns: refused by returning
     * the break unmoved. The upper bound is what keeps the two regions from
     * growing into each other. */
    if (regs->ebx < start || target < start || target > USER_MMAP_FLOOR) {
        regs->eax = current;
        return;
    }

    if (target == current) {
        regs->eax = current;
        return;
    }

    if (target < current) {
        release_user_range(target, current);
        current_task->brk_current = target;
        regs->eax = target;
        return;
    }

    uint32_t page = current;
    int failed = 0;

    while (page < target) {
        if (map_zeroed_page(page) != E_OK) {
            failed = 1;
            break;
        }
        page += PAGE_SIZE;
    }

    if (failed) {
        /*
         * All or nothing. A partial growth would leave pages mapped that the
         * break does not cover, and the next successful brk() would map over
         * them - map_page() would refuse the conflict, and a heap that cannot
         * grow past a hole it left itself is worse than one that failed
         * cleanly.
         */
        release_user_range(current, page);
        klog_int(LOG_LEVEL_WARN, "MM", "brk: could not grow the heap, pages short",
                 (int)((target - page) / PAGE_SIZE));
        regs->eax = current;
        return;
    }

    current_task->brk_current = target;
    regs->eax = target;
}

/**
 * @brief Syscall handler for mapping anonymous, private, zeroed pages.
 *
 * @param regs Saved register state; ebx holds the length, ecx is reserved.
 */
void sys_mmap(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = 0xFFFFFFFF;
        return;
    }

    uint32_t length = regs->ebx;

    if (length == 0) {
        regs->eax = 0xFFFFFFFF;
        return;
    }

    /*
     * The second argument is reserved and has to be zero.
     *
     * Refused rather than ignored. Everything this hands out is private,
     * anonymous and read/write, and a program that asks for something else -
     * a shared mapping, a file behind it, a read-only page - would otherwise
     * receive a mapping with different semantics from the one it requested and
     * find out much later. When there is something to honour here, a program
     * written against today's kernel still asks for zero and still gets what it
     * expects.
     */
    if (regs->ecx != 0) {
        regs->eax = 0xFFFFFFFF;
        return;
    }

    if (length > (USER_MMAP_TOP - USER_MMAP_FLOOR)) {
        regs->eax = 0xFFFFFFFF;
        return;
    }

    uint32_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t base = find_free_run(pages);

    if (base == 0) {
        klog_int(LOG_LEVEL_WARN, "MM", "mmap: no free run in the region, pages", (int)pages);
        regs->eax = 0xFFFFFFFF;
        return;
    }

    uint32_t page = base;
    uint32_t end = base + (pages * PAGE_SIZE);

    while (page < end) {
        if (map_zeroed_page(page) != E_OK) {
            /* Same all-or-nothing rule as brk(): a caller that received a
             * partial mapping would have no way to learn where it stopped. */
            release_user_range(base, page);
            klog_int(LOG_LEVEL_WARN, "MM", "mmap: out of frames, pages requested", (int)pages);
            regs->eax = 0xFFFFFFFF;
            return;
        }
        page += PAGE_SIZE;
    }

    regs->eax = base;
}

/**
 * @brief Syscall handler for releasing pages obtained from mmap.
 *
 * @param regs Saved register state; ebx holds the address, ecx the length.
 */
void sys_munmap(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = E_SRCH;
        return;
    }

    uint32_t addr = regs->ebx;
    uint32_t length = regs->ecx;

    if (length == 0 || (addr & 0xFFF) != 0) {
        regs->eax = E_INVAL;
        return;
    }

    uint32_t span = (length + PAGE_SIZE - 1) & 0xFFFFF000;
    uint32_t end = addr + span;

    /* A length that carries the end past the top of the address space would
     * otherwise make the bounds check below compare a wrapped value. */
    if (end <= addr) {
        regs->eax = E_INVAL;
        return;
    }

    /*
     * Confined to the region mmap hands out, and this is the most important
     * check in the file.
     *
     * Without it this is an arbitrary unmap: a program could pass its own stack,
     * its text, or the heap its allocator is standing on, and the kernel would
     * take the pages away and free the frames. Nothing else would object - the
     * addresses are all legitimately its own - and the fault would arrive
     * later, somewhere else entirely.
     */
    if (addr < USER_MMAP_FLOOR || end > USER_MMAP_TOP) {
        regs->eax = E_INVAL;
        return;
    }

    release_user_range(addr, end);
    regs->eax = E_OK;
}
