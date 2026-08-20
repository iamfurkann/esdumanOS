/*
 * File: test_cow.c
 * Purpose: Copy-on-write - the pages fork() hands over without copying them.
 *
 * fork() used to duplicate every page of the parent's address space. It no longer
 * does: the child gets the parent's frames, both sides lose write access, and the
 * first write from either of them splits the page. That is a promise made at fork
 * and kept much later, in a page fault handler, and the two halves fail in
 * different ways.
 *
 * The negative assertions are the ones that matter. A copy-on-write that never
 * splits - because a TLB entry was not flushed, or because the reference count
 * said "sole owner" when it was not - passes every content check made straight
 * after the fork and fails only later, as two processes writing over each other.
 * So every split here is checked from *both* sides: the writer saw its own byte,
 * and the other one did not.
 *
 * The frame accounting is checked just as strictly. Sharing must cost nothing and
 * splitting must cost exactly one page, and both are measured rather than assumed.
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
 * is what copy_user_space() shares. Inside directory entry 2, clear of
 * test_lifecycle's window at 0x500000, test_paging's probe at 0x600000 and
 * test_fork's at 0x700000.
 */
#define COW_VADDR       0x00800000u
/** Writable pages mapped and shared per round. */
#define COW_PAGES       2
/** Read-only probe, immediately above them. */
#define COW_RO_VADDR    (COW_VADDR + (COW_PAGES * PAGE_SIZE))
/** Share/teardown rounds for the leak check. */
#define COW_ROUNDS      8

/** Byte written into the parent's pages before sharing. */
#define PARENT_MARK     0x5A
/** Byte written afterwards; must never reach the other side. */
#define CHILD_MARK      0xC3

/**
 * @brief Reads a page table entry out of the running address space.
 *
 * @param vaddr Address whose entry to read.
 * @return The entry, or 0 when the directory entry covering it is absent.
 */
static uint32_t pte_of(uint32_t vaddr) {
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t pde = vaddr >> 22;

    if ((pd[pde] & 1) == 0) return 0;

    uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
    return pt[(vaddr >> 12) & 0x3FF];
}

/**
 * @brief Reads a page table entry out of a foreign address space.
 *
 * Interrupts are masked across the switch for the same reason test_fork.c masks
 * them: the read itself is safe, since everything the kernel touches is in the
 * half every directory shares, but a context switch in the middle would resume a
 * task with somebody else's directory loaded.
 *
 * @param pd_phys Physical address of the directory to read through.
 * @param vaddr   Address whose entry to read.
 * @return The entry as that address space sees it.
 */
static uint32_t pte_foreign(uint32_t pd_phys, uint32_t vaddr) {
    uint32_t orig_cr3, eflags, entry;

    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");
    asm volatile("mov %%cr3, %0" : "=r"(orig_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(pd_phys) : "memory");

    entry = pte_of(vaddr);

    asm volatile("mov %0, %%cr3" :: "r"(orig_cr3) : "memory");
    if (eflags & 0x200) asm volatile("sti");

    return entry;
}

/**
 * @brief Reads one byte out of a foreign address space.
 *
 * @param pd    Physical address of the directory to read through.
 * @param vaddr Address to read.
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
 * @brief Writes one byte into a foreign address space.
 *
 * This is what triggers the split. The store faults - the entry is present and
 * read-only, and CR0.WP makes that apply to Ring 0 as well - and comes back
 * through page_fault_handler() and cow_handle_fault() before completing. Driving
 * it from kernel mode is deliberate: it is the same path a syscall writing into a
 * freshly forked child's buffer takes, and the one that had nothing testing it.
 *
 * @param pd    Physical address of the directory to write through.
 * @param vaddr Address to write.
 * @param value Byte to store.
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
 * @brief Maps the probe pages into the running directory.
 *
 * COW_PAGES writable pages, then one read-only page above them. The read-only
 * one is not decoration: it is the page that must be shared *without* being
 * marked, and getting that wrong turns a real access violation into a silent
 * copy.
 *
 * @return Number of pages mapped; COW_PAGES + 1 on success.
 */
static int map_probe_pages(void) {
    int mapped = 0;

    for (int p = 0; p < COW_PAGES; p++) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == 0xFFFFFFFF) break;

        if (map_page(COW_VADDR + ((uint32_t)p * PAGE_SIZE), frame, PAGE_USER_ACCESS) != 0) {
            pmm_free_frame(frame);
            break;
        }
        ft_memset((void *)(COW_VADDR + ((uint32_t)p * PAGE_SIZE)), PARENT_MARK, PAGE_SIZE);
        mapped++;
    }

    if (mapped == COW_PAGES) {
        uint32_t frame = pmm_alloc_frame();
        if (frame != 0xFFFFFFFF) {
            /* Written through while it is still writable, then remapped without
             * the read/write bit - a fresh frame holds whatever its last owner
             * left in it, and "did this page change" has to mean something. */
            if (map_page(COW_RO_VADDR, frame, PAGE_USER_ACCESS) == 0) {
                ft_memset((void *)COW_RO_VADDR, PARENT_MARK, PAGE_SIZE);
                if (map_page(COW_RO_VADDR, frame, 5) == 0) {
                    mapped++;
                } else {
                    unmap_page(COW_RO_VADDR);
                    pmm_free_frame(frame);
                }
            } else {
                pmm_free_frame(frame);
            }
        }
    }

    return mapped;
}

/**
 * @brief Releases the probe pages, frames included.
 *
 * @param count Number of pages to release, from the low end.
 */
static void unmap_probe_pages(int count) {
    for (int p = 0; p < count; p++) {
        uint32_t vaddr = COW_VADDR + ((uint32_t)p * PAGE_SIZE);
        uint32_t entry = pte_of(vaddr);

        if (entry & 1) {
            pmm_free_frame(entry & 0xFFFFF000);
        }
        unmap_page(vaddr);
    }
}

/**
 * @brief Exercises the physical allocator's reference counting on its own.
 *
 * Everything else in this file goes through fork's machinery. This does not: it
 * is the layer underneath, and a count that is off by one here is a frame freed
 * while somebody is still using it - the failure copy-on-write was blocked on
 * before reference counting existed.
 */
static void run_refcount_tests(void) {
    uint32_t frame = pmm_alloc_frame();
    KTEST_ASSERT(frame != 0xFFFFFFFF, "[PMM] probe frame allocated");
    if (frame == 0xFFFFFFFF) return;

    KTEST_ASSERT(pmm_frame_refcount(frame) == 1,
                 "[STRICT] [PMM] a freshly allocated frame has exactly one owner");

    pmm_ref_frame(frame);
    pmm_ref_frame(frame);
    KTEST_ASSERT(pmm_frame_refcount(frame) == 3,
                 "[PMM] taking two more references counts three owners");

    uint32_t free_while_held = pmm_get_free_memory();

    pmm_free_frame(frame);
    KTEST_ASSERT(pmm_frame_refcount(frame) == 2, "[PMM] releasing one owner leaves two");
    KTEST_ASSERT(pmm_get_free_memory() == free_while_held,
                 "[STRICT] [PMM] a frame with owners left is not returned to the allocator");

    pmm_free_frame(frame);
    KTEST_ASSERT(pmm_frame_refcount(frame) == 1, "[PMM] releasing another leaves one");
    KTEST_ASSERT(pmm_get_free_memory() == free_while_held,
                 "[STRICT] [PMM] still not returned while one owner holds it");

    pmm_free_frame(frame);
    KTEST_ASSERT(pmm_frame_refcount(frame) == 0, "[PMM] the last release drops the count to zero");
    KTEST_ASSERT(pmm_get_free_memory() == free_while_held + PAGE_SIZE,
                 "[STRICT] [PMM] the last release returns the frame");

    /*
     * And a reference to something the allocator has already taken back must be
     * refused. Granting it would pin a frame that is about to be handed to
     * somebody else, and the count would then describe two unrelated owners.
     */
    pmm_ref_frame(frame);
    KTEST_ASSERT(pmm_frame_refcount(frame) == 0,
                 "[STRICT] [PMM] a frame that is not allocated cannot be shared");
}

/**
 * @brief Verifies that fork shares pages and that writing to one splits it.
 *
 * Expected behavior:
 * - Sharing installs the parent's own frames in the child, and costs no frames.
 * - Both sides come back read-only and marked, and a page that was already
 *   read-only is shared without being marked.
 * - The first write from either side costs exactly one frame and is invisible to
 *   the other.
 * - A page whose other owner has gone is reclaimed without a copy.
 * - Repeated share/teardown rounds return every frame.
 *
 * Edge cases covered:
 * - Writes driven from kernel mode with CR0.WP set, which is the path a syscall
 *   writing into a forked child's buffer takes.
 * - A frame whose reference count is decremented from both sides.
 */
void run_cow_tests(void) {
    printk("\n--- Copy-on-Write Tests ---\n");

    run_refcount_tests();

    int mapped = map_probe_pages();
    KTEST_ASSERT(mapped == COW_PAGES + 1, "[COW] probe pages mapped into the live directory");

    if (mapped == COW_PAGES + 1) {
        uint32_t parent_frame_a = pte_of(COW_VADDR) & 0xFFFFF000;
        uint32_t parent_frame_b = pte_of(COW_VADDR + PAGE_SIZE) & 0xFFFFF000;
        uint32_t ro_frame = pte_of(COW_RO_VADDR) & 0xFFFFF000;

        uint32_t child_pd = clone_page_directory();
        KTEST_ASSERT(child_pd != 0, "[COW] directory cloned for the share");

        if (child_pd != 0) {
            KTEST_ASSERT(copy_user_space(child_pd) == 0, "[COW] copy_user_space reported success");

            /* --------------------------------------------------------------
             * Shared, not copied: the child holds the parent's own frames.
             * -------------------------------------------------------------- */
            KTEST_ASSERT((pte_foreign(child_pd, COW_VADDR) & 0xFFFFF000) == parent_frame_a,
                         "[STRICT] [COW] the child maps the parent's own frame");
            KTEST_ASSERT(pmm_frame_refcount(parent_frame_a) == 2,
                         "[STRICT] [COW] the shared frame records two owners");

            /* --------------------------------------------------------------
             * Both sides lost write access. If either kept it, that side would
             * edit a page the other is still reading - the same bug as sharing
             * outright, only harder to see.
             * -------------------------------------------------------------- */
            uint32_t parent_pte = pte_of(COW_VADDR);
            uint32_t child_pte = pte_foreign(child_pd, COW_VADDR);

            KTEST_ASSERT((parent_pte & 0x02) == 0,
                         "[STRICT] [COW] the parent's entry is read-only after sharing");
            KTEST_ASSERT((parent_pte & PAGE_COW) != 0,
                         "[COW] the parent's entry is marked copy-on-write");
            KTEST_ASSERT((child_pte & 0x02) == 0,
                         "[STRICT] [COW] the child's entry is read-only after sharing");
            KTEST_ASSERT((child_pte & PAGE_COW) != 0,
                         "[COW] the child's entry is marked copy-on-write");

            /* --------------------------------------------------------------
             * A page that was read-only before the share is shared as it
             * stands. Marking it would turn a genuine access violation into a
             * silent private copy, which is a far worse failure than the one
             * the mark exists to prevent.
             * -------------------------------------------------------------- */
            KTEST_ASSERT((pte_of(COW_RO_VADDR) & PAGE_COW) == 0,
                         "[STRICT] [COW] a page that was already read-only is not marked");
            KTEST_ASSERT((pte_foreign(child_pd, COW_RO_VADDR) & PAGE_COW) == 0,
                         "[STRICT] [COW] and it is not marked in the child either");
            KTEST_ASSERT(pmm_frame_refcount(ro_frame) == 2,
                         "[COW] the read-only page is still shared and counted");

            /* --------------------------------------------------------------
             * The child sees the parent's bytes, last one included.
             * -------------------------------------------------------------- */
            int carried = 1;
            for (int p = 0; p < COW_PAGES; p++) {
                uint32_t va = COW_VADDR + ((uint32_t)p * PAGE_SIZE);
                if (peek_foreign(child_pd, va) != PARENT_MARK) carried = 0;
                if (peek_foreign(child_pd, va + PAGE_SIZE - 1) != PARENT_MARK) carried = 0;
            }
            KTEST_ASSERT(carried, "[STRICT] [COW] every shared page reads back the parent's contents");

            /* --------------------------------------------------------------
             * The split, driven from the child. One frame, no more, and the
             * parent must not see it.
             * -------------------------------------------------------------- */
            uint32_t before_split = pmm_get_free_memory();
            poke_foreign(child_pd, COW_VADDR, CHILD_MARK);
            uint32_t after_split = pmm_get_free_memory();

            KTEST_ASSERT(before_split - after_split == PAGE_SIZE,
                         "[STRICT] [COW] splitting a shared page costs exactly one frame");
            KTEST_ASSERT(peek_foreign(child_pd, COW_VADDR) == CHILD_MARK,
                         "[COW] the child's write landed in its own copy");
            KTEST_ASSERT(*(volatile uint8_t *)COW_VADDR == PARENT_MARK,
                         "[STRICT] [COW] the child's write did not reach the parent");
            KTEST_ASSERT(peek_foreign(child_pd, COW_VADDR + 1) == PARENT_MARK,
                         "[COW] the rest of the child's copy carries the original bytes");

            uint32_t split_pte = pte_foreign(child_pd, COW_VADDR);
            KTEST_ASSERT((split_pte & 0xFFFFF000) != parent_frame_a,
                         "[STRICT] [COW] the child now maps a different frame");
            KTEST_ASSERT((split_pte & 0x02) != 0 && (split_pte & PAGE_COW) == 0,
                         "[COW] the split page is writable and no longer marked");
            KTEST_ASSERT(pmm_frame_refcount(parent_frame_a) == 1,
                         "[STRICT] [COW] the frame left behind is down to one owner");

            /* --------------------------------------------------------------
             * The other direction, on the other page: this time the parent
             * writes first, while the count is still two.
             * -------------------------------------------------------------- */
            uint32_t before_parent = pmm_get_free_memory();
            *(volatile uint8_t *)(COW_VADDR + PAGE_SIZE) = CHILD_MARK;
            uint32_t after_parent = pmm_get_free_memory();

            KTEST_ASSERT(before_parent - after_parent == PAGE_SIZE,
                         "[COW] the parent's first write costs one frame too");
            KTEST_ASSERT(peek_foreign(child_pd, COW_VADDR + PAGE_SIZE) == PARENT_MARK,
                         "[STRICT] [COW] the parent's write did not reach the child");
            KTEST_ASSERT(pmm_frame_refcount(parent_frame_b) == 1,
                         "[COW] the page the child kept is down to one owner");

            /* --------------------------------------------------------------
             * Sole owner, still marked: reclaimed in place. This is the case
             * fork()-then-exec() takes, and every write after the first.
             * -------------------------------------------------------------- */
            uint32_t before_reclaim = pmm_get_free_memory();
            *(volatile uint8_t *)COW_VADDR = CHILD_MARK;
            uint32_t after_reclaim = pmm_get_free_memory();

            KTEST_ASSERT(after_reclaim == before_reclaim,
                         "[STRICT] [COW] writing to a page nobody else holds copies nothing");
            KTEST_ASSERT(*(volatile uint8_t *)COW_VADDR == CHILD_MARK,
                         "[COW] and the write went through");

            uint32_t reclaimed_pte = pte_of(COW_VADDR);
            KTEST_ASSERT((reclaimed_pte & 0x02) != 0 && (reclaimed_pte & PAGE_COW) == 0,
                         "[COW] the reclaimed page is writable and no longer marked");
            KTEST_ASSERT((reclaimed_pte & 0xFFFFF000) == parent_frame_a,
                         "[STRICT] [COW] and it is still the same frame, not a copy of it");

            cleanup_process_memory(child_pd);

            /* The child's teardown released its reference to the read-only page,
             * which the parent still holds. */
            KTEST_ASSERT(pmm_frame_refcount(ro_frame) == 1,
                         "[STRICT] [COW] tearing the child down drops its share, not the page");
        }
    }

    unmap_probe_pages(mapped);

    /* ------------------------------------------------------------------
     * Repeated rounds return every frame.
     *
     * A leak of one frame per fork is invisible in a single round and fatal in a
     * shell that forks per command. The baseline is taken after a warm-up round,
     * so the kernel heap has already grown to hold the page list
     * copy_user_space() allocates.
     * ------------------------------------------------------------------ */
    int warm = map_probe_pages();
    if (warm == COW_PAGES + 1) {
        uint32_t pd = clone_page_directory();
        if (pd != 0) {
            copy_user_space(pd);
            cleanup_process_memory(pd);
        }
    }
    unmap_probe_pages(warm);

    uint32_t before = pmm_get_free_memory();
    int rounds_done = 0;

    for (int r = 0; r < COW_ROUNDS; r++) {
        int live = map_probe_pages();
        if (live != COW_PAGES + 1) { unmap_probe_pages(live); break; }

        uint32_t pd = clone_page_directory();
        if (pd == 0) { unmap_probe_pages(live); break; }

        if (copy_user_space(pd) != 0) {
            cleanup_process_memory(pd);
            unmap_probe_pages(live);
            break;
        }

        /* Split one page per round, so the teardown has both a shared frame and
         * a private one to get right. */
        poke_foreign(pd, COW_VADDR, CHILD_MARK);

        cleanup_process_memory(pd);
        unmap_probe_pages(live);
        rounds_done++;
    }

    uint32_t after = pmm_get_free_memory();

    KTEST_ASSERT(rounds_done == COW_ROUNDS, "[COW] every share/split/teardown round completed");
    KTEST_ASSERT(after == before,
                 "[STRICT] [COW] repeated shares and splits leak no physical frames");
}
