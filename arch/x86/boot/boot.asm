MBALIGN  equ 1<<0
MEMINFO  equ 1<<1
VIDEO    equ 1<<2
FLAGS    equ MBALIGN | MEMINFO | VIDEO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

; Asking the bootloader for a screen made of pixels.
;
; VIDEO is what makes this kernel bootable on a machine with no text mode, which
; is every machine that boots UEFI. GRUB sets the mode before handing over and
; reports where it put the buffer in the Multiboot information; the kernel maps
; that and draws glyphs into it. Without the flag the request is never made and
; the framebuffer fields in the information structure are never filled in.
;
; The five zeros are not padding that could be dropped. Multiboot 1 puts its
; fields at fixed offsets: the load addresses occupy bytes 12 through 31 and the
; video request begins at byte 32, so reaching the video request means writing
; the addresses whether or not anything reads them. They are ignored here
; because bit 16 is clear, which is what tells the bootloader to take the load
; addresses from the ELF headers instead.
;
; 1024x768x32 is a request rather than an instruction. A bootloader is free to
; answer with something else, or with a text mode, and the kernel uses whatever
; it is actually given - see console_use_framebuffer(), which picks the largest
; whole-number glyph scale the answer leaves room for and centres the result.
section .multiboot
align 4
        dd MAGIC
        dd FLAGS
        dd CHECKSUM
        dd 0, 0, 0, 0, 0        ; header_addr .. entry_addr, unused
        dd 0                    ; mode_type: 0 is a linear graphics mode
        dd 1024                 ; width
        dd 768                  ; height
        dd 32                   ; depth, bits per pixel

; A second header, for the second specification, and the first one stays.
;
; Multiboot 2 is not an upgrade this kernel performs; it is a second way in that
; it now also answers to. Three of the four test targets boot with QEMU's
; -kernel, which reads Multiboot 1 and nothing else, so removing that header
; would take the whole suite with it. A bootloader reads whichever header it
; understands and ignores the other.
;
; What Multiboot 2 is here for is one tag: the ACPI RSDP. On a BIOS machine the
; RSDP can be found by scanning the legacy areas below 1 MB; on a UEFI machine
; it cannot, because those areas need not exist - the firmware hands it over in
; the EFI configuration table, and the bootloader is the only thing that ever
; sees that. Multiboot 1 has no way to pass it on. That is the whole reason this
; block exists, and shutting the machine down is what it buys.
;
; _start is unchanged and there is no second entry point, because both
; specifications enter the same way: the magic in eax, the information structure
; in ebx. kernel_main tells them apart by the magic and nothing else has to
; know.
;
; Eight-byte alignment is required by the specification rather than preferred,
; and the header must be inside the first 32 KB of the image - which the linker
; script guarantees by keeping both headers in the section it places first.
MB2_MAGIC equ 0xE85250D6
MB2_ARCH  equ 0                 ; 32-bit protected mode, i386

section .multiboot2
align 8
mb2_header_start:
        dd MB2_MAGIC
        dd MB2_ARCH
        dd mb2_header_end - mb2_header_start
        dd -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))

        ; The framebuffer request, which is this header's half of what the
        ; Multiboot 1 VIDEO flag asks for above. Same numbers, and the same
        ; standing: a bootloader may answer with something else or with a text
        ; mode, and install_framebuffer_console() already copes with both -
        ; it has had to since v1.6.0.
align 8
        dw 5                    ; tag type: framebuffer request
        dw 0                    ; flags
        dd 20                   ; size
        dd 1024                 ; width
        dd 768                  ; height
        dd 32                   ; depth

        ; The end tag. Not optional and not padding: the tag walk stops here.
align 8
        dw 0                    ; tag type: end
        dw 0                    ; flags
        dd 8                    ; size
mb2_header_end:

section .data
align 4096
boot_page_directory:
        times 1024 dd 0      ; 4KB Directory filled with 0s
boot_page_table_0:
        times 4096 dd 0      ; 16KB Table area filled with 0s

section .bss
align 16
stack_bottom:
        resb 16384           ; 16 KB Kernel Stack
stack_top:

section .text
global _start
extern kernel_main
extern _bss_start
extern _bss_end

; =============================================================================
; _start
; -----------------------------------------------------------------------------
; Low-level architectural purpose:
; Acts as the entry point for the kernel as invoked by the Multiboot compliant
; bootloader. It sets up initial paging for higher-half mapping, configures
; the basic kernel stack, and transfers execution to the C kernel main entry.
; =============================================================================
_start:
        ; Physical addresses must be used before paging is enabled!
        mov edi, (boot_page_table_0 - 0xC0000000)
        mov esi, 0
        mov ecx, 4096

.map_pages:
        mov edx, esi
        or edx, 0x00000003         ; Present (1) + Read/Write (2)
        mov [edi], edx
        add esi, 4096
        add edi, 4
        loop .map_pages

        mov dword [(boot_page_directory - 0xC0000000) + 0], (boot_page_table_0 - 0xC0000000) + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 4], (boot_page_table_0 - 0xC0000000) + 4096 + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 8], (boot_page_table_0 - 0xC0000000) + 8192 + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 12], (boot_page_table_0 - 0xC0000000) + 12288 + 0x003

        ; Higher Half (0xC0000000) Mapping
        mov dword [(boot_page_directory - 0xC0000000) + 768 * 4], (boot_page_table_0 - 0xC0000000) + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 769 * 4], (boot_page_table_0 - 0xC0000000) + 4096 + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 770 * 4], (boot_page_table_0 - 0xC0000000) + 8192 + 0x003
        mov dword [(boot_page_directory - 0xC0000000) + 771 * 4], (boot_page_table_0 - 0xC0000000) + 12288 + 0x003

        mov ecx, (boot_page_directory - 0xC0000000)
        mov cr3, ecx

        mov ecx, cr0
        or ecx, 0x80000000
        mov cr0, ecx
        
        ; Jump to higher half (Trampoline)
        lea ecx, [higher_half_jump]
        jmp ecx

higher_half_jump:
        ; Now we are executing at 0xC0100000+!
        
        ; Setup the Stack (using virtual address)
        mov esp, stack_top

        ; Save magic value (eax) in ebp because rep stosb clobbers eax
        mov ebp, eax

        ; Clear BSS (using virtual addresses)
        mov edi, _bss_start
        mov ecx, _bss_end
        sub ecx, edi
        xor eax, eax
        rep stosb

        ; Restore magic value
        mov eax, ebp

        push ebx ; Parameter 2: mboot_info (note: ebx contains physical address of mboot info!)
        push eax ; Parameter 1: magic

        call kernel_main

        cli
.hang:
        hlt
        jmp .hang