#include "elf.h"
#include "fs.h"
#include "stdio.h"
#include "libft.h"

extern void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
extern uint32_t pmm_alloc_frame(void);
extern void switch_to_user_mode(void *user_function, void *user_stack);
extern uint32_t user_stack[1024];

void load_and_exec_elf(const char *filename) {
    vfs_file_t file;
    if (fs_open(filename, &file) != 0) {
        printk("Hata: '%s' disk uzerinde bulunamadi!\n", filename);
        return;
    }

    elf32_ehdr_t ehdr;
    file.current_offset = 0;
    fs_read(&file, (uint8_t *)&ehdr, sizeof(elf32_ehdr_t));

    uint32_t *magic = (uint32_t *)ehdr.e_ident;
    if (*magic != ELF_MAGIC) {
        printk("Hata: %s gecerli bir ELF dosyasi degil!\n", filename);
        return;
    }

    printk("[ELF LOADER] '%s' isleniyor... Giris Adresi: 0x%x\n", filename, ehdr.e_entry);

    elf32_phdr_t phdr;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        file.current_offset = ehdr.e_phoff + (i * ehdr.e_phentsize);
        fs_read(&file, (uint8_t *)&phdr, sizeof(elf32_phdr_t));

        if(phdr.p_type == 1) {
            uint32_t page_count = (phdr.p_memsz / 4096) + 1;
            uint32_t base_vaddr = phdr.p_vaddr & 0xFFFFF000;

            for (uint32_t p = 0; p < page_count; p++) {
                uint32_t phys = pmm_alloc_frame();
                map_page(base_vaddr + (p * 4096), phys, 7);
            }

            file.current_offset = phdr.p_offset;
            fs_read(&file, (uint8_t *)phdr.p_vaddr, phdr.p_filesz);

            if (phdr.p_memsz > phdr.p_filesz)
                ft_memset((void *)(phdr.p_vaddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
        }
    }

    printk("[ELF LOADER] Kod RAM'e yerlestirildi. Calistiriliyor...\n\n");

    extern int create_process(uint32_t eip, uint32_t esp);
    
    uint32_t phys_stack = pmm_alloc_frame();
    static uint32_t next_elf_stack = 0xB0000000;
    map_page(next_elf_stack, phys_stack, 7);

    int pid = create_process(ehdr.e_entry, next_elf_stack + 4096 - 4);
    next_elf_stack += 4096;

    printk("[ELF LOADER] Kod RAM'e yerlestirildi. PID: %d olarak arka planda baslatildi!\n", pid);
}