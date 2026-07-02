global switch_to_user_mode
switch_to_user_mode:
    cli
    mov ebx, [esp+4]    ; EBX = eip (Hedef programin baslangic adresi)
    mov ecx, [esp+8]    ; ECX = esp (Hedef programin yigin adresi)

    ; Ring 3 Data Segment
    mov ax, 0x2B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x2B     ; 5. SS (Ring 3 Data Segment)
    push ecx            ; 4. ESP (Ring 3 Yigin isareti)
    pushfd              ; 3. EFLAGS

    pop eax
    or eax, 0x200
    push eax

    push dword 0x23
    push ebx

    iret