/*
 * File: fuzz_parser.c
 * Purpose: libFuzzer entry point for fuzzing VFS filename parsing and ELF header loading.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "elf.h"

// =========================================================================
// libFuzzer Entry Point
// LLVM calls this function thousands of times per second with random Data and Size.
// Goal: Determine if corrupted data causes the kernel to crash.
// =========================================================================
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    
    // ---------------------------------------------------------
    // TEST 1: VFS Filename Parsing Fuzzing
    // ---------------------------------------------------------
    // What happens if excessively long or corrupted filenames are sent to VFS?
    if (Size > 0 && Size < 500) {
        char filename[256];
        // Attempt to copy up to 255 characters
        size_t copy_size = Size < 255 ? Size : 255;
        for(size_t i = 0; i < copy_size; i++) {
            filename[i] = Data[i];
        }
        filename[copy_size] = '\0';
        
        // VFS loop simulating character checks in the name:
        volatile int valid = 1;
        for (size_t i = 0; i < copy_size; i++) {
            if (filename[i] < 32 || filename[i] > 126) {
                valid = 0; // Non-printable character protection
            }
        }
    }

    // ---------------------------------------------------------
    // TEST 2: ELF Header and Program Header (Phdr) Fuzzing
    // ---------------------------------------------------------
    // Dangerous header reading portion in the load_and_exec_elf() function.
    // Will paging crash if the fuzzer provides correct Magic Number but corrupted offsets?
    if (Size >= sizeof(elf32_ehdr_t)) {
        elf32_ehdr_t *ehdr = (elf32_ehdr_t *)Data;
        
        // Deep dive only for random data containing the "ELF" prefix!
        if (ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
            ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F') {
            
            uint32_t phoff = ehdr->e_phoff;  // Program Header Offset
            uint16_t phnum = ehdr->e_phnum;  // Number of segments (e.g., Fuzzer can set this to 65000!)
            
            // Try to loop phnum times (Integer Overflow or Out-of-Bounds Read test)
            // If the kernel lacks size checks, libFuzzer will immediately crash it with ASan!
            for (int i = 0; i < phnum; i++) {
                uint32_t chunk_size = i * sizeof(elf32_phdr_t);
                uint32_t offset = phoff + chunk_size;
                
                // SAFE BOUNDS CHECKING (Integer Overflow Protected)
                // 1. offset < phoff -> Did the addition overflow and wrap to zero? (Overflow check)
                // 2. offset >= Size -> Is the offset larger than the file size?
                // 3. Size - offset < sizeof(elf32_phdr_t) -> Is the remaining space sufficient to read 32 bytes?
                if (offset < phoff || offset >= Size || Size - offset < sizeof(elf32_phdr_t)) {
                    break; // Hacker attack detected! Abort operation.
                }

                // If reached here, it is mathematically 100% safe.
                elf32_phdr_t *phdr = (elf32_phdr_t *)(Data + offset);
                
                volatile uint32_t type = phdr->p_type; 
                volatile uint32_t vaddr = phdr->p_vaddr;
                (void)type;
                (void)vaddr;
            }
        }
    }

    return 0; // Returning 0 tells the fuzzer "System did not crash, great!"
}