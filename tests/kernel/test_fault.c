/*
 * File: test_fault.c
 * Purpose: Fault handling infrastructure tests.
 *
 * A double fault cannot be provoked from a test without ending the run, so what
 * is checked here is that the machinery is armed correctly: vector 8 dispatched
 * through a task gate, a valid TSS descriptor behind it, and that TSS pointing
 * at a private stack and the kernel address space.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "gdt.h"
#include "idt.h"
#include "tss.h"
#include "process.h"

extern idt_entry_t idt[256];
extern gdt_entry_t gdt_entries[];

/**
 * @brief Verifies the double fault path and the kernel stack budget.
 *
 * Expected behavior:
 * - Vector 8 is a present, DPL 0 task gate naming the double fault TSS.
 * - That selector resolves to an available 32-bit TSS descriptor.
 * - The TSS carries the kernel page directory, a stack of its own, and runs
 *   with interrupts disabled.
 * - Per-process kernel stacks are large enough for the deepest syscall path.
 *
 * Edge cases covered:
 * - An interrupt gate on vector 8 would push the fault frame onto the stack
 *   that just overflowed, so the gate *type* is asserted, not just its presence.
 * - The handler stack must not be one of the process kernel stacks, otherwise
 *   the handler dies the same way the faulting task did.
 */
void run_fault_tests(void) {
    printk("\n--- Fault Handling Infrastructure Tests ---\n");

    KTEST_ASSERT(KERNEL_STACK_SIZE >= 8192,
                 "Fault: per-process kernel stack is at least 8 KB");

    /* --- vector 8 must be a task gate, not an interrupt gate --- */
    KTEST_ASSERT((idt[8].flags & 0x80) != 0, "#DF: vector 8 gate is present");
    KTEST_ASSERT(((idt[8].flags >> 5) & 0x03) == 0, "#DF: vector 8 gate is DPL 0");
    KTEST_ASSERT((idt[8].flags & 0x1F) == 0x05,
                 "[STRICT] #DF: vector 8 is a task gate (type 5), not an interrupt gate");
    KTEST_ASSERT(idt[8].sel == GDT_DF_TSS_SEL,
                 "#DF: vector 8 selects the double fault TSS");

    /* --- and that selector must resolve to a usable TSS descriptor --- */
    gdt_entry_t *df_desc = &gdt_entries[GDT_DF_TSS_SEL >> 3];
    KTEST_ASSERT((df_desc->access & 0x80) != 0, "#DF: TSS descriptor is present");
    KTEST_ASSERT((df_desc->access & 0x0F) == 0x09,
                 "[STRICT] #DF: descriptor is an available 32-bit TSS (type 9)");

    uint32_t df_base = (uint32_t)df_desc->base_low |
                       ((uint32_t)df_desc->base_middle << 16) |
                       ((uint32_t)df_desc->base_high << 24);
    KTEST_ASSERT(df_base == (uint32_t)&df_tss,
                 "#DF: descriptor points at the double fault TSS");

    /* --- the task itself has to be able to run --- */
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    KTEST_ASSERT(df_tss.cr3 == cr3, "#DF: task carries the kernel page directory");
    KTEST_ASSERT(df_tss.eip != 0, "#DF: task has an entry point");
    KTEST_ASSERT(df_tss.cs == GDT_KERNEL_CS, "#DF: task runs in the kernel code segment");
    KTEST_ASSERT((df_tss.eflags & 0x200) == 0, "#DF: task runs with interrupts disabled");

    /* The whole point of the task gate: a stack that did not just overflow. */
    KTEST_ASSERT(df_stack_contains(df_tss.esp),
                 "[STRICT] #DF: task stack is its own, not a process kernel stack");
    KTEST_ASSERT(!df_stack_contains(tss_entry.esp0),
                 "[STRICT] #DF: the live Ring 0 stack is not the double fault stack");
}
