#ifndef KERNEL_H
#define KERNEL_H

/* Her güncellemede aşağıdaki rakamları arttırman yeterlidir:
 * MAJOR : Köklü/mimari değişiklikler (Örn: Mikroçekirdeğe geçiş)
 * MINOR : Yeni büyük özellikler (Örn: Dosya sistemi eklenmesi)
 * PATCH : Hata düzeltmeleri, ufak yamalar (Örn: Klavye boşluk fix)
 */
#define OS_VERSION_MAJOR    3
#define OS_VERSION_MINOR    4
#define OS_VERSION_PATCH    1

/* Makro Sihirbazlığı (Derleme zamanında sayıları yazıya çevirir) */
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

/* Nihai Versiyon Stringi: Çıktı otomatik olarak " v1.1.0 " formatına dönüşür */
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
#include "multiboot.h"
#include "rtc.h"
#include "signal.h"
#include "ata.h"
// Fonksiyon prototiplerin
void kernel_init(multiboot_info_t* mboot_info);
void kernel_welcome(void);
void spawn_user_process(void (*func)());
extern void switch_to_user_mode(void *user_function, void *user_stack);

#endif