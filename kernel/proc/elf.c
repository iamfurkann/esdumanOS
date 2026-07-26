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
/**
 * @brief Loads and executes a 32-bit ELF file.
 *
 * @param filename Path of the ELF file to load.
 * @param parent_id ID of the parent directory.
 * @return The task index on success, or a negative error code on failure.
 */
int load_and_exec_elf(const char *filename, uint8_t parent_id)
{
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

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)file_buffer;
    
    // Basic ELF signature and architecture check.
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || 
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        klog(LOG_LEVEL_ERROR, "ELF", "Not a valid ELF file!");
        kfree(file_buffer);
        return E_NOEXEC;
    }

    // [CRITICAL SECURITY PATCH]: Only accept 32-bit x86 (i386) files!
    if (ehdr->e_ident[4] != 1) { // 1 = ELFCLASS32
        klog(LOG_LEVEL_ERROR, "ELF", "File is not 32-bit (ELFCLASS32)!");
        kfree(file_buffer);
        return E_NOEXEC;
    }
    if (ehdr->e_machine != 3) { // 3 = EM_386 (Intel 80386)
        klog(LOG_LEVEL_ERROR, "ELF", "File does not match x86 (i386) architecture!");
        kfree(file_buffer);
        return E_NOEXEC;
    }

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
        uint32_t chunk_size = i * ehdr->e_phentsize;
        uint32_t offset = ehdr->e_phoff + chunk_size;
        
        if (offset < ehdr->e_phoff || offset >= (uint32_t)read_bytes || (uint32_t)read_bytes - offset < sizeof(elf32_phdr_t)) {
            printk("[ELF LOADER] CRITICAL ERROR: Corrupted ELF header (Phdr)!\n");
            goto cleanup_and_fail;
        }

        elf32_phdr_t *phdr = (elf32_phdr_t *)(file_buffer + offset);

        if (phdr->p_type == 1) {
            if (phdr->p_offset > file.file_size || phdr->p_filesz > file.file_size - phdr->p_offset) {
                printk("[ELF SECURITY] Error: Program header exceeds file bounds!\n");
                goto cleanup_and_fail;
            }

            uint32_t start_addr = phdr->p_vaddr;
            uint32_t end_addr = phdr->p_vaddr + phdr->p_memsz;
            
            // [CRITICAL SECURITY PATCH]: User-Space Address Validation
            // No program can be loaded below 0x400000 (4MB) or above 0xC0000000 (3GB)!
            if (start_addr < 0x400000 || end_addr > 0xC0000000 || end_addr < start_addr) {
                printk("[ELF SECURITY] Error: Invalid load address (0x%x). Cannot write to kernel space!\n", start_addr);
                goto cleanup_and_fail;
            }

            uint32_t flags = (phdr->p_flags & 2) ? 7 : 5; // 7 = RW (Writable), 5 = Read-Only

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
                    map_page(page, phys, flags); 
                    ft_memset((void *)page, 0, 4096);
                } else {
                    if ((flags & 2) && pt_virt) {
                        pt_virt[pt_index] |= 2; 
                        asm volatile("invlpg (%0)" ::"r"(page) : "memory");
                    }
                }
            }
            
            if (phdr->p_filesz > 0) {
                ft_memcpy((void *)phdr->p_vaddr, file_buffer + phdr->p_offset, phdr->p_filesz);
            }
            if (phdr->p_memsz > phdr->p_filesz) {
                uint32_t bss_start = phdr->p_vaddr + phdr->p_filesz;
                uint32_t bss_size = phdr->p_memsz - phdr->p_filesz;

                if (bss_start >= 0x400000) {
                    ft_memset((void *)bss_start, 0, bss_size);
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

    // User Stack Allocation
    uint32_t esp_addr = 0xB0000000;
    for (int j = 1; j <= 32; j++)
    {
        uint32_t page_addr = esp_addr - (j * 4096);
        uint32_t phys = pmm_alloc_frame();
        map_page(page_addr, phys, 7); // 7 = User, RW, Present
        ft_memset((void *)page_addr, 0, 4096);
    }

    // Guard Page to catch stack overflows
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
            for (uint32_t k = 0; k < current_task->fd_table_size; k++)
            {
                if (k < new_task->fd_table_size) {
                    new_task->fd_table[k] = current_task->fd_table[k];
                    if (new_task->fd_table[k].type == 3 && new_task->fd_table[k].ptr != 0)
                    {
                        pipe_t *p = (pipe_t *)new_task->fd_table[k].ptr;
                        if (new_task->fd_table[k].mode == 1) p->write_refs++;
                        else p->read_refs++;
                    }
                    else if (new_task->fd_table[k].type == 2 && new_task->fd_table[k].ptr != 0)
                    {
                        vfs_file_t *f = (vfs_file_t *)new_task->fd_table[k].ptr;
                        if (f->ref_count >= 0 && f->ref_count < 1000) {
                            f->ref_count++;
                        } else {
                            new_task->fd_table[k].type = 0;
                            new_task->fd_table[k].ptr = 0;
                            klog_int(LOG_LEVEL_WARN, "ELF", "Use-After-Free Protection: Invalid file pointer cleared FD", k);
                        }
                    }
                }
            }

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