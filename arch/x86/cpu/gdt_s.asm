; Loads the GDT address provided by C code into the CPU

global gdt_flush

; =============================================================================
; gdt_flush
; -----------------------------------------------------------------------------
; Low-level architectural purpose:
; Loads the Global Descriptor Table (GDT) pointer into the CPU using the `lgdt`
; instruction. It then updates all data segment registers to the new kernel data
; segment and performs a far jump to correctly reload the Code Segment (CS) register.
; =============================================================================
gdt_flush:
    mov eax, [esp+4]

    lgdt [eax]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush

.flush:
    ret
global tss_flush
; =============================================================================
; tss_flush
; -----------------------------------------------------------------------------
; Low-level architectural purpose:
; Loads the Task Register (TR) with the segment selector for the Task State Segment
; (TSS) using the `ltr` instruction. This informs the CPU where to find the Ring 0
; stack during privilege level transitions (e.g., interrupts from User Mode).
; =============================================================================
tss_flush:
    mov ax, 0x38
    ltr ax
    ret