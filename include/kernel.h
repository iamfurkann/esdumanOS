#ifndef KERNEL_H
#define KERNEL_H

/*
 * MAJOR : Köklü/mimari değişiklikler
 * MINOR : Yeni büyük özellikler
 * PATCH : Hata düzeltmeleri, ufak yamalar
 */
#define OS_VERSION_MAJOR    3
#define OS_VERSION_MINOR    4
#define OS_VERSION_PATCH    3

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

#define OS_VERSION_STR "  v" STRINGIFY(OS_VERSION_MAJOR) "." STRINGIFY(OS_VERSION_MINOR) "." STRINGIFY(OS_VERSION_PATCH) " "


#include "types.h"
#include "tty.h"
#include "stdio.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "io.h"
#include "rtc.h"
#include "signal.h"
#include "ata.h"
#include "fs.h"
#include "elf.h"
#include "process.h"
#include "errno.h"
#include "isr.h"
#include "security.h"

extern void switch_to_user_mode(uint32_t eip, uint32_t esp);
extern unsigned char init_elf[];
extern unsigned int init_elf_len;
#endif