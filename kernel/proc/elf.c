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

extern int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
extern uint32_t pmm_alloc_frame(void);
extern uint32_t clone_page_directory(void);
extern int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);
extern int check_free_task_slot(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

extern uint8_t kernel_master_key[32];
extern security_level_t current_sec_level;

int load_and_exec_elf(const char *filename, uint8_t parent_id) {
    if (!check_free_task_slot()) {
        printk("[ELF LOADER] HATA: Maksimum gorev limitine (16) ulasildi. '%s' yuklenemedi!\n", filename);
        return -1;
    }

    vfs_file_t file;
    if (fs_open(filename, parent_id, &file) != E_OK) {
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

    uint32_t new_pd = clone_page_directory();
    if (!new_pd) {
        kfree(file_buffer);
        return -1;
    }

    uint32_t eflags;
    asm volatile("pushfl; popl %0; cli" : "=r"(eflags)); 
    
    uint32_t original_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(original_cr3) :: "memory");
    
    // Yükleme işlemi için geçici olarak yeni izole haritaya (CR3) geç
    asm volatile("mov %0, %%cr3" :: "r"(new_pd) : "memory");

    for (int i = 0; i < ehdr->e_phnum; i++) {
        uint32_t chunk_size = i * ehdr->e_phentsize;
        uint32_t offset = ehdr->e_phoff + chunk_size;
        
        if (offset < ehdr->e_phoff || offset >= (uint32_t)read_bytes || (uint32_t)read_bytes - offset < sizeof(elf32_phdr_t)) {
            printk("[ELF LOADER] KRITIK HATA: Bozuk ELF basligi (Phdr)!\n");
            asm volatile("mov %0, %%cr3" :: "r"(original_cr3) : "memory");
            if (eflags & 0x200) asm volatile("sti" ::: "memory");
            kfree(file_buffer);
            return -1;
        }

        elf32_phdr_t *phdr = (elf32_phdr_t *)(file_buffer + offset);

        if (phdr->p_type == 1) { // PT_LOAD Segmenti (.text, .data, .bss)
            uint32_t start_addr = phdr->p_vaddr;
            uint32_t end_addr = phdr->p_vaddr + phdr->p_memsz;
            uint32_t flags = (phdr->p_flags & 2) ? 7 : 5; // 7 = RW (Yazılabilir), 5 = Read-Only

            // Sayfa sayfa haritalama döngüsü
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
                    // Sayfa yoksa yeni Frame tahsis et ve haritala
                    uint32_t phys = pmm_alloc_frame();
                    map_page(page, phys, flags); 
                    
                    // [KRİTİK ÇÖZÜM]: Yeni tahsis edilen RAM sayfasını SIFIRLA!
                    // Bu sayede çöp veriler User değişkenlerine sızıp programı çökertmez.
                    ft_memset((void *)page, 0, 4096);
                } else {
                    // Sayfa zaten .text sebebiyle Read-Only ise ve yeni gelen segment
                    // .bss/.data gibi Yazma yetkisi (RW) istiyorsa kilidi aç!
                    if ((flags & 2) && pt_virt) {
                        pt_virt[pt_index] |= 2; 
                        asm volatile("invlpg (%0)" ::"r"(page) : "memory");
                    }
                }
            }
            
            // Veriyi dosyadan sanal belleğe kopyala
            if (phdr->p_filesz > 0) {
                ft_memcpy((void *)phdr->p_vaddr, file_buffer + phdr->p_offset, phdr->p_filesz);
            }

            // Dosyada yer kaplamayan ama RAM'de olması gereken .bss (Değişkenler) alanını sıfırla
            if (phdr->p_memsz > phdr->p_filesz) {
                ft_memset((void *)(phdr->p_vaddr + phdr->p_filesz), 0, phdr->p_memsz - phdr->p_filesz);
            }
        }
    }

    // Kullanıcı Yığını (User Stack) Tahsisi
    uint32_t esp_addr = 0xB0000000; 
    for (int j = 1; j <= 32; j++) {
        uint32_t page_addr = esp_addr - (j * 4096); 
        uint32_t phys = pmm_alloc_frame();
        map_page(page_addr, phys, 7); // 7 = User, RW, Present
        ft_memset((void *)page_addr, 0, 4096); 
    }

    // Stack taşmalarını yakalamak için (Guard Page) koruma sayfası
    uint32_t guard_page_addr = esp_addr - (33 * 4096);
    map_page(guard_page_addr, 0, 0); // 0 = Present Biti Kapalı

    // Yükleme bitti, Kernel'in ana haritasına (CR3) geri dön
    asm volatile("mov %0, %%cr3" :: "r"(original_cr3) : "memory");
    if (eflags & 0x200) {
        asm volatile("sti" ::: "memory");
    }
    
    uint32_t entry_point = ehdr->e_entry;
    kfree(file_buffer);
    
    // Görevi yeni izole edilmiş CR3 (new_pd) ile oluştur
    int new_pid = create_process(entry_point, esp_addr - 4, new_pd);
    
    if (new_pid >= 0) {
        int array_index = -1;
        extern process_t tasks[];
        for (int i = 0; i < 16; i++) { 
            if (tasks[i].pid == new_pid && tasks[i].state != 0) { 
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
            return array_index;
        }
    }
    
    printk("[ELF LOADER] Hata: Gorev olusturulamadi!\n");
    return -1;
}