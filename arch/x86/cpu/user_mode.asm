global switch_to_user_mode

switch_to_user_mode:
    cli
    mov eax, [esp+4]
    mov ecx, [esp+8]

    mov dx, 0x2B
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push 0x2B
    push ecx

    pushfd
    pop edx
    or edx, 0x200
    push edx

    push 0x23
    push eax

    iret