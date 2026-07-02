#include "elf.h"
#include "fs.h"
#include "stdio.h"
#include "libft.h"
#include "process.h"
#include "paging.h"
#include "pipe.h"

extern void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
extern uint32_t pmm_alloc_frame(void);
extern uint32_t clone_page_directory(void);
extern int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);
extern int check_free_task_slot(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

int load_and_exec_elf(const char *filename) {
    if (!check_free_task_slot()) {
        printk("[ELF LOADER] HATA: Maksimum gorev limitine (16) ulasildi. '%s' yuklenemedi!\n", filename);
        return -1;
    }

    vfs_file_t file;
    if (fs_open(filename,0, &file) != 0) {
        printk("Hata: '%s' disk uzerinde bulunamadi!\n", filename);
        return -1;
    }

    uint8_t *file_buffer = (uint8_t *)kmalloc(file.file_size);
    if (!file_buffer) {
        printk("Hata: ELF yukleyici icin RAM ayrilamadi!\n");
        return -1;
    }

    int read_bytes = fs_read(&file, file_buffer, file.file_size);
    if (read_bytes < (int)sizeof(elf32_ehdr_t)) {
        kfree(file_buffer);
        return -1;
    }
    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)file_buffer;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || 
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        printk("Hata: %s gecerli bir ELF dosyasi degil!\n", filename);
        kfree(file_buffer);
        return -1;
    }

    printk("[ELF LOADER] '%s' isleniyor... Giris Adresi: 0x%x\n", filename, ehdr->e_entry);

    uint32_t new_pd = clone_page_directory();
    if (!new_pd) {
        kfree(file_buffer);
        return -1;
    }

    asm volatile("cli" ::: "memory");
    uint32_t original_cr3;
    
    asm volatile("mov %%cr3, %0" : "=r"(original_cr3) :: "memory");
    asm volatile("mov %0, %%cr3" :: "r"(new_pd) : "memory");
    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(file_buffer + ehdr->e_phoff + (i * ehdr->e_phentsize));

        if (phdr->p_type == 1) { // PT_LOAD
            uint32_t start_page = phdr->p_vaddr & 0xFFFFF000;
            uint32_t end_page = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & 0xFFFFF000;

            for (uint32_t page = start_page; page < end_page; page += 4096) {
                uint32_t pd_index = page >> 22;
                uint32_t pt_index = (page >> 12) & 0x3FF;
                uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
                
                int is_mapped = 0;
                if (pd_virt[pd_index] & 1) {
                    uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
                    if (pt_virt[pt_index] & 1) {
                        is_mapped = 1;
                    }
                }

                if (!is_mapped) {
                    uint32_t phys = pmm_alloc_frame();
                    uint32_t flags = (phdr->p_flags & 2) ? 7 : 5; // 7 = RW, 5 = Read-Only
                    
                    map_page(page, phys, flags); 
                }
            }
            ft_memcpy((void *)phdr->p_vaddr, file_buffer + phdr->p_offset, phdr->p_filesz);

            if (phdr->p_memsz > phdr->p_filesz) {
                ft_memset((void *)(phdr->p_vaddr + phdr->p_filesz), 0, phdr->p_memsz - phdr->p_filesz);
            }
        }
    }

    uint32_t esp_addr = 0xC0000000; 
    for (int j = 1; j <= 32; j++) {
        uint32_t page_addr = esp_addr - (j * 4096); 
        uint32_t phys = pmm_alloc_frame();
        map_page(page_addr, phys, 7);
    }

    uint32_t guard_page_addr = esp_addr - (33 * 4096);
    map_page(guard_page_addr, 0, 0); // 0 = Present Biti Kapalı

    asm volatile("mov %0, %%cr3" :: "r"(original_cr3) : "memory");
    asm volatile("sti" ::: "memory");
    
    uint32_t entry_point = ehdr->e_entry;
    kfree(file_buffer);
    int new_pid = create_process(entry_point, esp_addr - 4, new_pd);
    
    if (new_pid >= 0) {
        int array_index = -1;
        extern process_t tasks[];
        for (int i = 0; i < 16; i++) { // MAX_TASKS = 16
            if (tasks[i].pid == new_pid && tasks[i].state != 0) { // TASK_EMPTY = 0
                array_index = i;
                break;
            }
        }
        
        if (array_index >= 0) {

            for (int k = 0; k < 16; k++) {
                tasks[array_index].fd_table[k] = tasks[current_task].fd_table[k];
                if (tasks[array_index].fd_table[k].type == 3 && tasks[array_index].fd_table[k].ptr != 0) { 
                    pipe_t *p = (pipe_t *)tasks[array_index].fd_table[k].ptr;
                    if (tasks[array_index].fd_table[k].mode == 1) {
                        p->write_refs++;
                    } else {
                        p->read_refs++;
                    }
                }
            }
            
            if (tasks[array_index].fd_table[0].type == 0) {
                tasks[array_index].fd_table[0].type = 1; // stdin
                tasks[array_index].fd_table[1].type = 1; // stdout
                tasks[array_index].fd_table[2].type = 1; // stderr
            }
            
            printk("[ELF LOADER] PID: %d (Slot: %d) FD tablosu miras alindi.\n", new_pid, array_index);
            return array_index;
        }
    }
    
    printk("[ELF LOADER] Hata: Gorev olusturulamadi!\n");
    return -1;
}   