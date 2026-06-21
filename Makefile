CC = gcc
AS = nasm
LD = ld

# EXPORT ekliyoruz ki src/libc/Makefile bu derleyiciyi ve flag'leri alabilsin!
export CC AS LD

# Include kısmına libc klasörünü de ekledik ki libft.h her yerden bulunsun
CFLAGS = -m32 -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-stack-protector -Wall -Wextra -I include -I src/libc -c
export CFLAGS

ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker/linker.ld

OBJS = boot/boot.o \
        src/kernel/kernel.o \
        src/kernel/signal.o \
        src/drivers/tty.o \
        src/drivers/keyboard.o \
        src/drivers/ata.o \
        src/drivers/rtc.o \
        src/std/stdio.o \
        src/std/utils.o \
        src/std/utils2.o \
        src/std/stack.o \
        src/cpu/gdt.o  \
        src/cpu/gdt_s.o \
        src/cpu/idt.o \
        src/cpu/idt_s.o \
        src/cpu/isr.o \
        src/cpu/timer.o \
        src/mm/pmm.o \
        src/mm/paging.o \
        src/mm/paging_s.o \
        src/mm/kheap.o \
        src/tasking/tss.o \
        src/tasking/user_mode.o \
        src/tasking/process.o \
        src/tasking/elf.o \
        src/fs/vfs.o

BIN = myos.bin
ISO = myos.iso
LIBC = src/libc/libc.a

all: $(ISO)

# libc.a'nın derlenme kuralı
$(LIBC):
	$(MAKE) -C src/libc

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


hello.elf: src/user/hello.asm
	$(AS) -f elf32 src/user/hello.asm -o hello.o
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
	$(MAKE) -C src/libc clean
	rm -f hello.o hello.elf
	rm -rf $(OBJS) $(BIN) $(ISO) isodir