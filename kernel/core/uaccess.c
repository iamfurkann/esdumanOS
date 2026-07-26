#include "uaccess.h"
#include "errno.h"
#include "kernel.h"

// Global or per-cpu variable to hold the address of the current fault handler.
// The page fault handler in isr.c will check this before panicking.
uint32_t current_fault_handler = 0;

int copy_from_user(void *to, const void *from, size_t n) {
    __label__ fixup;
    
    if ((uint32_t)from >= 0xC0000000) return -E_FAULT; // Basic user-space bounds check
    
    char *d = (char *)to;
    const char *s = (const char *)from;
    
    current_fault_handler = (uint32_t)&&fixup;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    
    current_fault_handler = 0;
    return 0;

fixup:
    current_fault_handler = 0;
    return -E_FAULT;
}

int copy_to_user(void *to, const void *from, size_t n) {
    __label__ fixup;
    
    if ((uint32_t)to >= 0xC0000000) return -E_FAULT;
    
    char *d = (char *)to;
    const char *s = (const char *)from;
    
    current_fault_handler = (uint32_t)&&fixup;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    current_fault_handler = 0;
    return 0;

fixup:
    current_fault_handler = 0;
    return -E_FAULT;
}
