#ifndef UMALLOC_H
#define UMALLOC_H

/*
 * File: umalloc.h
 * Purpose: A dynamic allocator for user-space programs, in a header.
 *
 * Every program in apps/bin is a single translation unit compiled with
 * -nostdlib, defining whatever helpers it needs itself. There is no user-space
 * libc to put an allocator in and no link step that would find one, so it lives
 * here and each program takes a private copy by including it. That is the same
 * bargain the programs already make with their string helpers.
 *
 * Self-contained on purpose. It does not call the program's own syscall()
 * wrapper - if it did, this header would only compile when included below the
 * wrapper's definition, and a header with an ordering requirement is a trap
 * waiting for whoever adds the next program. It brings its own.
 *
 * Two sources of memory, chosen by size:
 *
 *   Small requests come out of the program break, a single run growing upwards
 *   from the end of the image. Freed blocks go on a list and are reused; the
 *   break itself never comes back down, because a run can only be returned from
 *   the top and the top is rarely the part you finished with.
 *
 *   Requests of UMALLOC_MMAP_MIN or more get their own anonymous mapping, which
 *   goes back to the kernel exactly when it is freed. One large buffer that
 *   outlives the small allocations around it - a file being edited, say - is the
 *   case this split exists for.
 *
 * Both kinds arrive zeroed, because the kernel zeroes every page it hands out.
 * ucalloc() still clears what it returns: a reused block from the free list has
 * whatever the last owner left in it, and only the kernel's pages are fresh.
 */

/**
 * @brief Payload alignment, and the size of a block header.
 *
 * Sixteen bytes satisfies every scalar this architecture has, including the SSE
 * loads the FPU state is saved with. The header is exactly this size so that a
 * run of blocks starting on a page boundary keeps every payload aligned.
 */
#define UMALLOC_ALIGN       16u

/**
 * @brief At or above this size, an allocation gets its own mapping.
 *
 * Low enough that a program's one big buffer lands on the mmap side, high enough
 * that ordinary small allocations never pay for a syscall and a fresh set of
 * page tables.
 */
#define UMALLOC_MMAP_MIN    (64u * 1024u)

/**
 * @brief Marks a block header as one of ours.
 *
 * A pointer that did not come from umalloc() reaches ufree() as a header with
 * something else in this field, and is ignored rather than followed. The kernel
 * heap makes the same check and panics; a program has no business panicking, so
 * this returns quietly.
 */
#define UMALLOC_MAGIC       0xC0FFEE01u

/**
 * @brief One allocation's bookkeeping, immediately below its payload.
 */
typedef struct umalloc_block {
    unsigned int magic;
    unsigned int size;              /**< Payload bytes, always a multiple of UMALLOC_ALIGN. */
    struct umalloc_block *next;     /**< Next block on the break heap, in address order. */
    unsigned char is_free;
    unsigned char from_mmap;        /**< Has its own mapping; not on the list. */
    unsigned char reserved[2];
} umalloc_block_t;

/** Head of the break heap's block list; mmap blocks are never on it. */
static umalloc_block_t *umalloc_head = 0;

/**
 * @brief Issues a system call. A private copy; see the note at the top.
 *
 * @param num  System call number.
 * @param a1   First argument.
 * @param a2   Second argument.
 * @param a3   Third argument.
 * @return The kernel's result.
 */
static inline int umalloc_syscall(int num, int a1, int a2, int a3) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(a1), "c"(a2), "d"(a3)
                 : "memory");
    return ret;
}

/**
 * @brief Rounds a size up to the payload alignment.
 *
 * @param n Requested bytes.
 * @return @p n rounded up to a multiple of UMALLOC_ALIGN.
 */
static inline unsigned int umalloc_round(unsigned int n) {
    return (n + (UMALLOC_ALIGN - 1u)) & ~(UMALLOC_ALIGN - 1u);
}

/**
 * @brief Reports whether two list neighbours are also neighbours in memory.
 *
 * The list is in address order and the break heap is one continuous run, so this
 * should always hold. It is checked anyway, because merging two blocks that are
 * not adjacent produces a block whose recorded size does not describe the memory
 * it covers, and every later split inherits that.
 *
 * @param a Lower block.
 * @param b The block after it in the list.
 * @return Non-zero when @p b begins exactly where @p a ends.
 */
static inline int umalloc_adjacent(umalloc_block_t *a, umalloc_block_t *b) {
    return ((unsigned char *)a + sizeof(umalloc_block_t) + a->size) == (unsigned char *)b;
}

/**
 * @brief Splits a block if the remainder can hold a header and something useful.
 *
 * @param b    Block to split, already large enough for @p need.
 * @param need Payload the caller asked for, already rounded.
 */
static inline void umalloc_split(umalloc_block_t *b, unsigned int need) {
    if (b->size < need + sizeof(umalloc_block_t) + UMALLOC_ALIGN) return;

    umalloc_block_t *rest =
        (umalloc_block_t *)((unsigned char *)b + sizeof(umalloc_block_t) + need);

    rest->magic = UMALLOC_MAGIC;
    rest->size = b->size - need - (unsigned int)sizeof(umalloc_block_t);
    rest->next = b->next;
    rest->is_free = 1;
    rest->from_mmap = 0;

    b->size = need;
    b->next = rest;
}

/**
 * @brief Extends the break and appends the new space as one free block.
 *
 * The kernel rounds the break up to a page, so more usually arrives than was
 * asked for. The block takes all of it: the surplus becomes free space the next
 * small allocation can use, rather than a gap between the break and the last
 * block that nothing could ever reach.
 *
 * @param need Payload required, already rounded.
 * @param tail Last block on the list, or 0 when the list is empty.
 * @return The new block, or 0 when the kernel refused to move the break.
 */
static inline umalloc_block_t *umalloc_grow(unsigned int need, umalloc_block_t *tail) {
    unsigned int total = need + (unsigned int)sizeof(umalloc_block_t);

    /* brk(0) can never be granted, so the break comes back unmoved: that is how
     * you read it. Zero means this task has no heap at all. */
    unsigned int cur = (unsigned int)umalloc_syscall(56, 0, 0, 0);
    if (cur == 0) return 0;

    unsigned int got = (unsigned int)umalloc_syscall(56, (int)(cur + total), 0, 0);
    if (got < cur + total) return 0;

    umalloc_block_t *b = (umalloc_block_t *)cur;
    b->magic = UMALLOC_MAGIC;
    b->size = (got - cur) - (unsigned int)sizeof(umalloc_block_t);
    b->next = 0;
    b->is_free = 0;
    b->from_mmap = 0;

    if (tail) tail->next = b;
    else umalloc_head = b;

    return b;
}

/**
 * @brief Allocates memory.
 *
 * @param size Bytes required.
 * @return Pointer to at least @p size bytes, or 0.
 */
static inline void *umalloc(unsigned int size) {
    if (size == 0) return 0;

    unsigned int need = umalloc_round(size);

    /* Guard the rounding and the header addition against a size so large that
     * they wrap - which would otherwise turn a refusal into a tiny allocation
     * the caller believes is huge. */
    if (need < size || need + sizeof(umalloc_block_t) < need) return 0;

    if (need + sizeof(umalloc_block_t) >= UMALLOC_MMAP_MIN) {
        unsigned int total = need + (unsigned int)sizeof(umalloc_block_t);
        unsigned int addr = (unsigned int)umalloc_syscall(57, (int)total, 0, 0);

        if (addr == 0xFFFFFFFFu) return 0;

        umalloc_block_t *b = (umalloc_block_t *)addr;
        b->magic = UMALLOC_MAGIC;
        b->size = need;
        b->next = 0;
        b->is_free = 0;
        b->from_mmap = 1;
        return (void *)(b + 1);
    }

    umalloc_block_t *cur = umalloc_head;
    umalloc_block_t *tail = 0;

    while (cur) {
        if (cur->is_free && cur->size >= need) {
            umalloc_split(cur, need);
            cur->is_free = 0;
            return (void *)(cur + 1);
        }
        tail = cur;
        cur = cur->next;
    }

    umalloc_block_t *fresh = umalloc_grow(need, tail);
    if (!fresh) return 0;

    umalloc_split(fresh, need);
    return (void *)(fresh + 1);
}

/**
 * @brief Releases memory obtained from umalloc().
 *
 * A block with its own mapping goes back to the kernel here. One from the break
 * heap joins the free list and is merged with whichever of its neighbours are
 * also free - both directions, which is why the predecessor is looked up rather
 * than remembered: a backward link in every header would cost more, on every
 * allocation, than this walk costs on the few that are freed.
 *
 * @param ptr Pointer returned by umalloc(), or 0.
 */
static inline void ufree(void *ptr) {
    if (!ptr) return;

    umalloc_block_t *b = ((umalloc_block_t *)ptr) - 1;
    if (b->magic != UMALLOC_MAGIC || b->is_free) return;

    if (b->from_mmap) {
        unsigned int total = b->size + (unsigned int)sizeof(umalloc_block_t);
        b->magic = 0;
        umalloc_syscall(58, (int)(unsigned int)b, (int)total, 0);
        return;
    }

    b->is_free = 1;

    while (b->next && b->next->is_free && umalloc_adjacent(b, b->next)) {
        b->size += (unsigned int)sizeof(umalloc_block_t) + b->next->size;
        b->next = b->next->next;
    }

    umalloc_block_t *prev = umalloc_head;
    while (prev && prev->next != b) prev = prev->next;

    if (prev && prev->is_free && umalloc_adjacent(prev, b)) {
        prev->size += (unsigned int)sizeof(umalloc_block_t) + b->size;
        prev->next = b->next;
    }
}

/**
 * @brief Allocates zeroed memory for an array.
 *
 * @param n    Number of elements.
 * @param size Size of one element.
 * @return Zeroed memory, or 0.
 */
static inline void *ucalloc(unsigned int n, unsigned int size) {
    if (n != 0 && size > 0xFFFFFFFFu / n) return 0;

    unsigned int total = n * size;
    unsigned char *p = (unsigned char *)umalloc(total);
    if (!p) return 0;

    /* Pages from the kernel arrive zeroed, but a block reused from the free list
     * carries whatever its last owner left. */
    for (unsigned int i = 0; i < total; i++) p[i] = 0;
    return (void *)p;
}

/**
 * @brief Resizes an allocation, preserving its contents.
 *
 * @param ptr  Existing allocation, or 0.
 * @param size New size in bytes.
 * @return The resized allocation, or 0 with @p ptr untouched.
 */
static inline void *urealloc(void *ptr, unsigned int size) {
    if (!ptr) return umalloc(size);
    if (size == 0) { ufree(ptr); return 0; }

    umalloc_block_t *b = ((umalloc_block_t *)ptr) - 1;
    if (b->magic != UMALLOC_MAGIC) return 0;

    if (b->size >= umalloc_round(size)) return ptr;

    unsigned char *fresh = (unsigned char *)umalloc(size);
    if (!fresh) return 0;

    unsigned char *old = (unsigned char *)ptr;
    for (unsigned int i = 0; i < b->size; i++) fresh[i] = old[i];

    ufree(ptr);
    return (void *)fresh;
}

#endif // UMALLOC_H
