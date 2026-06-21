#ifndef KHEAP_H
#define KHEAP_H

#include "types.h"

void init_kheap(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
size_t kmalloc_size(void *ptr);

#endif