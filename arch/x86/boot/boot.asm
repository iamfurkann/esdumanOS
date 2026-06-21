;GRUB asagidaki sabit degerleri okuyarak kerneli tanir

MBALIGN equ     1<<0 ;Modulleri page sinirlarina hizala
MEMINFO equ     1<<1 ;Sistemdeki bellek haritasini bize sagla
FLAGS   equ     MBALIGN | MEMINFO ;Yukarida bulunan bayraklari birlestir
MAGIC   equ     0x1BADB002        ;GRUB'in aradigi zorunlu Multiboot magic number
CHECKSUM equ    -(MAGIC + FLAGS)  ; Dogrulama (MAGIC + FLAGS + CHECKSUM her zaman 0 olmalidir)

;bu section dosyanin en basinda olmalidir (8KB yani)
section .multiboot
align 4
        dd MAGIC
        dd FLAGS
        dd CHECKSUM

;Stack section
;C dili calisabilmek icin stack alanina ihtiyac duyar.
;Henuz dinamik bir allocation olmadigi icin statick(stack) bir alan ayirdik.
section .bss
        align 16
        stack_bottom:
        resb 16384 ;16KB buyuklugunde alan rezerv ediyoruz.
        stack_top:

;bu section islemcinin calisacagi ilk yer
section .text
        global _start
extern kernel_main ; C kodunudaki func'i dahil ediyoruz.
_start:
        mov esp, stack_top

        push ebx
        push eax

        call kernel_main

        ;kernel_main biterse islemciyi sonsuz bir uyku dongusune giriyor 
        cli ;interruptlari kapat
.hang:
        hlt ;islemciyi bir sonraki interrupta kadar durdur
        jmp .hang ;hlt'den uyanirsa tekrar basa don

