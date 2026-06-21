CC = gcc
AS = nasm
LD = ld

# EXPORT ekliyoruz ki src/libc/Makefile bu derleyiciyi ve flag'leri alabilsin!
export CC AS LD

# Include kısmına libc klasörünü de ekledik ki libft.h her yerden bulunsun
CFLAGS = -m32 -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-stack-protector -Wall -Wextra -I include -I src/libc -c
export CFLAGS

ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T arch/x86/linker.ld

OBJS = arch/x86/boot/boot.o \
        kernel/kernel.o \
        kernel/signal.o \
        kernel/process.o \
        kernel/elf.o \
        drivers/tty.o \
        drivers/keyboard.o \
        drivers/ata.o \
        drivers/rtc.o \
        lib/stdio.o \
        lib/utils.o \
        lib/utils2.o \
        lib/stack.o \
        arch/x86/cpu/gdt.o  \
        arch/x86/cpu/gdt_s.o \
        arch/x86/cpu/idt.o \
        arch/x86/cpu/idt_s.o \
        arch/x86/cpu/isr.o \
        arch/x86/cpu/timer.o \
        arch/x86/cpu/tss.o \
        arch/x86/cpu/user_mode.o \
        mm/pmm.o \
        mm/paging.o \
        mm/paging_s.o \
        mm/kheap.o \
        fs/vfs.o

BIN = myos.bin
ISO = myos.iso
LIBC = lib/libc.a

all: $(ISO)

# libc.a'nın derlenme kuralı
$(LIBC):
	$(MAKE) -C lib

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# İki ayrı (BIN) kuralını sildik, sadece bu doğru olanı bıraktık
$(BIN): $(LIBC) $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBC) -o $(BIN)

$(ISO): $(BIN) grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(BIN) isodir/boot/$(BIN)
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir


hello.elf: apps/hello.asm
	$(AS) -f elf32 apps/hello.asm -o hello.o
	$(LD) -m elf_i386 hello.o -o hello.elf

run: $(ISO) hello.elf
	@echo "Gelismis Sanal Hard Disk (disk.img) olusturuluyor..."
	@dd if=/dev/zero of=disk.img bs=512 count=100 > /dev/null 2>&1
	@echo "Merhaba Hard Disk! Ben esdumanOS!" > message.txt
	@echo "Bu bir esdumanOS gizli metin belgesidir!" > gizli.txt
	@dd if=message.txt of=disk.img conv=notrunc > /dev/null 2>&1
	@python3 tools/inject.py disk.img hello.elf gizli.txt
	qemu-system-i386 -cdrom $(ISO) -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses

clean:
	$(MAKE) -C lib clean
	rm -f hello.o hello.elf
	rm -rf $(OBJS) $(BIN) $(ISO) isodir