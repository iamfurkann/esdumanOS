/*
 * File: kheap.c
 * Purpose: Kernel Heap implementation for dynamic memory allocation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "stdio.h"
#include "libft.h"

#define KHEAP_START_VIRTUAL 0xD0000000 
#define HEAP_MAGIC_ALLOCATED 0xDEADBEEF
#define HEAP_MAGIC_FREE      0xFEEDFACE 

static uint32_t current_heap_end = KHEAP_START_VIRTUAL;

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    int is_free;
    struct heap_block *next;
    struct heap_block *prev;
    uint32_t padding[3]; // Pad to 32 bytes for 16-byte alignment
} heap_block_t;

static heap_block_t *heap_head = 0;
static heap_block_t *heap_tail = 0;

#define HEAP_LOCK(flags)   asm volatile ("pushf; pop %0; cli" : "=r"(flags))
#define HEAP_UNLOCK(flags) do { if (flags & 0x200) asm volatile ("sti"); } while(0)

/**
 * @brief Expands the kernel heap by a given size.
 * 
 * @param size The additional size needed in bytes.
 * @return 1 on success, 0 on failure (OOM).
 */
static int heap_grow(size_t size) {
    size_t needed = size + sizeof(heap_block_t);
    size_t pages = (needed + 0xFFF) / 0x1000;
    size_t total_alloc_size = pages * 0x1000;
if (total_alloc_size > pmm_get_free_memory()) {
        return 0; // OOM protection - reject without wasting frames
    }

    uint32_t start_addr = current_heap_end;

    for (size_t i = 0; i < pages; i++) {
        uint32_t phys_frame = pmm_alloc_frame();
        if (phys_frame == 0xFFFFFFFF) {
            for (size_t j = 0; j < i; j++) {
                uint32_t vaddr = start_addr + j * 0x1000;
                uint32_t pd_index = vaddr >> 22;
                uint32_t pt_index = (vaddr >> 12) & 0x3FF;
                uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
                if (pt_virt[pt_index] & 1) {
                    pmm_free_frame(pt_virt[pt_index] & 0xFFFFF000);
                    pt_virt[pt_index] = 0; // Clear the PTE
                    asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory"); // Flush TLB
                }
            }
            return 0; // OOM
        }
map_page(current_heap_end, phys_frame, 3);
        current_heap_end += 0x1000;
    }

    heap_block_t *new_block = (heap_block_t *)start_addr;
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->size = total_alloc_size - sizeof(heap_block_t);
    new_block->is_free = 1;
    new_block->next = 0;
    new_block->prev = heap_tail;

    if (!heap_head) {
        heap_head = new_block;
        heap_tail = new_block;
    } else {
        heap_tail->next = new_block;
        new_block->prev = heap_tail;

        if (heap_tail->is_free) {
            uint32_t tail_end_addr = (uint32_t)heap_tail + sizeof(heap_block_t) + heap_tail->size;
            if (tail_end_addr == (uint32_t)new_block) {
                heap_tail->size += total_alloc_size;
                heap_tail->next = 0;
            } else {
                heap_tail = new_block;
            }
        } else {
            heap_tail = new_block;
        }
    }

    return 1;
}

/**
 * @brief Initializes the kernel heap.
 */
void init_kheap(void) {
    heap_head = 0;
    heap_tail = 0;
    if (!heap_grow(1)) {
        printk("KERNEL PANIC: Heap initialization failed!\n");
        asm volatile("cli; hlt");
    }
}

/**
 * @brief Allocates dynamic memory from the kernel heap.
 * 
 * @param size The number of bytes to allocate.
 * @return A pointer to the allocated memory, or 0 if out of memory.
 */
void *kmalloc(size_t size) {
    if (size == 0) return 0;

    // Align allocated size to 16 bytes to guarantee memory alignment for SSE/FPU
    size = (size + 15) & ~15;

    uint32_t eflags;
    HEAP_LOCK(eflags);

    while (1) {
        heap_block_t *curr = heap_head;
        while (curr) {
            if (curr->magic != HEAP_MAGIC_ALLOCATED && curr->magic != HEAP_MAGIC_FREE) {
                HEAP_UNLOCK(eflags);
                printk("\n[KERNEL PANIC] kmalloc: Heap chain corrupted!\n");
                asm volatile("cli; hlt");
            }

            if (curr->is_free && curr->size >= size) {
                if (curr->size > size + sizeof(heap_block_t) + 4) {
                    heap_block_t *new_block = (heap_block_t *)((uint8_t *)curr + sizeof(heap_block_t) + size);
                    
                    new_block->magic = HEAP_MAGIC_FREE;
                    new_block->size = curr->size - size - sizeof(heap_block_t);
                    new_block->is_free = 1;
                    
                    new_block->next = curr->next;
                    new_block->prev = curr;
                    
                    if (curr->next) curr->next->prev = new_block;
                    else heap_tail = new_block;

                    curr->size = size;
                    curr->next = new_block;
                }
                
                curr->is_free = 0;
                curr->magic = HEAP_MAGIC_ALLOCATED;
                
                HEAP_UNLOCK(eflags);
                return (void *)((uint8_t *)curr + sizeof(heap_block_t));
            }
            curr = curr->next;
        }

        if (!heap_grow(size)) {
            HEAP_UNLOCK(eflags);
            return 0;
        }
    }
}

/**
 * @brief Frees previously allocated dynamic memory.
 * 
 * @param ptr Pointer to the memory to free.
 */
void kfree(void *ptr) {
    if (!ptr) return;

    uint32_t eflags;
    HEAP_LOCK(eflags);

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    
    if (block->magic == HEAP_MAGIC_FREE || block->is_free == 1) {
        HEAP_UNLOCK(eflags);
        printk("\n[KERNEL PANIC] kfree: Double free detected! Address: 0x%x\n", (uint32_t)ptr);
        asm volatile("cli; hlt");
        return;
    }

    if (block->magic != HEAP_MAGIC_ALLOCATED) {
        HEAP_UNLOCK(eflags);
        printk("\n[KERNEL PANIC] kfree: Invalid pointer! Address: 0x%x\n", (uint32_t)ptr);
        asm volatile("cli; hlt");
        return;
    }

    block->is_free = 1;
    block->magic = HEAP_MAGIC_FREE;

    if (block->next && block->next->is_free) {
        block->size += block->next->size + sizeof(heap_block_t);
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
        else heap_tail = block; 
    }
    if (block->prev && block->prev->is_free) {
        heap_block_t *prev_block = block->prev;
        prev_block->size += block->size + sizeof(heap_block_t);
        prev_block->next = block->next;
        if (block->next) block->next->prev = prev_block;
        else heap_tail = prev_block;
        
        block = prev_block;
    }
    if (block == heap_tail && block->size >= 4096) {
        uint32_t pages_to_free = block->size / 4096;
        
        for (uint32_t i = 0; i < pages_to_free; i++) {
            current_heap_end -= 4096;
            uint32_t pd_index = current_heap_end >> 22;
            uint32_t pt_index = (current_heap_end >> 12) & 0x3FF;
            uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
            if (pt_virt[pt_index] & 1) {
                pmm_free_frame(pt_virt[pt_index] & 0xFFFFF000);
            }
            unmap_page(current_heap_end);
        }
        
        block->size -= (pages_to_free * 4096);
        if (block->size < sizeof(heap_block_t)) {
            if (block->prev) {
                block->prev->next = 0;
                heap_tail = block->prev;
            } else {
                heap_head = 0;
                heap_tail = 0;
            }
        }
    }
    
    HEAP_UNLOCK(eflags);
}

/**
 * @brief Reallocates a previously allocated memory block with a new size.
 * 
 * @param ptr Pointer to the original memory block.
 * @param new_size The new size in bytes.
 * @return A pointer to the newly allocated memory, or 0 if out of memory.
 */
void *krealloc(void *ptr, size_t new_size) {
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    uint32_t eflags;
    HEAP_LOCK(eflags);
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC_ALLOCATED) {
        HEAP_UNLOCK(eflags);
        return 0;
    }
    
    if (block->size >= new_size) {
        HEAP_UNLOCK(eflags);
        return ptr;
    }

    size_t old_size = block->size;
    HEAP_UNLOCK(eflags);

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
ft_memcpy(new_ptr, ptr, old_size); 
    kfree(ptr);
    
    return new_ptr;
}

/**
 * @brief Gets the size of an allocated memory block.
 * 
 * @param ptr Pointer to the allocated memory.
 * @return The size of the memory block in bytes.
 */
size_t kmalloc_size(void *ptr) {
    if (!ptr) return 0;
    
    uint32_t eflags;
    HEAP_LOCK(eflags);
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    
    if (block->magic != HEAP_MAGIC_ALLOCATED) {
        HEAP_UNLOCK(eflags);
        return 0; 
    }
    
    size_t size = block->size;
    HEAP_UNLOCK(eflags);
    
    return size;
}