#include "stack.h"
#include "stdio.h"

/*
 * The kernel stack, rendered rather than printed.
 *
 * This printed to the screen until v0.9.2, which meant `stack > dump.txt` wrote
 * an empty file and `stack | grep` fed an empty pipe: the bytes went to the VGA
 * text buffer and never reached the caller's descriptor 1. It fills a buffer
 * now, and the caller writes it.
 *
 * Note that the buffer being filled is itself somewhere in memory the dump may
 * cover, so a dump taken through this shows a stack with the rendering in
 * progress on it. That is unavoidable for a self-inspecting dump and it was
 * already true of the printing version, whose formatting buffers sat in the same
 * frames.
 */

/**
 * @brief The hexadecimal digits, once, for both renderers here.
 */
static const char hex_digits[] = "0123456789abcdef";

int format_kernel_stack(char *buf, uint32_t cap) {
    uint32_t esp;
    int n = 0;

    asm volatile("mov %%esp, %0" : "=r"(esp));

    unsigned char *stack = (unsigned char *)esp;

    n = kbprintf(buf, cap, (uint32_t)n, "\nstack\n");

    for (int i = 0; i < 320; i += 16) {

        n = kbprintf(buf, cap, (uint32_t)n, "0x%x: ", (uint32_t)(stack + i));

        for (int j = 0; j < 16; j++) {
            unsigned char v = stack[i + j];

            n = kbprintf(buf, cap, (uint32_t)n, "%c%c ",
                         hex_digits[(v >> 4) & 0xF], hex_digits[v & 0xF]);
        }

        n = kbprintf(buf, cap, (uint32_t)n, " ");

        for (int j = 0; j < 16; j++) {

            unsigned char c = stack[i + j];

            if (c >= 32 && c <= 126)
                n = kbprintf(buf, cap, (uint32_t)n, "%c", c);
            else
                n = kbprintf(buf, cap, (uint32_t)n, ".");
        }

        n = kbprintf(buf, cap, (uint32_t)n, "\n");
    }

    return n;
}
