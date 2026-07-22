#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "stdio.h"

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
} heap_block_t;

static heap_block_t *heap_head = 0;
static heap_block_t *heap_tail = 0;

#define HEAP_LOCK(flags)   asm volatile ("pushf; pop %0; cli" : "=r"(flags))
#define HEAP_UNLOCK(flags) do { if (flags & 0x200) asm volatile ("sti"); } while(0)

static int heap_grow(size_t size) {
    size_t needed = size + sizeof(heap_block_t);
    size_t pages = (needed + 0xFFF) / 0x1000;
    size_t total_alloc_size = pages * 0x1000;

    extern uint32_t pmm_get_free_memory(void);
    if (total_alloc_size > pmm_get_free_memory()) {
        return 0; // OOM korumasi - frame'leri israf etmeden reddet
    }

    uint32_t start_addr = current_heap_end;

    for (size_t i = 0; i < pages; i++) {
        uint32_t phys_frame = pmm_alloc_frame();
        if (phys_frame == 0xFFFFFFFF) {
            return 0; // OOM
        }
        
        extern int map_page(uint32_t, uint32_t, uint32_t);
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

void init_kheap(void) {
    heap_head = 0;
    heap_tail = 0;
    if (!heap_grow(1)) {
        printk("KERNEL PANIC: Heap baslatilamadi!\n");
        asm volatile("cli; hlt");
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;

    size = (size + 3) & ~3;

    uint32_t eflags;
    HEAP_LOCK(eflags);

    while (1) {
        heap_block_t *curr = heap_head;
        while (curr) {
            if (curr->magic != HEAP_MAGIC_ALLOCATED && curr->magic != HEAP_MAGIC_FREE) {
                HEAP_UNLOCK(eflags);
                printk("\n[KERNEL PANIC] kmalloc: Heap Zinciri Bozuldu (Corruption)!\n");
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
            printk("\n[UYARI] kmalloc: Out of Memory! Fiziksel hafiza doldu.\n");
            return 0;
        }
    }
}

void kfree(void *ptr) {
    if (!ptr) return;

    uint32_t eflags;
    HEAP_LOCK(eflags);

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    
    if (block->magic == HEAP_MAGIC_FREE || block->is_free == 1) {
        HEAP_UNLOCK(eflags);
        printk("\n[KERNEL PANIC] kfree: Cift Serbest Birakma (Double Free) Yasak! Adres: 0x%x\n", (uint32_t)ptr);
        asm volatile("cli; hlt");
        return;
    }

    if (block->magic != HEAP_MAGIC_ALLOCATED) {
        HEAP_UNLOCK(eflags);
        printk("\n[KERNEL PANIC] kfree: Gecersiz gosterici (Invalid Pointer)! Adres: 0x%x\n", (uint32_t)ptr);
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
            extern void unmap_page(uint32_t);
            extern void pmm_free_frame(uint32_t);
            
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
    
    extern void *ft_memcpy(void *dest, const void *src, size_t n);
    ft_memcpy(new_ptr, ptr, old_size); 
    kfree(ptr);
    
    return new_ptr;
}

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