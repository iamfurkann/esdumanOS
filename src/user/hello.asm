global _start

section .text
_start:
    mov eax, 4
    mov ebx, msg
    int 0x80

    mov eax, 1
    mov ebx, 0
    int 0x80

section .data
msg db ">>>ELF PROGRAM! <<<", 10, 0