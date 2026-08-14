/*
 * File: test_lifecycle.c
 * Purpose: Address space lifecycle tests.
 *
 * Exercises the clone -> populate -> tear down cycle that every exec()/exit()
 * pair drives, and asserts that the physical frames come back. Process teardown
 * used to reclaim nothing at all: exit_current_process() called
 * cleanup_process_memory() with the directory that was still in CR3, and the
 * function's own guard turned that into a no-op, so every process permanently
 * leaked its page directory, its page tables and roughly 40 data frames.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"

/** Number of clone/teardown rounds. */
#define LIFECYCLE_ROUNDS   16
/** User pages mapped into each cloned address space. */
#define PAGES_PER_ROUND     8
/**
 * Probe address for the mapped pages. Deliberately inside directory entry 1:
 * user space starts at 4 MB, so entries 1..3 are real user territory, and both
 * the clone and the teardown paths used to mishandle exactly that range.
 */
#define LIFECYCLE_VADDR    0x00500000u

/**
 * @brief Counts present directory entries below the kernel split.
 *
 * Switches to @p pd, walks its directory through the recursive slot and switches
 * back. Nothing may be printed while the foreign directory is live.
 *
 * @param pd Physical address of the directory to inspect.
 * @return Number of present entries in the 0..767 range.
 */
static int count_user_pdes(uint32_t pd) {
    uint32_t orig_cr3;
    int present = 0;

    asm volatile("mov %%cr3, %0" : "=r"(orig_cr3));
    asm volatile("cli");
    asm volatile("mov %0, %%cr3" :: "r"(pd) : "memory");

    volatile uint32_t *pd_virt = (volatile uint32_t *)0xFFFFF000;
    for (int i = 0; i < 768; i++) {
        if (pd_virt[i] & 1) present++;
    }

    asm volatile("mov %0, %%cr3" :: "r"(orig_cr3) : "memory");
    asm volatile("sti");

    return present;
}

/**
 * @brief Maps PAGES_PER_ROUND user pages into a foreign address space.
 *
 * @param pd Physical address of the directory to populate.
 * @return Number of pages successfully mapped.
 */
static int populate_address_space(uint32_t pd) {
    uint32_t orig_cr3;
    int mapped = 0;

    asm volatile("mov %%cr3, %0" : "=r"(orig_cr3));
    asm volatile("cli");
    asm volatile("mov %0, %%cr3" :: "r"(pd) : "memory");

    for (int p = 0; p < PAGES_PER_ROUND; p++) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == 0xFFFFFFFF) break;

        if (map_page(LIFECYCLE_VADDR + ((uint32_t)p * 4096), frame, 7) != 0) {
            pmm_free_frame(frame);
            break;
        }
        mapped++;
    }

    asm volatile("mov %0, %%cr3" :: "r"(orig_cr3) : "memory");
    asm volatile("sti");

    return mapped;
}

/**
 * @brief Verifies that address spaces are created clean and released completely.
 *
 * Expected behavior:
 * - A freshly cloned directory has no present entries below the kernel split.
 * - Populating and tearing an address space down returns every frame, so free
 *   memory after N rounds equals free memory before them.
 * - clone_page_directory() reports failure as 0, never as a negative errno.
 *
 * Edge cases covered:
 * - Mappings inside directory entry 1, the range the teardown scan used to skip.
 * - Repeated rounds, so a per-round leak of even one frame is visible.
 */
void run_lifecycle_tests(void) {
    printk("\n--- Address Space Lifecycle Tests ---\n");

    /*
     * A clone must start empty below the kernel split. Entries 0..3 used to be
     * copied from the caller with the present bit forced on, which pointed them
     * at physical frame 0 and made the real-mode interrupt vector table act as
     * the page table for the first 16 MB of every address space.
     */
    uint32_t probe_pd = clone_page_directory();
    KTEST_ASSERT(probe_pd != 0, "Lifecycle: clone_page_directory returned a directory");

    if (probe_pd != 0) {
        int stray = count_user_pdes(probe_pd);
        KTEST_ASSERT(stray == 0, "[STRICT] Lifecycle: fresh address space has no present user-space PDEs");
        cleanup_process_memory(probe_pd);
    }

    /* Baseline is taken after the probe so its own frame is already accounted. */
    uint32_t before = pmm_get_free_memory();

    int rounds_done = 0;
    int pages_mapped = 0;

    for (int r = 0; r < LIFECYCLE_ROUNDS; r++) {
        uint32_t pd = clone_page_directory();
        if (pd == 0) break;

        pages_mapped += populate_address_space(pd);
        cleanup_process_memory(pd);
        rounds_done++;
    }

    uint32_t after = pmm_get_free_memory();

    KTEST_ASSERT(rounds_done == LIFECYCLE_ROUNDS,
                 "Lifecycle: every clone/teardown round completed");
    KTEST_ASSERT(pages_mapped == LIFECYCLE_ROUNDS * PAGES_PER_ROUND,
                 "Lifecycle: user pages mapped into each cloned address space");
    KTEST_ASSERT(after == before,
                 "[STRICT] Lifecycle: teardown reclaims every frame (no leak)");

    /*
     * Guard the CR3 contract directly: the live address space must never be
     * torn down, and neither must the kernel directory.
     */
    uint32_t live_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(live_cr3));

    uint32_t guard_before = pmm_get_free_memory();
    cleanup_process_memory(live_cr3);
    cleanup_process_memory(0);
    uint32_t guard_after = pmm_get_free_memory();

    KTEST_ASSERT(guard_after == guard_before,
                 "[STRICT] Lifecycle: teardown refuses the live and the null directory");
}
