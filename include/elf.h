#ifndef ELF_H
#define ELF_H

#include "types.h"

#define ELF_MAGIC 0x464C457F

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;      /* 2 = Executable (Çalıştırılabilir Dosya) */
    uint16_t e_machine;   /* 3 = x86 (i386) Mimarisi */
    uint32_t e_version;
    uint32_t e_entry;     /* ÇOK KRİTİK: Programın çalışmaya başlayacağı Sanal Adres! */
    uint32_t e_phoff;     /* Program Header Tablosunun dosya içindeki başlangıç noktası */
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize; /* Her bir Program Header'ın boyutu */
    uint16_t e_phnum;     /* Kaç tane Program Header var? */
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;      /* 1 = Yüklenebilir (Loadable) Segment */
    uint32_t p_offset;    /* Bu segmentin dosya içindeki başlangıç noktası */
    uint32_t p_vaddr;     /* ÇOK KRİTİK: RAM'de kopyalanması gereken Sanal Adres */
    uint32_t p_paddr;     /* Fiziksel Adres (Genelde kullanılmaz) */
    uint32_t p_filesz;    /* Dosyadaki boyutu (Byte) */
    uint32_t p_memsz;     /* RAM'de kaplayacağı boyut (Genellikle filesz ile aynıdır, BSS hariç) */
    uint32_t p_flags;     /* Segment İzinleri (R, W, X) */
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

int load_and_exec_elf(const char *filename, uint8_t parent_id);

#endif