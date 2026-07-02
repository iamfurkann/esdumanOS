ARCH ?= x86

CORE_OBJS = kernel/kernel.o \
            kernel/signal.o \
            kernel/process.o \
            kernel/syscall.o \
            kernel/pipe.o \
            kernel/elf.o \
            kernel/shell.o \
            kernel/security.o \
            tests/kernel/selftest.o \
            src/resources/init_elf_data.o \
            fs/vfs.o \
            fs/crypto_fs.o \
            mm/pmm.o \
            mm/paging.o \
            mm/kheap.o \
            lib/stdio.o \
            lib/utils.o \
            lib/utils2.o \
            lib/stack.o \
            crypto/aes.o \

ifeq ($(ARCH), x86)
    # x86
    CC = gcc
    AS = nasm
    LD = ld
    AR = ar

    CFLAGS = -m32 -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-stack-protector -Wall -Wextra -I include -I src/libc -c -DARCH_X86
    ASFLAGS = -f elf32
    LDFLAGS = -m elf_i386 -T arch/x86/linker.ld -z noexecstack

    USER_CFLAGS = -m32 -nostdlib -ffreestanding -fno-pie -no-pie -e main -I include
    USER_LDFLAGS = -m elf_i386

    QEMU = qemu-system-i386
    QEMU_FLAGS = -cdrom $(ISO) -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses
    
    ARCH_OBJS = arch/x86/boot/boot.o \
                arch/x86/cpu/gdt.o  \
                arch/x86/cpu/gdt_s.o \
                arch/x86/cpu/idt.o \
                arch/x86/cpu/idt_s.o \
                arch/x86/cpu/isr.o \
                arch/x86/cpu/timer.o \
                arch/x86/cpu/tss.o \
                arch/x86/cpu/user_mode.o \
                mm/paging_s.o \
                drivers/tty.o \
                drivers/keyboard.o \
                drivers/ata.o \
                drivers/rtc.o

else ifeq ($(ARCH), riscv64)
    # RISC-V (64-bit)
    CC = riscv64-unknown-elf-gcc
    AS = riscv64-unknown-elf-as
    LD = riscv64-unknown-elf-ld
    AR = riscv64-unknown-elf-ar

    CFLAGS = -march=rv64imac -mabi=lp64 -mcmodel=medany -nostdlib -nodefaultlibs -fno-builtin -Wall -Wextra -I include -I src/libc -c -DARCH_RISCV64
    ASFLAGS = -march=rv64imac -mabi=lp64
    LDFLAGS = -T arch/riscv/linker.ld

    USER_CFLAGS = -march=rv64imac -mabi=lp64 -nostdlib -ffreestanding -fno-pie -no-pie -e main -I include
    USER_LDFLAGS = -m elf64lriscv

    QEMU = qemu-system-riscv64
    QEMU_FLAGS = -machine virt -bios default -kernel myos.bin -drive format=raw,file=disk.img,if=none,id=d0 -device virtio-blk-device,drive=d0 -display curses

    ARCH_OBJS = arch/riscv/boot/boot.o \
                arch/riscv/cpu/trap.o \
                arch/riscv/drivers/uart.o
else
    $(error "Desteklenmeyen Mimari: $(ARCH). Lutfen x86 veya riscv64 secin.")
endif

export CC AS LD CFLAGS

OBJS = $(CORE_OBJS) $(ARCH_OBJS)

BIN = myos.bin
ISO = myos.iso
LIBC = lib/libc.a

all: $(ISO)

$(LIBC):
	$(MAKE) -C lib

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN): $(LIBC) $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBC) -o $(BIN)

$(ISO): $(BIN) grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(BIN) isodir/boot/$(BIN)
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir

start:
	$(QEMU) $(QEMU_FLAGS)

hello.elf: apps/hello.asm
	$(AS) $(ASFLAGS) apps/hello.asm -o hello.o
	$(LD) $(USER_LDFLAGS) hello.o -o hello.elf

apps/init.elf: apps/init.c
	gcc -m32 -nostdlib -ffreestanding -fno-pie -no-pie \
	-e _start -I include apps/init.c -o apps/init.elf

tools/encrypt_tool: tools/encrypt_tool.c
	gcc tools/encrypt_tool.c -o tools/encrypt_tool -lcrypto

apps/init_encrypted.elf: apps/init.elf tools/encrypt_tool
	./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy

src/resources/init_elf_data.c: apps/init_encrypted.elf
	@mkdir -p src/resources
	xxd -i apps/init_encrypted.elf | \
	sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c

test:
	@echo "--- Host Unit Tests Calistiriliyor ---"
	@gcc tests/host/test_hash.c -o tests/host/test_runner
	@./tests/host/test_runner
	@python3 -m unittest discover -s tests/host -p "test_*.py"

test_kernel: $(BIN)
	@echo "--- Kernel QEMU Self-Test Calistiriliyor ---"
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@$(QEMU) -kernel $(BIN) -append "kernel_pass=selftest" \
        -drive format=raw,file=disk.img,if=ide,index=0,media=disk \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -serial stdio -display none

run: apps/init.elf tools/encrypt_tool $(ISO) hello.elf
	@echo "--- [1/4] init.elf sifreli pakete donusturuluyor..."
	@./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@echo "--- [2/4] C veri dosyasi uretiliyor..."
	@mkdir -p src/resources
	@xxd -i apps/init_encrypted.elf | \
		sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c
	@echo "--- [3/4] Kernel yeniden derleniyor (sifreli ELF ile)..."
	@$(MAKE) $(ISO)
	@echo "--- [4/4] Disk imaji hazirlanip QEMU baslatiliyor..."
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@echo "Merhaba Hard Disk! Ben esdumanOS!" > message.txt
	@echo "Bu bir esdumanOS gizli metin belgesidir!" > gizli.txt
	@dd if=message.txt of=disk.img conv=notrunc > /dev/null 2>&1
	@python3 tools/inject.py disk.img hello.elf gizli.txt
	$(QEMU) $(QEMU_FLAGS)

clean:
	$(MAKE) -C lib clean
	rm -f hello.o hello.elf apps/init.elf apps/init_encrypted.elf tools/encrypt_tool
	rm -f src/resources/init_elf_data.c
	rm -f tests/host/test_runner
	rm -rf $(OBJS) $(BIN) $(ISO) isodir message.txt gizli.txt