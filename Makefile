ARCH ?= x86

CORE_OBJS = kernel/core/kernel.o \
			kernel/core/klog.o \
            kernel/proc/signal.o \
            kernel/proc/process.o \
			kernel/proc/pipe.o \
            kernel/proc/elf.o \
            kernel/syscall/syscall.o \
            kernel/security/security.o \
			kernel/security/passwd.o \
            src/resources/init_elf_data.o \
            src/resources/hello_elf_data.o \
            src/resources/clear_elf_data.o \
            src/resources/echo_elf_data.o \
			fs/bcache.o \
            fs/vfs.o \
            fs/crypto_fs.o \
			fs/devfs.o \
            mm/pmm.o \
            mm/paging.o \
            mm/kheap.o \
            lib/stdio.o \
            lib/stack.o \
			lib/utils.o \
			lib/utils2.o \
            crypto/aes.o \
			crypto/sha256.o

TEST_OBJS = tests/kernel/selftest.o \
            tests/kernel/test_string.o \
            tests/kernel/test_memory.o \
            tests/kernel/test_pipe.o \
            tests/kernel/test_vfs.o \
			tests/kernel/test_devfs.o \
			tests/kernel/test_passwd.o \
            tests/kernel/test_security.o \
            tests/kernel/test_stress.o \
            tests/kernel/test_adversarial.o \
            tests/kernel/test_integration.o \
            tests/kernel/test_regression.o \
			tests/kernel/test_concurrency.o

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
TEST_BIN = myos_test.bin
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

$(TEST_BIN): $(LIBC) $(OBJS) $(TEST_OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(TEST_OBJS) $(LIBC) -o $(TEST_BIN)

$(ISO): $(BIN) grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(BIN) isodir/boot/$(BIN)
	cp grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir

start:
	$(QEMU) $(QEMU_FLAGS)

hello.elf: apps/bin/hello.c
	$(CC) $(USER_CFLAGS) apps/bin/hello.c -o hello.elf

apps/init.elf: apps/init.c
	gcc -m32 -nostdlib -ffreestanding -fno-pie -no-pie \
	-e _start -I include apps/init.c -o apps/init.elf

apps/bin/clear.elf: apps/bin/clear.c
	$(CC) $(USER_CFLAGS) apps/bin/clear.c -o apps/bin/clear.elf

apps/bin/echo.elf: apps/bin/echo.c
	$(CC) $(USER_CFLAGS) apps/bin/echo.c -o apps/bin/echo.elf

tools/encrypt_tool: tools/encrypt_tool.c
	gcc tools/encrypt_tool.c -o tools/encrypt_tool -lcrypto

apps/init_encrypted.elf: apps/init.elf tools/encrypt_tool
	./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy

src/resources/init_elf_data.c: apps/init_encrypted.elf
	@mkdir -p src/resources
	xxd -i apps/init_encrypted.elf | \
	sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c

src/resources/hello_elf_data.c: hello.elf
	@mkdir -p src/resources
	@./tools/encrypt_tool hello.elf apps/hello_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@xxd -i apps/hello_encrypted.elf | \
	sed 's/apps_hello_encrypted_elf/hello_elf/g' > src/resources/hello_elf_data.c

src/resources/clear_elf_data.c: apps/bin/clear.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/clear.elf apps/bin/clear_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@xxd -i apps/bin/clear_encrypted.elf | \
	sed 's/apps_bin_clear_encrypted_elf/clear_elf/g' > src/resources/clear_elf_data.c

src/resources/echo_elf_data.c: apps/bin/echo.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/echo.elf apps/bin/echo_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@xxd -i apps/bin/echo_encrypted.elf | \
	sed 's/apps_bin_echo_encrypted_elf/echo_elf/g' > src/resources/echo_elf_data.c

test:
	@echo "--- Host Unit Tests Calistiriliyor ---"
	
	@gcc -Wall -Wextra -I./include -I./crypto -DARCH_X86 tests/host/c/test_crypto.c crypto/aes.c -o tests/host/test_crypto
	@./tests/host/test_crypto
	
	@gcc -Wall -Wextra -I./include -DARCH_X86 tests/host/c/test_hash.c -o tests/host/test_hash
	@./tests/host/test_hash

	@mkdir -p tests/host/bin
	@gcc tests/host/c/test_elf_sast.c -o tests/host/bin/test_elf_sast
	@./tests/host/bin/test_elf_sast

	@python3 -m unittest discover -s tests/host/python -p "test_*.py"

test_kernel: $(TEST_BIN) hello.elf
	@echo "--- Kernel QEMU Self-Test Calistiriliyor ---"
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@echo "Merhaba Hard Disk! Ben esdumanOS!" > message.txt
	@echo "Bu bir esdumanOS gizli metin belgesidir!" > gizli.txt
	@dd if=message.txt of=disk.img conv=notrunc > /dev/null 2>&1
	@if $(QEMU) -kernel $(TEST_BIN) -append "kernel_pass=selftest" \
		-drive format=raw,file=disk.img,if=ide,index=0,media=disk \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-serial stdio -display none; then \
		echo "HATA: QEMU beklenmedik sekilde kapandi!"; exit 1; \
	else \
		RET=$$?; \
		if [ $$RET -eq 33 ]; then \
			echo "KERNEL TESTLERI KUSURSUZ! (Tum Moduller Gecti)"; exit 0; \
		elif [ $$RET -eq 35 ]; then \
			echo "KERNEL TESTLERI BASARISIZ! (Bazi testler kaldi)"; exit 1; \
		else \
			echo "KERNEL PANIC/COKME YASANDI! (Exit Code: $$RET)"; exit 1; \
		fi; \
	fi

fuzz:
	@echo "--- Fuzzing (libFuzzer) Baslatiliyor ---"
	@mkdir -p tests/host/corpus
	@clang -g -O1 -fsanitize=fuzzer,address -I./include -DARCH_X86 tests/host/c/fuzz_parser.c -o tests/host/fuzz_parser
	@echo "Bilinen 'Crash' (Zero-Day) dosyalari (Corpus) test ediliyor, ardindan yeni saldirilar uretilecek..."
	@./tests/host/fuzz_parser tests/host/corpus -max_total_time=10

run: apps/init.elf tools/encrypt_tool $(ISO) hello.elf apps/bin/clear.elf apps/bin/echo.elf
	@echo "--- [1/4] ELF dosyalari sifreli pakete donusturuluyor..."
	@./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@./tools/encrypt_tool hello.elf apps/hello_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@./tools/encrypt_tool apps/bin/clear.elf apps/bin/clear_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	@./tools/encrypt_tool apps/bin/echo.elf apps/bin/echo_encrypted.elf dF8pQ2mX7kL4zR9tN1cV5wHy
	
	@echo "--- [2/4] C veri dosyalari uretiliyor..."
	@mkdir -p src/resources
	@xxd -i apps/init_encrypted.elf | sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c
	@xxd -i apps/hello_encrypted.elf | sed 's/apps_hello_encrypted_elf/hello_elf/g' > src/resources/hello_elf_data.c
	@xxd -i apps/bin/clear_encrypted.elf | sed 's/apps_bin_clear_encrypted_elf/clear_elf/g' > src/resources/clear_elf_data.c
	@xxd -i apps/bin/echo_encrypted.elf | sed 's/apps_bin_echo_encrypted_elf/echo_elf/g' > src/resources/echo_elf_data.c
	
	@echo "--- [3/4] Kernel yeniden derleniyor (sifreli ELF ile)..."
	@$(MAKE) $(ISO)
	@echo "--- [4/4] Disk imaji hazirlanip QEMU baslatiliyor..."
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	$(QEMU) $(QEMU_FLAGS)

clean:
	$(MAKE) -C lib clean
	rm -f apps/bin/hello.o apps/bin/hello.elf hello.elf apps/init.o apps/init.elf apps/init_encrypted.elf tools/encrypt_tool apps/hello_encrypted.elf
	rm -f apps/bin/*.elf apps/bin/*_encrypted.elf src/resources/clear_elf_data.c src/resources/echo_elf_data.c
	rm -f tests/host/test_runner tests/host/test_crypto tests/host/test_hash tests/host/fuzz_parser
	rm -rf $(OBJS) $(TEST_OBJS) $(BIN) $(TEST_BIN) $(ISO) isodir message.txt gizli.txt