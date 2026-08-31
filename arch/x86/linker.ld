ENTRY(_start)

SECTIONS
{
    . = 1M;
    
    /*
     * Both multiboot headers must be located early in the binary.
     *
     * Multiboot 1 wants its header inside the first 8 KB and 4-byte aligned;
     * Multiboot 2 wants its own inside the first 32 KB and 8-byte aligned. Both
     * are satisfied by putting them in the section this script places first,
     * with the alignment stated rather than assumed - the assembler's `align 8`
     * only aligns within its own section, and it is the linker that decides
     * where that section begins.
     */
    .multiboot.data : {
        *(.multiboot)
        . = ALIGN(8);
        *(.multiboot2)
    }
    
    . += 0xC0000000;
    _kernel_start = .;

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
    {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    . = ALIGN(4K);
    _kernel_end = .;
}