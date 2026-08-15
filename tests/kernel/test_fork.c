/*
 * File: test_fork.c
 * Purpose: Address space duplication - the half of fork() that copies memory.
 *
 * clone_page_directory() has always produced an address space with nothing at all
 * below the kernel split; exec() then populated it from an ELF. fork() cannot do
 * that, because what the child needs is not a program image but whatever its
 * parent happens to be holding. copy_user_space() is that step, and this file is
 * the proof that a copy is a copy: the child gets the parent's bytes, and writing
 * to one of them afterwards does not reach the other.
 *
 * The assertions that matter most are the negative ones. A "copy" that quietly
 * shared frames would pass every content check here and fail only later, as
 * two processes corrupting each other, or as a double free during teardown.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "kheap.h"
#include "libft.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"

/**
 * Probe address for the pages this module maps into the *live* directory, which
 * is what copy_user_space() reads. Inside directory entry 1, so it is real user
 * territory, and clear of both the 0x500000..0x503000 window run_all_selftests()
 * maps for other modules and test_paging's probe at 0x600000.
 */
#define FORK_VADDR      0x00700000u
/** Pages mapped and copied per round. */
#define FORK_PAGES      3
/** Create/copy/destroy rounds for the leak check. */
#define FORK_ROUNDS     8

/** Byte written into the parent's pages before copying. */
#define PARENT_MARK     0x5A
/** Byte written into the child's copy afterwards; must not reach the parent. */
#define CHILD_MARK      0xC3

/**
 * @brief Fills the live mapping at FORK_VADDR with a known byte.
 *
 * @param value Byte to write into every page.
 */
static void mark_live_pages(uint8_t value) {
    for (int p = 0; p < FORK_PAGES; p++) {
        ft_memset((void *)(FORK_VADDR + ((uint32_t)p * PAGE_SIZE)), value, PAGE_SIZE);
    }
}

/**
 * @brief Reads one byte out of a foreign address space.
 *
 * Switches to @p pd, reads, and switches back. Interrupts are masked across the
 * switch: the read itself is safe - kernel text, stack and data are in the half
 * every directory shares - but a context switch in the middle would resume a task
 * with someone else's directory loaded.
 *
 * @param pd     Physical address of the directory to read through.
 * @param vaddr  Address to read.
 * @return The byte at @p vaddr in that address space.
 */
static uint8_t peek_foreign(uint32_t pd, uint32_t vaddr) {
    uint32_t orig_cr3, eflags;
    uint8_t value;

    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");
    asm volatile("mov %%cr3, %0" : "=r"(orig_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(pd) : "memory");

    value = *(volatile uint8_t *)vaddr;

    asm volatile("mov %0, %%cr3" :: "r"(orig_cr3) : "memory");
    if (eflags & 0x200) asm volatile("sti");

    return value;
}

/**
 * @brief Writes one byte into a foreign address space. See peek_foreign().
 *
 * @param pd     Physical address of the directory to write through.
 * @param vaddr  Address to write.
 * @param value  Byte to store.
 */
static void poke_foreign(uint32_t pd, uint32_t vaddr, uint8_t value) {
    uint32_t orig_cr3, eflags;

    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");
    asm volatile("mov %%cr3, %0" : "=r"(orig_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(pd) : "memory");

    *(volatile uint8_t *)vaddr = value;

    asm volatile("mov %0, %%cr3" :: "r"(orig_cr3) : "memory");
    if (eflags & 0x200) asm volatile("sti");
}

/**
 * @brief Maps FORK_PAGES fresh user pages into the running directory.
 *
 * @return Number of pages mapped; FORK_PAGES on success.
 */
static int map_live_pages(void) {
    int mapped = 0;

    for (int p = 0; p < FORK_PAGES; p++) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == 0xFFFFFFFF) break;

        if (map_page(FORK_VADDR + ((uint32_t)p * PAGE_SIZE), frame, PAGE_USER_ACCESS) != 0) {
            pmm_free_frame(frame);
            break;
        }
        mapped++;
    }
    return mapped;
}

/**
 * @brief Releases the pages map_live_pages() installed, frames included.
 *
 * @param count Number of pages to release, from the low end.
 */
static void unmap_live_pages(int count) {
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;

    for (int p = 0; p < count; p++) {
        uint32_t vaddr = FORK_VADDR + ((uint32_t)p * PAGE_SIZE);
        uint32_t pde = vaddr >> 22;
        uint32_t pte = (vaddr >> 12) & 0x3FF;

        if ((pd[pde] & 1) == 0) continue;

        uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
        if (pt[pte] & 1) {
            pmm_free_frame(pt[pte] & 0xFFFFF000);
        }
        unmap_page(vaddr);
    }
}

/**
 * @brief Verifies that a duplicated address space is a copy and not a sharing.
 *
 * Expected behavior:
 * - A clone populated by copy_user_space() reads back the parent's bytes.
 * - Writing through the clone does not change the parent, and vice versa.
 * - A clone of an address space with nothing mapped below the split succeeds and
 *   copies nothing.
 * - Repeated copy/teardown rounds return every frame.
 *
 * Edge cases covered:
 * - Mappings inside directory entry 1, the range both the clone and the teardown
 *   paths have mishandled before.
 * - A null destination, which must be refused rather than written through.
 */
void run_fork_tests(void) {
    printk("\n--- Address Space Copy Tests ---\n");

    KTEST_ASSERT(copy_user_space(0) != 0,
                 "[STRICT] [FORK] copy_user_space refuses a null destination");

    int mapped = map_live_pages();
    KTEST_ASSERT(mapped == FORK_PAGES, "[FORK] probe pages mapped into the live directory");

    if (mapped == FORK_PAGES) {
        mark_live_pages(PARENT_MARK);

        uint32_t child_pd = clone_page_directory();
        KTEST_ASSERT(child_pd != 0, "[FORK] directory cloned for the copy");

        if (child_pd != 0) {
            int res = copy_user_space(child_pd);
            KTEST_ASSERT(res == 0, "[FORK] copy_user_space reported success");

            /* ------------------------------------------------------------------
             * The child sees the parent's bytes.
             * ------------------------------------------------------------------ */
            int all_carried = 1;
            for (int p = 0; p < FORK_PAGES; p++) {
                uint32_t va = FORK_VADDR + ((uint32_t)p * PAGE_SIZE);
                if (peek_foreign(child_pd, va) != PARENT_MARK) all_carried = 0;
                /* The last byte too - a copy that stopped short would pass a
                 * check that only ever read offset zero. */
                if (peek_foreign(child_pd, va + PAGE_SIZE - 1) != PARENT_MARK) all_carried = 0;
            }
            KTEST_ASSERT(all_carried,
                         "[STRICT] [FORK] every copied page carries the parent's contents");

            /* ------------------------------------------------------------------
             * And they are separate pages, which is the whole point.
             *
             * A copy_user_space() that installed the parent's own frames into the
             * child would satisfy every assertion above. It fails here, and it
             * would otherwise fail much later - as two processes writing over each
             * other, or as a double free when the second of them exits.
             * ------------------------------------------------------------------ */
            poke_foreign(child_pd, FORK_VADDR, CHILD_MARK);
            KTEST_ASSERT(peek_foreign(child_pd, FORK_VADDR) == CHILD_MARK,
                         "[FORK] the child's copy is writable");
            KTEST_ASSERT(*(volatile uint8_t *)FORK_VADDR == PARENT_MARK,
                         "[STRICT] [FORK] writing through the child does not reach the parent");

            *(volatile uint8_t *)(FORK_VADDR + 1) = CHILD_MARK;
            KTEST_ASSERT(peek_foreign(child_pd, FORK_VADDR + 1) == PARENT_MARK,
                         "[STRICT] [FORK] writing through the parent does not reach the child");

            cleanup_process_memory(child_pd);
        }
    }

    unmap_live_pages(mapped);

    /* ------------------------------------------------------------------
     * An empty user half copies nothing and reports success.
     *
     * fork() from a task with no user mappings is not a case the shell will
     * produce, but it is the boundary of the loop and a count of zero has to mean
     * "nothing to do" rather than "allocate nothing and then walk it".
     * ------------------------------------------------------------------ */
    uint32_t empty_pd = clone_page_directory();
    if (empty_pd != 0) {
        KTEST_ASSERT(copy_user_space(empty_pd) == 0,
                     "[FORK] copying an address space with no user pages succeeds");
        cleanup_process_memory(empty_pd);
    }

    /* ------------------------------------------------------------------
     * Repeated rounds return every frame.
     *
     * The measurement v0.5.0 rests on: fork() copies whole address spaces, and a
     * leak of one frame per child is invisible in any single round. The baseline
     * is taken after a warm-up round so the kernel heap has already grown to hold
     * the page list copy_user_space() allocates.
     * ------------------------------------------------------------------ */
    int warm = map_live_pages();
    if (warm == FORK_PAGES) {
        uint32_t pd = clone_page_directory();
        if (pd != 0) {
            copy_user_space(pd);
            cleanup_process_memory(pd);
        }
    }
    unmap_live_pages(warm);

    uint32_t before = pmm_get_free_memory();
    int rounds_done = 0;

    for (int r = 0; r < FORK_ROUNDS; r++) {
        int live = map_live_pages();
        if (live != FORK_PAGES) { unmap_live_pages(live); break; }

        uint32_t pd = clone_page_directory();
        if (pd == 0) { unmap_live_pages(live); break; }

        if (copy_user_space(pd) != 0) {
            cleanup_process_memory(pd);
            unmap_live_pages(live);
            break;
        }

        cleanup_process_memory(pd);
        unmap_live_pages(live);
        rounds_done++;
    }

    uint32_t after = pmm_get_free_memory();

    KTEST_ASSERT(rounds_done == FORK_ROUNDS, "[FORK] every copy/teardown round completed");
    KTEST_ASSERT(after == before,
                 "[STRICT] [FORK] repeated address space copies leak no physical frames");
}
