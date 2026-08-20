/*
 * File: elf.c
 * Purpose: Parses and loads 32-bit ELF executables into memory for process execution.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "elf.h"
#include "fs.h"
#include "stdio.h"
#include "libft.h"
#include "process.h"
#include "paging.h"
#include "pipe.h"
#include "crypto.h"
#include "security.h"
#include "errno.h"
#include  "klog.h"
#include "kheap.h"
#include "pmm.h"
#include "uaccess.h"

static const uint8_t zero_page[4096] = {0};
/**
 * @brief Loads and executes a 32-bit ELF file.
 *
 * @param filename Path of the ELF file to load.
 * @param parent_id ID of the parent directory.
 * @return The task index on success, or a negative error code on failure.
 */
int load_and_exec_elf(const char *filename, uint8_t parent_id)
{
    /*
     * LOCKDOWN is documented as blocking new task creation, but nothing
     * enforced it - set_security_level() only zeroed the key and logged. This
     * is the choke point every new program passes through.
     */
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN)
    {
        klog(LOG_LEVEL_WARN, "ELF", "Refused to start a new program: system is in LOCKDOWN.");
        return E_ACCES;
    }

    if (!check_free_task_slot())
    {
        klog(LOG_LEVEL_ERROR, "ELF", "Maximum task limit reached!");
        return E_AGAIN;
    }

    vfs_file_t file;
    if (fs_open(filename, parent_id, &file) != E_OK)
    {
        klog(LOG_LEVEL_ERROR, "ELF", "File not found on disk!");
        return E_NOENT;
    }

    uint8_t *file_buffer = (uint8_t *)kmalloc(file.file_size);
    if (!file_buffer)
    {
        klog(LOG_LEVEL_ERROR, "ELF", "Failed to allocate RAM for ELF loader!");
        return E_NOMEM;
    }

    int read_bytes = fs_read(&file, file_buffer, file.file_size);
    if (read_bytes < (int)sizeof(elf32_ehdr_t))
    {
        klog(LOG_LEVEL_ERROR, "ELF", "File read error or file too small!");
        kfree(file_buffer);
        return E_IO;
    }

    if (elf_validate_image(file_buffer, (uint32_t)read_bytes) != E_OK) {
        klog(LOG_LEVEL_ERROR, "ELF", "Malformed or unsupported ELF image rejected!");
        kfree(file_buffer);
        return E_NOEXEC;
    }

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)file_buffer;

    uint32_t new_pd = clone_page_directory();
    if (!new_pd) {
        klog(LOG_LEVEL_ERROR, "ELF", "Failed to clone page directory!");
        kfree(file_buffer);
        return E_NOMEM;
    }

    uint32_t eflags;
    asm volatile("pushfl; popl %0; cli" : "=r"(eflags)); 
    
    uint32_t original_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(original_cr3) :: "memory");
    asm volatile("mov %0, %%cr3" :: "r"(new_pd) : "memory");

    for (int i = 0; i < ehdr->e_phnum; i++) {
        uint32_t chunk_size = i * sizeof(elf32_phdr_t);
        uint32_t offset = ehdr->e_phoff + chunk_size;
        
        if (offset < ehdr->e_phoff || offset >= (uint32_t)read_bytes || (uint32_t)read_bytes - offset < sizeof(elf32_phdr_t)) {
            printk("[ELF LOADER] CRITICAL ERROR: Corrupted ELF header (Phdr)!\n");
            goto cleanup_and_fail;
        }

        elf32_phdr_t *phdr = (elf32_phdr_t *)(file_buffer + offset);

        if (phdr->p_type == 1) {
            uint32_t start_addr = phdr->p_vaddr;
            uint32_t end_addr = phdr->p_vaddr + phdr->p_memsz;
            
            // [CRITICAL SECURITY PATCH]: User-Space Address Validation
            // No program can be loaded below 0x400000 (4MB) or above 0xC0000000 (3GB)!
            if (start_addr < 0x400000 || end_addr > 0xC0000000 || end_addr < start_addr) {
                printk("[ELF SECURITY] Error: Invalid load address (0x%x). Cannot write to kernel space!\n", start_addr);
                goto cleanup_and_fail;
            }

            /*
             * Load every segment writable, whatever its final permissions are.
             * CR0.WP makes the read/write bit apply to Ring 0 too, so the
             * kernel cannot fill a page it has already mapped read-only. The
             * real permissions are applied in the pass after this loop, once
             * all the segment data is in place.
             */
            uint32_t load_flags = 7; // Present | User | Writable

            // Page-by-page mapping loop
            for (uint32_t page = (start_addr & 0xFFFFF000); page < end_addr; page += 4096) {
                uint32_t pd_index = page >> 22;
                uint32_t pt_index = (page >> 12) & 0x3FF;
                uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
                
                int is_mapped = 0;
                uint32_t *pt_virt = (uint32_t *)0;
                
                if (pd_virt[pd_index] & 1) {
                    pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
                    if (pt_virt[pt_index] & 1) {
                        is_mapped = 1;
                    }
                }

                if (!is_mapped) {
                    uint32_t phys = pmm_alloc_frame();
                    if (phys == 0xFFFFFFFF) {
                        printk("[ELF LOADER] Out of physical memory loading a segment!\n");
                        goto cleanup_and_fail;
                    }

                    /*
                     * Separated from the || chain these three used to share.
                     *
                     * A frame that is allocated and then fails to map belongs to
                     * nobody. It is not in the directory, so the
                     * cleanup_process_memory() on the failure path cannot find
                     * it, and nothing hands it back either - one frame lost per
                     * failed exec, permanently. The chain also collapsed three
                     * different failures into one message that named only the
                     * middle one.
                     */
                    if (map_page(page, phys, load_flags) != E_OK) {
                        pmm_free_frame(phys);
                        printk("[ELF LOADER] Failed to map a user program page!\n");
                        goto cleanup_and_fail;
                    }

                    /* Mapped, so the frame is the directory's now and the
                     * teardown below reclaims it with the rest. */
                    if (copy_to_user((void *)page, zero_page, sizeof(zero_page)) != E_OK) {
                        printk("[ELF LOADER] Failed to clear a user program page!\n");
                        goto cleanup_and_fail;
                    }
                } else if (pt_virt) {
                    /* Shared with a segment already loaded: it has to be
                     * writable for this segment's copy as well. */
                    pt_virt[pt_index] |= 0x02;
                    asm volatile("invlpg (%0)" ::"r"(page) : "memory");
                }
            }
            
            if (phdr->p_filesz > 0) {
                if (copy_to_user((void *)phdr->p_vaddr, file_buffer + phdr->p_offset, phdr->p_filesz) != E_OK) {
                    printk("[ELF LOADER] Failed to copy program data to user memory!\n");
                    goto cleanup_and_fail;
                }
            }
            if (phdr->p_memsz > phdr->p_filesz) {
                uint32_t bss_start = phdr->p_vaddr + phdr->p_filesz;
                uint32_t bss_size = phdr->p_memsz - phdr->p_filesz;

                while (bss_size > 0) {
                    uint32_t chunk = bss_size > sizeof(zero_page) ? sizeof(zero_page) : bss_size;
                    if (copy_to_user((void *)bss_start, zero_page, chunk) != E_OK) {
                        printk("[ELF LOADER] Failed to zero user BSS!\n");
                        goto cleanup_and_fail;
                    }
                    bss_start += chunk;
                    bss_size -= chunk;
                }
            }
        }
    }
    goto load_success;

cleanup_and_fail:
    asm volatile("mov %0, %%cr3" :: "r"(original_cr3) : "memory");
    if (eflags & 0x200) asm volatile("sti" ::: "memory");
    kfree(file_buffer);
    
    // Call custom method to clean up the aborted Page Directory
cleanup_process_memory(new_pd);
    klog(LOG_LEVEL_ERROR, "ELF", "Task aborted, memory cleared.");
    return E_NOEXEC;

load_success:

    /*
     * Apply the real segment permissions now that every byte is in place.
     *
     * Two passes, because segments whose file offsets are not 4 KB apart can
     * share a page and the union of their permissions has to win. Pass 0
     * tightens the read-only segments; pass 1 re-opens any page a writable
     * segment also covers. Running them in this order means a shared page ends
     * up writable, which is the safe direction: a page that should have been
     * read-only stays writable, rather than a writable page being locked and
     * faulting the program on its first store.
     *
     * The program header table was bounds-checked by elf_validate_image() and
     * again in the loop above, so re-deriving the entries here is in range.
     */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ehdr->e_phnum; i++) {
            elf32_phdr_t *ph = (elf32_phdr_t *)(file_buffer + ehdr->e_phoff +
                                                ((uint32_t)i * sizeof(elf32_phdr_t)));

            if (ph->p_type != 1 || ph->p_memsz == 0) continue;

            int writable = (ph->p_flags & 2) != 0;
            if (writable != pass) continue;

            uint32_t seg_end = ph->p_vaddr + ph->p_memsz;
            for (uint32_t page = (ph->p_vaddr & 0xFFFFF000); page < seg_end; page += 4096) {
                uint32_t pd_index = page >> 22;
                uint32_t pt_index = (page >> 12) & 0x3FF;
                uint32_t *pd_virt = (uint32_t *)0xFFFFF000;

                if (!(pd_virt[pd_index] & 1)) continue;

                uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
                if (!(pt_virt[pt_index] & 1)) continue;

                if (writable) {
                    pt_virt[pt_index] |= 0x02;
                } else {
                    pt_virt[pt_index] &= ~0x02u;
                }
                asm volatile("invlpg (%0)" ::"r"(page) : "memory");
            }
        }
    }

    // User Stack Allocation
    uint32_t esp_addr = 0xB0000000;
    for (int j = 1; j <= 32; j++)
    {
        uint32_t page_addr = esp_addr - (j * 4096);
        uint32_t phys = pmm_alloc_frame();
        if (phys == 0xFFFFFFFF) {
            printk("[ELF LOADER] Out of physical memory building the user stack!\n");
            goto cleanup_and_fail;
        }

        /* Same split, same reason, as the segment loop above. */
        if (map_page(page_addr, phys, 7) != E_OK) {
            pmm_free_frame(phys);
            printk("[ELF LOADER] Failed to map a user stack page!\n");
            goto cleanup_and_fail;
        }

        if (copy_to_user((void *)page_addr, zero_page, sizeof(zero_page)) != E_OK) {
            printk("[ELF LOADER] Failed to clear a user stack page!\n");
            goto cleanup_and_fail;
        }
    }

    /*
     * Guard page to catch stack overflows. The result is deliberately not
     * checked: this installs an entry with the present bit clear, and the only
     * way it can fail is that the page table for the range could not be
     * allocated - in which case the address is unmapped anyway, and an overflow
     * faults there exactly as intended.
     */
    uint32_t guard_page_addr = esp_addr - (33 * 4096);
    map_page(guard_page_addr, 0, 0); // 0 = Present Bit Cleared

    // Loading complete, return to Kernel's main map (CR3)
    asm volatile("mov %0, %%cr3" ::"r"(original_cr3) : "memory");
    if (eflags & 0x200)
    {
        asm volatile("sti" ::: "memory");
    }

    uint32_t entry_point = ehdr->e_entry;
    kfree(file_buffer);

    // Create task with newly isolated CR3 (new_pd)
    int new_pid = create_process(entry_point, esp_addr - 4, new_pd);

    if (new_pid >= 0)
    {
        process_t *new_task = 0;
        for (process_t *p = task_list_head; p != 0; p = p->next)
        {
            if (p->pid == new_pid && p->state != 0)
            {
                new_task = p;
                break;
            }
        }

        if (new_task != 0 && current_task != 0)
        {
            /* Shared with fork(), which needs exactly the same reference-counted
             * copy. The standard descriptors below are not part of it: they are
             * what a fresh program image needs, and a forked child inherits its
             * parent's table verbatim instead. */
            inherit_fd_table(new_task, current_task);

            if (new_task->fd_table[0].type == 0)
            {
                new_task->fd_table[0].type = 1; // stdin
                new_task->fd_table[1].type = 1; // stdout
                new_task->fd_table[2].type = 1; // stderr
            }
            return new_pid;
        }
    }

    klog(LOG_LEVEL_ERROR, "ELF", "Failed to create task (Internal logic error)!");
    return E_FAULT;
}
