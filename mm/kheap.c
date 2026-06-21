#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "stdio.h"

// Heap'imizi RAM'de izole ve güvenli, yüksek bir sanal adrese yerleştiriyoruz!
#define KHEAP_START_VIRTUAL 0xD0000000 

static uint32_t current_heap_end = KHEAP_START_VIRTUAL;

typedef struct heap_block {
    size_t size;
    int is_free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = 0;

// Heap dolduğunda sanal hafızayı dinamik genişleten fonksiyon (sbrk stili)
static int heap_grow(size_t size) {
    // İhtiyacımız olan 4KB (sayfa) sayısını hesapla
    size_t needed = size + sizeof(heap_block_t);
    size_t pages = (needed + 0xFFF) / 0x1000; // Yukarı yuvarlama
    size_t total_alloc_size = pages * 0x1000;

    uint32_t start_addr = current_heap_end;

    // PMM'den fiziksel sayfaları alıp, VMM üzerinden art arda Sanal adreslere bağla
    for (size_t i = 0; i < pages; i++) {
        uint32_t phys_frame = pmm_alloc_frame();
        if (phys_frame == 0xFFFFFFFF) {
            return 0; // Kernel Panic: Tüm fiziksel RAM bitti!
        }
        
        map_page(current_heap_end, phys_frame, 3); // Present, R/W
        current_heap_end += 0x1000;
    }

    // Yeni eklenen büyük sanal hafıza alanını bir blok olarak ayarla
    heap_block_t *new_block = (heap_block_t *)start_addr;
    new_block->size = total_alloc_size - sizeof(heap_block_t);
    new_block->is_free = 1;
    new_block->next = 0;

    // Linked List'in sonuna ekle
    if (!heap_head) {
        heap_head = new_block;
    } else {
        heap_block_t *curr = heap_head;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = new_block;
    }

    // Ardışık boş bloklar oluştuysa onları tek parça yap (Coalescing)
    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->is_free && curr->next && curr->next->is_free) {
            curr->size += curr->next->size + sizeof(heap_block_t);
            curr->next = curr->next->next;
            continue; 
        }
        curr = curr->next;
    }

    return 1;
}

void init_kheap(void) {
    heap_head = 0;
    // Başlangıçta 1 sayfalık (4 KB) can suyu ver
    if (!heap_grow(1)) {
        printk("KERNEL PANIC: Heap baslatilamadi!\n");
        asm volatile("cli; hlt");
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;

    // 4-byte hizalaması (Alignment) işlemcinin belleği hızlı okuması için şarttır!
    size = (size + 3) & ~3;

    while (1) {
        heap_block_t *curr = heap_head;
        
        while (curr) {
            if (curr->is_free && curr->size >= size) {
                // Blok gereğinden fazlaysa parçala (Split)
                if (curr->size > size + sizeof(heap_block_t) + 4) {
                    heap_block_t *new_block = (heap_block_t *)((uint8_t *)curr + sizeof(heap_block_t) + size);
                    new_block->size = curr->size - size - sizeof(heap_block_t);
                    new_block->is_free = 1;
                    new_block->next = curr->next;

                    curr->size = size;
                    curr->next = new_block;
                }
                curr->is_free = 0;
                return (void *)((uint8_t *)curr + sizeof(heap_block_t));
            }
            curr = curr->next;
        }

        // Eğer buraya ulaştıysak, var olan heap içinde uygun yer yok demektir.
        // Sisteme yeni sayfa ekleyerek (grow) döngüyü tekrar çalıştır.
        if (!heap_grow(size)) {
            printk("KERNEL PANIC: Out of Memory! Fiziksel hafiza doldu.\n");
            return 0;
        }
    }
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    // Boşalan blokları etrafındakilerle birleştir ki hafıza delik deşik olmasın (Coalescing)
    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->is_free && curr->next && curr->next->is_free) {
            curr->size += curr->next->size + sizeof(heap_block_t);
            curr->next = curr->next->next;
            continue;
        }
        curr = curr->next;
    }
}

size_t kmalloc_size(void *ptr) {
    if (!ptr) return 0;
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    return block->size;
}