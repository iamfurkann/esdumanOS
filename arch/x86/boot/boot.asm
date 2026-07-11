MBALIGN  equ 1<<0 
MEMINFO  equ 1<<1 
FLAGS    equ MBALIGN | MEMINFO 
MAGIC    equ 0x1BADB002        
CHECKSUM equ -(MAGIC + FLAGS)  

section .multiboot
align 4
        dd MAGIC
        dd FLAGS
        dd CHECKSUM

section .data
align 4096
boot_page_directory:
        times 1024 dd 0      ; İçi 0 ile dolu 4KB'lık Dizin
boot_page_table_0:
        times 4096 dd 0      ; İçi 0 ile dolu 16KB'lık Tablo alanı

section .bss
align 16
stack_bottom:
        resb 16384           ; 16 KB Çekirdek Yığını
stack_top:

section .text
global _start
extern kernel_main

_start:
        mov edi, boot_page_table_0
        mov esi, 0
        mov ecx, 4096

.map_pages:
        mov edx, esi
        or edx, 0x00000003         ; Present (1) + Read/Write (2)
        mov [edi], edx
        add esi, 4096
        add edi, 4
        loop .map_pages

        mov dword [boot_page_directory + 0], boot_page_table_0 + 0x003
        mov dword [boot_page_directory + 4], boot_page_table_0 + 4096 + 0x003
        mov dword [boot_page_directory + 8], boot_page_table_0 + 8192 + 0x003
        mov dword [boot_page_directory + 12], boot_page_table_0 + 12288 + 0x003

        ; Higher Half (0xC0000000) Eşleşmesi
        mov dword [boot_page_directory + 768 * 4], boot_page_table_0 + 0x003
        mov dword [boot_page_directory + 769 * 4], boot_page_table_0 + 4096 + 0x003
        mov dword [boot_page_directory + 770 * 4], boot_page_table_0 + 8192 + 0x003
        mov dword [boot_page_directory + 771 * 4], boot_page_table_0 + 12288 + 0x003

        mov ecx, boot_page_directory
        mov cr3, ecx

        mov ecx, cr0
        or ecx, 0x80000000
        mov cr0, ecx
        mov esp, stack_top

        push ebx ; Parametre 2: mboot_info
        push eax ; Parametre 1: magic

        call kernel_main

        cli
.hang:
        hlt
        jmp .hang