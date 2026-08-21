/*
 * File: test_umem.c
 * Purpose: User-space dynamic memory - the break, anonymous mappings, and the
 *          bounds that keep them apart from everything else.
 *
 * The Ring 3 payload exercises all of this the way a program does, through
 * umalloc(). What it cannot do is count frames exactly - it reads free memory in
 * kilobytes - or reach a task that has no ELF image behind it. Both live here.
 *
 * The assertion that matters most in this file is a refusal. munmap() takes an
 * address and a length from user space and hands the pages back to the
 * allocator; if it did not insist the range lie inside the region mmap owns, a
 * program could pass its own stack, its own text, or the heap its allocator is
 * standing on, and the kernel would take those pages away. Every address
 * involved is legitimately the caller's own, so nothing further down would
 * object, and the fault would surface somewhere else entirely and much later.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "libft.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "errno.h"

extern void sys_brk(arch_regs_t *regs);
extern void sys_mmap(arch_regs_t *regs);
extern void sys_munmap(arch_regs_t *regs);

/**
 * Where the synthetic task's heap is told to begin. Clear of every other
 * module's probe window: 0x500000 is the shared scratch range, 0x600000 is
 * test_paging, 0x700000 test_fork and 0x800000 test_cow.
 */
#define UMEM_BRK_START  0x00900000u

/** Rounds of map/unmap for the leak check. */
#define UMEM_ROUNDS     8

/**
 * @brief Calls sys_brk() and returns the resulting break.
 *
 * @param request Requested break, as the syscall would receive it in ebx.
 * @return The break the kernel reports.
 */
static uint32_t call_brk(uint32_t request) {
    arch_regs_t regs;

    ft_memset(&regs, 0, sizeof(regs));
    regs.ebx = request;
    sys_brk(&regs);
    return regs.eax;
}

/**
 * @brief Calls sys_mmap().
 *
 * @param length Requested length in bytes.
 * @param flags  Reserved argument, passed through as ecx.
 * @return The mapped address, or 0xFFFFFFFF.
 */
static uint32_t call_mmap(uint32_t length, uint32_t flags) {
    arch_regs_t regs;

    ft_memset(&regs, 0, sizeof(regs));
    regs.ebx = length;
    regs.ecx = flags;
    sys_mmap(&regs);
    return regs.eax;
}

/**
 * @brief Calls sys_munmap().
 *
 * @param addr   Address to release.
 * @param length Length in bytes.
 * @return E_OK, or a negative errno.
 */
static int call_munmap(uint32_t addr, uint32_t length) {
    arch_regs_t regs;

    ft_memset(&regs, 0, sizeof(regs));
    regs.ebx = addr;
    regs.ecx = length;
    sys_munmap(&regs);
    return (int)regs.eax;
}

/**
 * @brief Reports whether every byte of a page is zero.
 *
 * @param page Address of a mapped page.
 * @return Non-zero when the page is entirely zero.
 */
static int page_is_zero(uint32_t page) {
    const volatile uint8_t *p = (const volatile uint8_t *)page;

    for (uint32_t i = 0; i < PAGE_SIZE; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

/**
 * @brief Verifies the break and anonymous mappings, and the bounds on both.
 *
 * Expected behavior:
 * - A task with no ELF image has no heap and is refused rather than given one.
 * - brk() reports the resulting break, so a refusal is the break unmoved - which
 *   is what makes brk(0) a way to read it.
 * - Growing costs exactly the pages it covers, and they arrive zeroed.
 * - mmap() lands inside its own region, page-aligned, zeroed, and never on top
 *   of a mapping it has already handed out.
 * - munmap() refuses any range outside that region.
 * - Repeated map/unmap rounds return every frame.
 *
 * Edge cases covered:
 * - A break below its own start, and one that would run into the mmap region.
 * - A reserved mmap argument that is not zero.
 * - An unaligned munmap address, a zero length, and a length that runs off the
 *   top of the region.
 */
void run_umem_tests(void) {
    printk("\n--- User Memory (brk / mmap) Tests ---\n");

    if (current_task == 0) {
        KTEST_ASSERT(0, "[UMEM] a task context is required for these tests");
        return;
    }

    uint32_t saved_start = current_task->brk_start;
    uint32_t saved_current = current_task->brk_current;

    /* ------------------------------------------------------------------
     * A task with no image has no heap.
     *
     * The synthetic task these modules run against was never built from an ELF,
     * so its break is zero - the same state the idle task is in. Inventing one
     * would put a heap wherever that address space happened to be empty.
     * ------------------------------------------------------------------ */
    current_task->brk_start = 0;
    current_task->brk_current = 0;
    KTEST_ASSERT(call_brk(0x900000) == 0,
                 "[STRICT] [UMEM] brk is refused for a task with no program image");

    current_task->brk_start = UMEM_BRK_START;
    current_task->brk_current = UMEM_BRK_START;

    /* ------------------------------------------------------------------
     * Reading the break without moving it.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(call_brk(0) == UMEM_BRK_START,
                 "[UMEM] brk(0) reports the current break without moving it");

    /* ------------------------------------------------------------------
     * Growing: exactly the frames it covers, and zeroed.
     *
     * A warm-up first, and it is not decoration. The first page mapped into a
     * 4 MB span makes map_page() allocate the page table covering it, and
     * neither shrinking nor unmapping ever gives that table back - so the first
     * page into a fresh span costs two frames and every page after it costs
     * one. The measurements here are about pages, so the table has to be there
     * before the baseline is taken.
     *
     * In a full run test_cow has already opened this one, which is exactly why
     * the warm-up matters: without it these assertions would pass in CI and
     * fail for anyone running `make test_kernel MODULE=umem` on its own.
     * ------------------------------------------------------------------ */
    call_brk(UMEM_BRK_START + PAGE_SIZE);
    call_brk(UMEM_BRK_START);

    uint32_t before_grow = pmm_get_free_memory();
    uint32_t grown = call_brk(UMEM_BRK_START + (2 * PAGE_SIZE));
    uint32_t after_grow = pmm_get_free_memory();

    KTEST_ASSERT(grown == UMEM_BRK_START + (2 * PAGE_SIZE),
                 "[UMEM] brk grows to the requested break");
    KTEST_ASSERT(before_grow - after_grow == 2 * PAGE_SIZE,
                 "[STRICT] [UMEM] growing by two pages costs exactly two frames");

    if (grown == UMEM_BRK_START + (2 * PAGE_SIZE)) {
        /*
         * Zeroed, and this is a security property rather than a courtesy: a
         * frame the allocator just handed over holds whatever its last owner
         * left in it, and a heap page that arrives unzeroed is another
         * process's memory delivered to this one.
         */
        KTEST_ASSERT(page_is_zero(UMEM_BRK_START) && page_is_zero(UMEM_BRK_START + PAGE_SIZE),
                     "[STRICT] [UMEM] new heap pages arrive zeroed");

        *(volatile uint8_t *)UMEM_BRK_START = 0x5A;
        *(volatile uint8_t *)(UMEM_BRK_START + (2 * PAGE_SIZE) - 1) = 0xA5;
        KTEST_ASSERT(*(volatile uint8_t *)UMEM_BRK_START == 0x5A &&
                     *(volatile uint8_t *)(UMEM_BRK_START + (2 * PAGE_SIZE) - 1) == 0xA5,
                     "[UMEM] the whole grown range is writable");
    }

    /* ------------------------------------------------------------------
     * Refusals report the break unmoved, which is how the caller finds out.
     * ------------------------------------------------------------------ */
    uint32_t held = current_task->brk_current;

    KTEST_ASSERT(call_brk(UMEM_BRK_START - PAGE_SIZE) == held,
                 "[STRICT] [UMEM] a break below its own start is refused");
    KTEST_ASSERT(call_brk(USER_MMAP_FLOOR + PAGE_SIZE) == held,
                 "[STRICT] [UMEM] a break that would reach the mmap region is refused");
    KTEST_ASSERT(current_task->brk_current == held,
                 "[UMEM] a refused break really did not move");

    /* ------------------------------------------------------------------
     * Shrinking gives the frames back.
     * ------------------------------------------------------------------ */
    uint32_t before_shrink = pmm_get_free_memory();
    KTEST_ASSERT(call_brk(UMEM_BRK_START) == UMEM_BRK_START, "[UMEM] brk shrinks back to its start");
    KTEST_ASSERT(pmm_get_free_memory() - before_shrink == 2 * PAGE_SIZE,
                 "[STRICT] [UMEM] shrinking returns the frames it had taken");

    /* ------------------------------------------------------------------
     * mmap: inside its own region, aligned, zeroed.
     *
     * Warmed up for the same reason as the break above, and here nothing else
     * in the suite reaches this part of the address space - the mmap region
     * lives just under the stack guard, and no other module maps anything
     * there. So the first mapping always pays for a page table, in a full run
     * as much as a filtered one.
     * ------------------------------------------------------------------ */
    uint32_t warm_map = call_mmap(PAGE_SIZE, 0);
    if (warm_map != 0xFFFFFFFF) call_munmap(warm_map, PAGE_SIZE);

    uint32_t before_map = pmm_get_free_memory();
    uint32_t one = call_mmap(1, 0);
    uint32_t after_map = pmm_get_free_memory();

    KTEST_ASSERT(one != 0xFFFFFFFF, "[UMEM] mmap of one byte succeeds");
    KTEST_ASSERT(one >= USER_MMAP_FLOOR && one < USER_MMAP_TOP,
                 "[STRICT] [UMEM] the mapping lands inside the mmap region");
    KTEST_ASSERT((one & 0xFFF) == 0, "[UMEM] the mapping is page-aligned");
    KTEST_ASSERT(before_map - after_map == PAGE_SIZE,
                 "[STRICT] [UMEM] a one-byte request costs exactly one frame");

    if (one != 0xFFFFFFFF) {
        KTEST_ASSERT(page_is_zero(one), "[STRICT] [UMEM] a mapped page arrives zeroed");

        /* A second mapping must not land on the first. */
        uint32_t two = call_mmap(2 * PAGE_SIZE, 0);
        KTEST_ASSERT(two != 0xFFFFFFFF, "[UMEM] a second mapping succeeds");

        if (two != 0xFFFFFFFF) {
            int overlaps = (two < one + PAGE_SIZE) && (one < two + (2 * PAGE_SIZE));
            KTEST_ASSERT(!overlaps,
                         "[STRICT] [UMEM] a second mapping does not overlap the first");
            KTEST_ASSERT(call_munmap(two, 2 * PAGE_SIZE) == E_OK, "[UMEM] the second mapping is released");
        }

        /* ------------------------------------------------------------------
         * The refusals. Every one of these is a range the caller has no
         * business handing to munmap, and the addresses are all otherwise
         * legitimate - which is exactly why the check has to be here.
         * ------------------------------------------------------------------ */
        KTEST_ASSERT(call_munmap(UMEM_BRK_START, PAGE_SIZE) == E_INVAL,
                     "[STRICT] [UMEM] munmap refuses an address below the mmap region");
        KTEST_ASSERT(call_munmap(USER_STACK_TOP - PAGE_SIZE, PAGE_SIZE) == E_INVAL,
                     "[STRICT] [UMEM] munmap refuses the user stack");
        KTEST_ASSERT(call_munmap(USER_MMAP_TOP - PAGE_SIZE, 2 * PAGE_SIZE) == E_INVAL,
                     "[STRICT] [UMEM] munmap refuses a range running off the top of the region");
        KTEST_ASSERT(call_munmap(one + 1, PAGE_SIZE) == E_INVAL,
                     "[UMEM] munmap refuses an unaligned address");
        KTEST_ASSERT(call_munmap(one, 0) == E_INVAL,
                     "[UMEM] munmap refuses a zero length");

        /* None of those refusals may have taken anything away. */
        KTEST_ASSERT(page_is_zero(one),
                     "[STRICT] [UMEM] a refused munmap left the mapping intact");

        uint32_t before_unmap = pmm_get_free_memory();
        KTEST_ASSERT(call_munmap(one, PAGE_SIZE) == E_OK, "[UMEM] munmap releases the mapping");
        KTEST_ASSERT(pmm_get_free_memory() - before_unmap == PAGE_SIZE,
                     "[STRICT] [UMEM] munmap returns exactly the frame it took");
    }

    /* ------------------------------------------------------------------
     * The reserved argument, and a length of nothing.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(call_mmap(PAGE_SIZE, 1) == 0xFFFFFFFF,
                 "[STRICT] [UMEM] mmap refuses a reserved argument it cannot honour");
    KTEST_ASSERT(call_mmap(0, 0) == 0xFFFFFFFF, "[UMEM] mmap refuses a zero length");
    KTEST_ASSERT(call_mmap(USER_MMAP_TOP - USER_MMAP_FLOOR + 1, 0) == 0xFFFFFFFF,
                 "[UMEM] mmap refuses a length larger than the region");

    /* ------------------------------------------------------------------
     * Repeated rounds return every frame.
     *
     * No warm-up needed here: the page table covering this end of the region
     * was allocated by the first mapping above and never handed back, and every
     * round below lands in the same 4 MB span.
     * ------------------------------------------------------------------ */
    uint32_t before_rounds = pmm_get_free_memory();
    int rounds = 0;

    for (int r = 0; r < UMEM_ROUNDS; r++) {
        uint32_t addr = call_mmap(3 * PAGE_SIZE, 0);
        if (addr == 0xFFFFFFFF) break;
        if (call_munmap(addr, 3 * PAGE_SIZE) != E_OK) break;
        rounds++;
    }

    KTEST_ASSERT(rounds == UMEM_ROUNDS, "[UMEM] every map/unmap round completed");
    KTEST_ASSERT(pmm_get_free_memory() == before_rounds,
                 "[STRICT] [UMEM] repeated mappings leak no physical frames");

    /* Leave the synthetic task exactly as it was found: later modules see the
     * same pristine PCB, and a stray break would outlive this file. */
    current_task->brk_start = saved_start;
    current_task->brk_current = saved_current;
}
