#include "stack.h"
#include "stdio.h"

void print_hex_byte(uint8_t value)
{
    char *hex = "0123456789abcdef";

    ft_kputchar(hex[(value >> 4) & 0xF]);
    ft_kputchar(hex[value & 0xF]);
}

void print_kernel_stack(void)
{
    uint32_t esp;

    asm volatile("mov %%esp, %0" : "=r"(esp));

    unsigned char *stack = (unsigned char *)esp;

    printk("\nstack\n");

    for (int i = 0; i < 320; i += 16) {

        printk("0x%x: ", (uint32_t)(stack + i));

        for (int j = 0; j < 16; j++) {

            print_hex_byte(stack[i + j]);
            ft_kputchar(' ');
        }

        ft_kputchar(' ');

        for (int j = 0; j < 16; j++) {

            unsigned char c = stack[i + j];

            if (c >= 32 && c <= 126)
                ft_kputchar(c);
            else
                ft_kputchar('.');
        }

        ft_kputchar('\n');
    }
}