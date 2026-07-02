global load_idt

load_idt:
    push ebp
    mov ebp, esp

    mov eax, [ebp+8]
    lidt [eax]

    pop ebp
    ret

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    cli
    push dword %1
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8  ; Double Fault
ISR_NOERRCODE 9
ISR_ERRCODE   10 ; Invalid TSS
ISR_ERRCODE   11 ; Segment Not Present
ISR_ERRCODE   12 ; Stack-Segment Fault
ISR_ERRCODE   13 ; General Protection Fault
ISR_ERRCODE   14 ; Page Fault
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

;(IRQs)
ISR_NOERRCODE 32 ; IRQ0: PIT (Timer)
ISR_NOERRCODE 33 ; IRQ1: Klavye
ISR_NOERRCODE 34 ; IRQ2: Cascade
ISR_NOERRCODE 35 ; IRQ3: COM2
ISR_NOERRCODE 36 ; IRQ4: COM1
ISR_NOERRCODE 37 ; IRQ5: LPT2
ISR_NOERRCODE 38 ; IRQ6: Floppy Disk
ISR_NOERRCODE 39 ; IRQ7: LPT1
ISR_NOERRCODE 40 ; IRQ8: CMOS RTC
ISR_NOERRCODE 41 ; IRQ9: Boş
ISR_NOERRCODE 42 ; IRQ10: Boş
ISR_NOERRCODE 43 ; IRQ11: Boş
ISR_NOERRCODE 44 ; IRQ12: Fare (PS/2)
ISR_NOERRCODE 45 ; IRQ13: FPU
ISR_NOERRCODE 46 ; IRQ14: Primary ATA Hard Disk
ISR_NOERRCODE 47 ; IRQ15: Secondary ATA Hard Disk

ISR_NOERRCODE 128 ; Syscall (0x80)

extern isr_handler
isr_common_stub:
    pusha
    mov ax, ds
    push eax

    mov ax, 0x10        
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp
    call isr_handler
    add esp, 4
    
    pop eax
    mov ds, ax
    mov es, ax       
    mov fs, ax
    mov gs, ax

    popa          
    add esp, 8
    iret