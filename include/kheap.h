#ifndef KHEAP_H
#define KHEAP_H

#include "types.h"

/**
 * @brief Initializes the kernel heap allocator.
 * Prepares the underlying data structures for dynamic memory management.
 */
void init_kheap(void);

/**
 * @brief Allocates a block of memory from the kernel heap.
 * 
 * @param size The number of bytes to allocate.
 * @return A pointer to the allocated memory, or NULL if allocation fails.
 */
void *kmalloc(size_t size);

/**
 * @brief Frees a previously allocated memory block back to the kernel heap.
 * 
 * @param ptr Pointer to the memory block to free.
 */
void kfree(void *ptr);

/**
 * @brief Retrieves the size of an allocated memory block.
 * 
 * @param ptr Pointer to the allocated memory block.
 * @return The size of the memory block in bytes.
 */
size_t kmalloc_size(void *ptr);

/**
 * @brief Resizes an existing memory block, preserving its current contents.
 * 
 * @param ptr Pointer to the existing memory block.
 * @param new_size The new requested size in bytes.
 * @return A pointer to the newly resized memory block, or NULL on failure.
 */
void *krealloc(void *ptr, size_t new_size);

#endif