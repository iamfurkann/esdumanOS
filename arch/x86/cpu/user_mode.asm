global switch_to_user_mode
; =============================================================================
; switch_to_user_mode
; -----------------------------------------------------------------------------
; Low-level architectural purpose:
; Facilitates the transition from Kernel Mode (Ring 0) to User Mode (Ring 3).
; It prepares an `iret` frame containing the user-mode Data Segment, Stack Pointer,
; EFLAGS, Code Segment, and Instruction Pointer, then executes `iret` to switch privileges.
; =============================================================================
switch_to_user_mode:
    cli
    mov ebx, [esp+4]    ; EBX = eip (Target program starting address)
    mov ecx, [esp+8]    ; ECX = esp (Target program stack address)

    ; Ring 3 Data Segment
    mov ax, 0x2B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x2B     ; 5. SS (Ring 3 Data Segment)
    push ecx            ; 4. ESP (Ring 3 Stack pointer)
    pushfd              ; 3. EFLAGS

    pop eax
    or eax, 0x200
    push eax

    push dword 0x23
    push ebx

    iret