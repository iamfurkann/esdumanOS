#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/**
 * @brief Virtual address for fractal (recursive) mapping of the Page Directory.
 * Allows the Page Directory to map itself to easily modify page tables.
 */
#define RECURSIVE_PD_VADDR 0xFFFFF000

/**
 * @brief Starting virtual address for the Page Tables when using recursive mapping.
 */
#define RECURSIVE_PT_VADDR 0xFFC00000

/**
 * @brief Top of the user-mode (Ring 3) stack.
 *
 * This read 0xBFFFF000 and nothing used it. The ELF loader has always built the
 * stack at 0xB0000000 with its own literal, and the memory map in the README
 * documents that address, so the one constant naming the boundary was the only
 * place that disagreed with everyone else. Anything laid out relative to the
 * stack - which is now both of the regions below - has to start from the real
 * figure.
 */
#define USER_STACK_TOP     0xB0000000

/**
 * @brief Number of stack pages the loader maps below USER_STACK_TOP.
 */
#define USER_STACK_PAGES   32

/**
 * @brief The unmapped page below the stack that catches an overflow.
 */
#define USER_STACK_GUARD   (USER_STACK_TOP - ((USER_STACK_PAGES + 1) * PAGE_SIZE))

/**
 * @brief Highest address mmap() will hand out, exclusive.
 *
 * Directly below the stack guard page, and mmap() searches downwards from here.
 */
#define USER_MMAP_TOP      USER_STACK_GUARD

/**
 * @brief Lowest address mmap() will hand out, and the ceiling on the brk heap.
 *
 * The two regions grow towards each other, so something has to stop them from
 * meeting. A fixed split rather than a moving boundary: it costs address space
 * that a 128 MB machine cannot use anyway, and it means neither region's bounds
 * depend on what the other has done. mmap gets 0xA0000000 upwards to the guard
 * page, roughly 256 MB; the heap gets everything from the end of the program's
 * image up to here.
 *
 * Clear of 0x80000000, which the idle task's page uses in the kernel directory.
 */
#define USER_MMAP_FLOOR    0xA0000000

/**
 * @brief Temporary mapping virtual address used during address space cloning.
 */
#define TEMP_MAP_VADDR     0xE0000000

/**
 * @brief The window device registers and DMA buffers are mapped into.
 *
 * Kernel space is 0xC0000000 upwards and most of it is empty: the first 16 MB of
 * physical memory sits at 0xC0000000, the kernel heap grows upwards from
 * 0xD0000000, TEMP_MAP_VADDR takes a single page at 0xE0000000, and the
 * recursive page tables live at the very top. Everything from here to
 * DEVICE_WINDOW_END was unclaimed, and a driver needs somewhere to put a
 * controller's registers that is not backed by RAM.
 *
 * 16 MB, which is far more than anything this kernel will map - an AHCI
 * controller's register file is under 8 KB - and it costs nothing but address
 * space on a machine that has 128 MB of memory and 4 GB of addresses.
 */
#define DEVICE_WINDOW_BASE 0xE1000000

/** @brief One past the last address vmm_map_device() will hand out. */
#define DEVICE_WINDOW_END  0xE2000000

/**
 * @brief Standard page size (4 KB).
 */
#ifndef PAGE_SIZE
#define PAGE_SIZE          4096
#endif

/**
 * @brief Page table/directory flags.
 * PAGE_KERNEL_ONLY: Present=1, Read/Write=1, User=0. Accessible only by kernel.
 */
#define PAGE_KERNEL_ONLY   3

/**
 * @brief Page table/directory flags.
 * PAGE_USER_ACCESS: Present=1, Read/Write=1, User=1. Accessible by user-mode processes.
 */
#define PAGE_USER_ACCESS   7

/**
 * @brief Page table/directory flags.
 * PAGE_NOT_PRESENT: Present=0, Read/Write=1. Page is swapped out or unmapped.
 */
#define PAGE_NOT_PRESENT   2

/**
 * @brief Marks a page that is shared until somebody writes to it.
 *
 * Bit 9 of a page table entry is one of the three the processor ignores and
 * leaves to the operating system, so it rides along in the entry itself rather
 * than in a table beside it.
 *
 * The bit means: this mapping is read-only *because it is shared*, not because
 * the page is meant to be read-only. That distinction is the whole mechanism. A
 * write fault on a page carrying this bit is resolved by giving the writer a
 * private copy; a write fault on a page without it - a program's text, say - is
 * a real access violation and still kills the process.
 *
 * Only ever set on entries that were writable before they were shared.
 */
#define PAGE_COW           0x200

/**
 * @brief Kernel mapping for memory that is not memory.
 *
 * Present, writable, kernel-only - and with PWT (bit 3) and PCD (bit 4) set, so
 * the processor neither caches reads from it nor lets writes to it sit in a
 * cache line waiting to be flushed.
 *
 * That is not an optimisation choice, it is the difference between a driver that
 * works and one that does not. A control register mapped write-back accepts a
 * write into the cache and the device never sees it; a status register read the
 * same way answers with whatever the cache remembers rather than what the device
 * is saying now. The failure has no symptom other than the device ignoring
 * everything, which is why it is worth a named constant rather than a literal at
 * the call site.
 *
 * map_page() needs no change to honour this: it already passes flags through as
 * (flags & 0xFFF), and both bits are inside that mask.
 */
#define PAGE_KERNEL_MMIO   0x1B

/**
 * @brief Initializes the paging subsystem.
 * Sets up the kernel page directory and enables hardware paging.
 */
void init_paging(void);

/**
 * @brief Maps a physical address to a virtual address with specified flags.
 * 
 * @param virtual_addr The virtual address to map.
 * @param physical_addr The physical address to back the virtual address.
 * @param flags The access flags for the page (e.g., PAGE_KERNEL_ONLY, PAGE_USER_ACCESS).
 * @return 0 on success, or a negative error code on failure.
 */
int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);

/**
 * @brief Maps a physical region into the device window, uncached.
 *
 * For a controller's register file, and for the buffers handed to a controller
 * that reads memory on its own. kmalloc() cannot serve the second of those: it
 * maps one page at a time from wherever the physical allocator happened to be,
 * so a heap block is contiguous in virtual addresses and scattered in physical
 * ones, and a device given the address of the first page would walk into
 * somebody else's.
 *
 * @param phys Physical base. Need not be page aligned; the offset within the
 *             page is preserved in the returned pointer.
 * @param size Bytes required from @p phys.
 * @return A kernel pointer to @p phys, or 0 when the window is exhausted or the
 *         mapping was refused.
 */
void *vmm_map_device(uint32_t phys, uint32_t size);

/**
 * @brief Unmaps a previously mapped virtual address.
 * Invalidates the TLB entry to ensure memory consistency.
 * 
 * @param virtual_addr The virtual address to unmap.
 */
void unmap_page(uint32_t virtual_addr);

/**
 * @brief Loads the page directory base address into the CR3 register.
 * 
 * @param dir Physical address of the page directory.
 */
extern void load_page_directory(uint32_t* dir);

/**
 * @brief Enables the paging mechanism by setting the PG bit in the CR0 register.
 */
extern void enable_paging(void);


// --- Added by Refactor Script ---
extern uint32_t *page_directory;

/**
 * @brief Creates a new address space sharing the kernel half of the current one.
 *
 * @return Physical address of the new page directory, or 0 on failure. The
 *         result is loaded straight into CR3, so failure must be 0 rather than
 *         a negative errno.
 */
extern uint32_t clone_page_directory(void);

/**
 * @brief Gives a cloned address space the caller's user pages, shared until written.
 *
 * The other half of what fork() needs: clone_page_directory() produces an address
 * space with nothing below the kernel split, and this fills it in. The child ends
 * up seeing exactly the caller's memory and owning none of it yet — every shared
 * frame gains a reference, and every entry that was writable on either side is
 * marked read-only and PAGE_COW. The first write to one of those splits it.
 *
 * This used to duplicate every page outright, which fork() paid for in full even
 * though the overwhelmingly common next call is exec() and throws the copy away.
 * The teardown path is what made that necessary: cleanup_process_memory() freed
 * every user frame it found unconditionally, so a shared frame would have been
 * released twice. Reference counting in the physical allocator is what removed
 * that obstacle.
 *
 * @param dst_pd Physical address of a directory from clone_page_directory().
 * @return E_OK, or E_NOMEM if a page table could not be allocated. References
 *         taken for entries that were not installed are dropped here; the
 *         installed ones belong to @p dst_pd and go with
 *         cleanup_process_memory().
 */
int copy_user_space(uint32_t dst_pd);

/**
 * @brief Resolves a write fault on a shared page by handing over a private copy.
 *
 * Runs in the faulting task's own address space — a page fault does not change
 * CR3 — so the recursive mapping reaches exactly the tables that produced the
 * fault.
 *
 * Three outcomes, and the caller needs all three apart:
 *  - the page carries no PAGE_COW, so this is somebody else's fault to handle;
 *  - the page was shared and now is not, and the faulting instruction should be
 *    retried;
 *  - the page is shared and there is no memory to split it with, which is not an
 *    access violation and must not be reported as one.
 *
 * A frame with only one reference left needs no copy at all: the other owner is
 * already gone, and clearing the bit hands the page back to its last holder.
 * That is the case fork()-then-exec() and every second write take.
 *
 * @param faulting_addr Contents of CR2.
 * @return 1 if the fault was resolved, 0 if the page is not copy-on-write,
 *         -1 if it is and the copy could not be made.
 */
int cow_handle_fault(uint32_t faulting_addr);

/**
 * @brief Releases every frame owned by an address space, then the directory.
 *
 * Frees the user-space page tables (directory entries 0..767) and the frames
 * they map, and finally the directory itself. Kernel entries (768..1022) are
 * shared with every other address space and are left alone.
 *
 * Must not be called on the address space currently in CR3; the caller has to
 * switch away first. Process teardown therefore defers this to the zombie
 * reaper in schedule(), which runs once another task's directory is live.
 *
 * @param cr3 Physical address of the page directory to release.
 */
extern void cleanup_process_memory(uint32_t cr3);

#endif