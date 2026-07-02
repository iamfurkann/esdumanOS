MBALIGN equ     1<<0 ;Modulleri page sinirlarina hizala
MEMINFO equ     1<<1 ;Sistemdeki bellek haritasini bize sagla
FLAGS   equ     MBALIGN | MEMINFO ;Yukarida bulunan bayraklari birlestir
MAGIC   equ     0x1BADB002        ;GRUB'in aradigi zorunlu Multiboot magic number
CHECKSUM equ    -(MAGIC + FLAGS)  ; Dogrulama (MAGIC + FLAGS + CHECKSUM her zaman 0 olmalidir)

section .multiboot
align 4
        dd MAGIC
        dd FLAGS
        dd CHECKSUM

;Stack section
section .bss
        align 16
        stack_bottom:
        resb 16384
        stack_top:

section .text
        global _start
extern kernel_main ;
_start:
        mov esp, stack_top

        push ebx
        push eax

        call kernel_main

        cli
.hang:
        hlt
        jmp .hang
