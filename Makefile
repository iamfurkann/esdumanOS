ifndef ESDUMAN_ELF_KEY_HEX
$(error ERROR: ESDUMAN_ELF_KEY_HEX is not set! Must be exactly 64 hex characters (32 bytes AES key). Build aborted.)
endif

# Validate key length
ifneq ($(shell printf '%s' '$(ESDUMAN_ELF_KEY_HEX)' | wc -c | tr -d ' '),64)
$(error ERROR: ESDUMAN_ELF_KEY_HEX must be exactly 64 hex characters.)
endif

ARCH ?= x86

# Appended to CFLAGS in every arch branch. Exists so a target can re-invoke make
# with extra defines (see test_kernel) without having to re-quote all of CFLAGS.
EXTRA_CFLAGS ?=

CORE_OBJS = kernel/core/kernel.o \
			kernel/core/klog.o \
			kernel/core/uaccess.o \
            kernel/proc/signal.o \
            kernel/proc/process.o \
            kernel/proc/pipe.o \
			kernel/proc/elf_validate.o \
            kernel/proc/elf.o \
            kernel/syscall/syscall.o \
			kernel/syscall/sys_fs.o \
			kernel/syscall/sys_ipc.o \
			kernel/syscall/sys_process.o \
			kernel/syscall/sys_sec.o \
			kernel/syscall/sys_utils.o \
			kernel/security/passwd.o \
			kernel/security/security.o \
			kernel/security/entropy.o \
			kernel/security/stack_protect.o \
            src/resources/init_elf_data.o \
            src/resources/hello_elf_data.o \
            src/resources/clear_elf_data.o \
            src/resources/echo_elf_data.o \
			src/resources/sh_elf_data.o \
			src/resources/touch_elf_data.o \
			src/resources/rm_elf_data.o \
			src/resources/mv_elf_data.o \
			src/resources/cp_elf_data.o \
			src/resources/free_elf_data.o \
			src/resources/whoami_elf_data.o \
			src/resources/kill_elf_data.o \
			src/resources/grep_elf_data.o \
			src/resources/head_elf_data.o \
			src/resources/date_elf_data.o \
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
			crypto/sha256.o \
			crypto/hmac.o \
			crypto/pbkdf2.o \
			crypto/chacha20.o

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
			tests/kernel/test_concurrency.o \
            tests/kernel/test_paging.o \
            tests/kernel/test_pmm.o \
			tests/kernel/test_lifecycle.o \
			tests/kernel/test_fault.o \
            tests/kernel/test_syscall.o \
            tests/kernel/test_process.o \
			tests/kernel/test_signal.o \
			tests/kernel/test_elf.o \
            tests/kernel/test_crypto.o \
			tests/kernel/test_entropy.o \
			tests/kernel/test_bcache.o \
			src/resources/ktest_user_elf_data.o
ifeq ($(ARCH), x86)
    # x86
    CC = gcc
    AS = nasm
    LD = ld
    AR = ar

    CFLAGS = -m32 -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-pic -fno-pie -Wall -Wextra -Iinclude -O2 -c -DARCH_X86 -MMD -MP -DELF_ENCRYPTION_KEY=\"$(ESDUMAN_ELF_KEY_HEX)\" $(EXTRA_CFLAGS)
    ASFLAGS = -f elf32
    LDFLAGS = -m elf_i386 -T arch/x86/linker.ld -z noexecstack

    USER_CFLAGS = -m32 -nostdlib -ffreestanding -fno-pie -no-pie -e main -I include
    USER_LDFLAGS = -m elf_i386

    QEMU = qemu-system-i386
    QEMU_FLAGS = -cdrom $(ISO) -serial file:kernel_log.txt -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses
    
    ARCH_OBJS = arch/x86/boot/boot.o \
                arch/x86/cpu/gdt.o  \
                arch/x86/cpu/gdt_s.o \
                arch/x86/cpu/idt.o \
                arch/x86/cpu/idt_s.o \
                arch/x86/cpu/isr.o \
                arch/x86/cpu/timer.o \
                arch/x86/cpu/tss.o \
                mm/paging_s.o \
                drivers/tty.o \
                drivers/keyboard.o \
                drivers/ata.o \
                drivers/rtc.o \
				drivers/serial.o \

else ifeq ($(ARCH), riscv64)
    # RISC-V (64-bit)
    #
    # Scaffolding for a port that does not exist in this tree yet: the objects
    # below live under arch/riscv/, and there is no arch/riscv/. Kept because it
    # records the intended shape of the port, but guarded - without this check the
    # failure was a wall of "No rule to make target arch/riscv/boot/boot.o" and a
    # missing linker script, which says nothing about why.
    ifeq ($(wildcard arch/riscv),)
        $(error ARCH=riscv64 selected, but arch/riscv/ is not present in this tree. The RISC-V port has not been written yet; only ARCH=x86 builds.)
    endif

    CC = riscv64-unknown-elf-gcc
    AS = riscv64-unknown-elf-as
    LD = riscv64-unknown-elf-ld
    AR = riscv64-unknown-elf-ar

    CFLAGS = -march=rv64imac -mabi=lp64 -mcmodel=medany -nostdlib -nodefaultlibs -fno-builtin -Wall -Wextra -I include -c -DARCH_RISCV64 -DELF_ENCRYPTION_KEY=\"$(ESDUMAN_ELF_KEY_HEX)\"
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
    $(error "Unsupported architecture: $(ARCH). Please select x86 or riscv64.")
endif

export CC AS LD CFLAGS

OBJS = $(CORE_OBJS) $(ARCH_OBJS)

-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)

BIN = myos.bin
TEST_BIN = myos_test.bin
ISO = myos.iso
LIBC = lib/libc.a

# Wall-clock budget for a QEMU self-test run. A kernel panic parks the CPU with
# cli;hlt and never reaches isa-debug-exit, so without this the run hangs instead
# of failing. Raise it if PBKDF2_DEFAULT_ITERATIONS makes first boot slower.
#
# NOTE: the callers must use "timeout --foreground". Without it GNU timeout puts
# QEMU in its own process group, and QEMU's -serial stdio chardev then takes
# SIGTTOU from tcsetattr() and stops before the CPU ever starts - which looks
# exactly like a kernel hang, but with an empty -D log and no serial output.
QEMU_TEST_TIMEOUT ?= 300

# PBKDF2 cost used by self-test builds.
#
# hmac_sha256() no longer allocates per iteration, so this is no longer papering
# over a broken cost model - it only keeps the suite quick. A self-test run
# performs five derivations (two creating /etc/shadow at first boot, three more
# in test_security.c), and every one of them is wall-clock time under QEMU TCG.
#
# Set it to 600000 to run at the production cost; that should now finish inside
# QEMU_TEST_TIMEOUT, which it could not before.
PBKDF2_TEST_ITERATIONS ?= 100000

# Extra -cpu flags for the test_kernel run. Empty means QEMU's default i386 model,
# which exposes neither RDRAND nor SMEP/SMAP.
#
# It exists because the two entropy paths cannot both be reached from the targets
# we already have. test_smap uses "-cpu max", which brings RDRAND *and* SMAP; SMAP
# in turn makes selftest.c skip every kernel-mode module, so the ENTROPY_OK branch
# is never executed there. Asking for RDRAND on its own does reach it:
#
#   make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"
#
# If a QEMU build rejects that model, "-cpu max,-smap,-smep" is equivalent for
# this purpose.
QEMU_TEST_CPU ?=

.PHONY: all clean run run-dev debug restart reset-disk test test_kernel test_smap fuzz start

all: $(ISO)

.PHONY: force_libc

$(LIBC): force_libc
	$(MAKE) -C lib

force_libc:

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

apps/bin/sh.elf: apps/bin/sh.c
	$(CC) $(USER_CFLAGS) apps/bin/sh.c -o apps/bin/sh.elf

apps/bin/touch.elf: apps/bin/touch.c
	$(CC) $(USER_CFLAGS) apps/bin/touch.c -o apps/bin/touch.elf

apps/bin/rm.elf: apps/bin/rm.c
	$(CC) $(USER_CFLAGS) apps/bin/rm.c -o apps/bin/rm.elf

apps/bin/mv.elf: apps/bin/mv.c
	$(CC) $(USER_CFLAGS) apps/bin/mv.c -o apps/bin/mv.elf

apps/bin/cp.elf: apps/bin/cp.c
	$(CC) $(USER_CFLAGS) apps/bin/cp.c -o apps/bin/cp.elf

apps/bin/free.elf: apps/bin/free.c
	$(CC) $(USER_CFLAGS) apps/bin/free.c -o apps/bin/free.elf

apps/bin/whoami.elf: apps/bin/whoami.c
	$(CC) $(USER_CFLAGS) apps/bin/whoami.c -o apps/bin/whoami.elf

apps/bin/kill.elf: apps/bin/kill.c
	$(CC) $(USER_CFLAGS) apps/bin/kill.c -o apps/bin/kill.elf

apps/bin/grep.elf: apps/bin/grep.c
	$(CC) $(USER_CFLAGS) apps/bin/grep.c -o apps/bin/grep.elf

apps/bin/head.elf: apps/bin/head.c
	$(CC) $(USER_CFLAGS) apps/bin/head.c -o apps/bin/head.elf

apps/bin/date.elf: apps/bin/date.c
	$(CC) $(USER_CFLAGS) apps/bin/date.c -o apps/bin/date.elf


# Ring 3 half of the kernel self-test suite. Built, encrypted and embedded the
# same way as the /bin programs, but only linked into $(TEST_BIN).
tests/user/ktest_user.elf: tests/user/ktest_user.c
	$(CC) $(USER_CFLAGS) tests/user/ktest_user.c -o tests/user/ktest_user.elf

src/resources/ktest_user_elf_data.c: tests/user/ktest_user.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool tests/user/ktest_user.elf tests/user/ktest_user_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i tests/user/ktest_user_encrypted.elf | \
	sed 's/tests_user_ktest_user_encrypted_elf/ktest_user_elf/g' > src/resources/ktest_user_elf_data.c

tools/encrypt_tool: tools/encrypt_tool.c
	gcc tools/encrypt_tool.c -o tools/encrypt_tool -lcrypto

apps/init_encrypted.elf: apps/init.elf tools/encrypt_tool
	./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)

src/resources/init_elf_data.c: apps/init_encrypted.elf
	@mkdir -p src/resources
	xxd -i apps/init_encrypted.elf | \
	sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c

src/resources/hello_elf_data.c: hello.elf
	@mkdir -p src/resources
	@./tools/encrypt_tool hello.elf apps/hello_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/hello_encrypted.elf | \
	sed 's/apps_hello_encrypted_elf/hello_elf/g' > src/resources/hello_elf_data.c

src/resources/clear_elf_data.c: apps/bin/clear.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/clear.elf apps/bin/clear_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/clear_encrypted.elf | \
	sed 's/apps_bin_clear_encrypted_elf/clear_elf/g' > src/resources/clear_elf_data.c

src/resources/echo_elf_data.c: apps/bin/echo.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/echo.elf apps/bin/echo_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/echo_encrypted.elf | \
	sed 's/apps_bin_echo_encrypted_elf/echo_elf/g' > src/resources/echo_elf_data.c

src/resources/sh_elf_data.c: apps/bin/sh.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/sh.elf apps/bin/sh_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/sh_encrypted.elf | \
	sed 's/apps_bin_sh_encrypted_elf/sh_elf/g' > src/resources/sh_elf_data.c

src/resources/touch_elf_data.c: apps/bin/touch.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/touch.elf apps/bin/touch_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/touch_encrypted.elf | \
	sed 's/apps_bin_touch_encrypted_elf/touch_elf/g' > src/resources/touch_elf_data.c

src/resources/rm_elf_data.c: apps/bin/rm.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/rm.elf apps/bin/rm_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/rm_encrypted.elf | \
	sed 's/apps_bin_rm_encrypted_elf/rm_elf/g' > src/resources/rm_elf_data.c

src/resources/mv_elf_data.c: apps/bin/mv.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/mv.elf apps/bin/mv_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/mv_encrypted.elf | \
	sed 's/apps_bin_mv_encrypted_elf/mv_elf/g' > src/resources/mv_elf_data.c

src/resources/cp_elf_data.c: apps/bin/cp.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/cp.elf apps/bin/cp_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/cp_encrypted.elf | \
	sed 's/apps_bin_cp_encrypted_elf/cp_elf/g' > src/resources/cp_elf_data.c

src/resources/free_elf_data.c: apps/bin/free.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/free.elf apps/bin/free_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/free_encrypted.elf | \
	sed 's/apps_bin_free_encrypted_elf/free_elf/g' > src/resources/free_elf_data.c

src/resources/whoami_elf_data.c: apps/bin/whoami.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/whoami.elf apps/bin/whoami_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/whoami_encrypted.elf | \
	sed 's/apps_bin_whoami_encrypted_elf/whoami_elf/g' > src/resources/whoami_elf_data.c

src/resources/kill_elf_data.c: apps/bin/kill.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/kill.elf apps/bin/kill_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/kill_encrypted.elf | \
	sed 's/apps_bin_kill_encrypted_elf/kill_elf/g' > src/resources/kill_elf_data.c

src/resources/grep_elf_data.c: apps/bin/grep.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/grep.elf apps/bin/grep_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/grep_encrypted.elf | \
	sed 's/apps_bin_grep_encrypted_elf/grep_elf/g' > src/resources/grep_elf_data.c

src/resources/head_elf_data.c: apps/bin/head.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/head.elf apps/bin/head_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/head_encrypted.elf | \
	sed 's/apps_bin_head_encrypted_elf/head_elf/g' > src/resources/head_elf_data.c

src/resources/date_elf_data.c: apps/bin/date.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/date.elf apps/bin/date_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/date_encrypted.elf | \
	sed 's/apps_bin_date_encrypted_elf/date_elf/g' > src/resources/date_elf_data.c


test:
	@echo "--- Running Host Unit Tests ---"
	
	@gcc -Wall -Wextra -I./include -I./crypto -DARCH_X86 tests/host/c/test_crypto.c crypto/aes.c -o tests/host/test_crypto
	@./tests/host/test_crypto
	
	@gcc -Wall -Wextra -I./include -DARCH_X86 tests/host/c/test_hash.c -o tests/host/test_hash
	@./tests/host/test_hash

	@gcc -Wall -Wextra -I./include -DARCH_X86 tests/host/c/test_elf_validation.c kernel/proc/elf_validate.c -o tests/host/test_elf_validation
	@./tests/host/test_elf_validation

	@mkdir -p tests/host/bin
	@gcc tests/host/c/test_elf_sast.c -o tests/host/bin/test_elf_sast
	@./tests/host/bin/test_elf_sast

	@python3 -m unittest discover -s tests/host/python -p "test_*.py"

# $(OBJS) are shared with the production binary and make does not track CFLAGS
# changes, so a stale object built at the production PBKDF2 cost would silently
# be reused and the suite would time out again. Force a rebuild instead.
#
# WARNING: the objects left behind afterwards carry -DPBKDF2_DEV_ITERATIONS.
# Run `make clean` before building a production image, or myos.bin inherits the
# reduced iteration count. Splitting test and production object trees would
# remove this hazard for good.
test_kernel: hello.elf
	@rm -f $(OBJS) $(TEST_OBJS) $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(BIN) $(TEST_BIN)
	@$(MAKE) EXTRA_CFLAGS='-DPBKDF2_DEV_ITERATIONS=$(PBKDF2_TEST_ITERATIONS)' $(TEST_BIN)
	@echo "--- Running Kernel QEMU Self-Tests (PBKDF2=$(PBKDF2_TEST_ITERATIONS) iterations, CPU='$(QEMU_TEST_CPU)') ---"
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@echo "Merhaba Hard Disk! Ben esdumanOS!" > message.txt
	@echo "Bu bir esdumanOS gizli metin belgesidir!" > gizli.txt
	@dd if=message.txt of=disk.img bs=512 seek=2048 conv=notrunc > /dev/null 2>&1
	@if timeout --foreground $(QEMU_TEST_TIMEOUT) $(QEMU) -kernel $(TEST_BIN) $(QEMU_TEST_CPU) -append "kernel_pass=selftest" \
		-drive format=raw,file=disk.img,if=ide,index=0,media=disk \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-d int,cpu_reset -D qemu.log \
		-serial stdio -display none -no-reboot; then \
		echo "ERROR: QEMU exited unexpectedly!"; exit 1; \
	else \
		RET=$$?; \
		if [ $$RET -eq 33 ]; then \
			echo "ALL KERNEL TESTS PASSED! (All Modules Passed)"; exit 0; \
		elif [ $$RET -eq 35 ]; then \
			echo "KERNEL TESTS FAILED! (Some modules did not pass)"; exit 1; \
		elif [ $$RET -eq 124 ]; then \
			echo "KERNEL HUNG! No verdict within $(QEMU_TEST_TIMEOUT)s (panic halts the CPU forever)."; exit 1; \
		else \
			echo "KERNEL PANIC/CRASH DETECTED! (Exit Code: $$RET)"; exit 1; \
		fi; \
	fi

# SMAP test target: runs the full self-test suite on a SMAP/SMEP-capable CPU.
# The default QEMU CPU exposes neither feature, so this is the only configuration
# in which the uaccess paths are actually enforced by hardware. Gated on the same
# exit codes as test_kernel: a supervisor access to user memory that bypasses
# copy_to_user() faults here and must fail the build rather than be swallowed.
test_smap:
	@rm -f $(OBJS) $(TEST_OBJS) $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(BIN) $(TEST_BIN)
	@$(MAKE) EXTRA_CFLAGS='-DPBKDF2_DEV_ITERATIONS=$(PBKDF2_TEST_ITERATIONS)' $(TEST_BIN)
	@echo "[SMAP TEST] Running kernel self-tests with -cpu max (SMEP/SMAP enabled)..."
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@if timeout --foreground $(QEMU_TEST_TIMEOUT) $(QEMU) -kernel $(TEST_BIN) -cpu max -append "kernel_pass=selftest" \
		-drive format=raw,file=disk.img,if=ide,index=0,media=disk \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-serial stdio -display none -no-reboot; then \
		echo "[SMAP TEST] ERROR: QEMU exited unexpectedly!"; exit 1; \
	else \
		RET=$$?; \
		if [ $$RET -eq 33 ]; then \
			echo "[SMAP TEST] ALL KERNEL TESTS PASSED under SMEP/SMAP."; exit 0; \
		elif [ $$RET -eq 35 ]; then \
			echo "[SMAP TEST] KERNEL TESTS FAILED under SMEP/SMAP!"; exit 1; \
		elif [ $$RET -eq 124 ]; then \
			echo "[SMAP TEST] KERNEL HUNG under SMEP/SMAP! (likely a supervisor access to user memory -> panic)"; exit 1; \
		else \
			echo "[SMAP TEST] KERNEL PANIC/CRASH under SMEP/SMAP! (Exit Code: $$RET)"; exit 1; \
		fi; \
	fi

fuzz:
	@echo "--- Starting Fuzzing (libFuzzer) ---"
	@mkdir -p tests/host/corpus
	@clang -g -O1 -fsanitize=fuzzer,address -I./include -DARCH_X86 tests/host/c/fuzz_parser.c kernel/proc/elf_validate.c -o tests/host/fuzz_parser
	@echo "Testing known crash (corpus) files, then generating new attack vectors..."
	@./tests/host/fuzz_parser tests/host/corpus -max_total_time=10

run: apps/init.elf tools/encrypt_tool $(ISO) hello.elf apps/bin/clear.elf apps/bin/echo.elf apps/bin/sh.elf
	@echo "--- [1/4] Encrypting ELF binaries..."
	@./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool hello.elf apps/hello_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/clear.elf apps/bin/clear_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/echo.elf apps/bin/echo_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/sh.elf apps/bin/sh_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)

	@echo "--- [2/4] Generating C data files..."
	@mkdir -p src/resources
	@xxd -i apps/init_encrypted.elf | sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c
	@xxd -i apps/hello_encrypted.elf | sed 's/apps_hello_encrypted_elf/hello_elf/g' > src/resources/hello_elf_data.c
	@xxd -i apps/bin/clear_encrypted.elf | sed 's/apps_bin_clear_encrypted_elf/clear_elf/g' > src/resources/clear_elf_data.c
	@xxd -i apps/bin/echo_encrypted.elf | sed 's/apps_bin_echo_encrypted_elf/echo_elf/g' > src/resources/echo_elf_data.c
	@xxd -i apps/bin/sh_encrypted.elf | sed 's/apps_bin_sh_encrypted_elf/sh_elf/g' > src/resources/sh_elf_data.c
	
	@echo "--- [3/4] Rebuilding kernel (with encrypted ELF binaries)..."
	@$(MAKE) $(ISO)
	@echo "--- [4/4] Preparing disk image and launching QEMU..."
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	$(QEMU) $(QEMU_FLAGS)

clean:
	$(MAKE) -C lib clean
	rm -f apps/bin/hello.o apps/bin/hello.elf hello.elf apps/init.o apps/init.elf apps/init_encrypted.elf tools/encrypt_tool apps/hello_encrypted.elf
	rm -f apps/bin/*.elf apps/bin/*_encrypted.elf
	rm -f tests/user/*.elf
	rm -rf src/resources/*_data.c src/resources/*.o
	rm -f disk.img kernel_log.txt
	rm -f tests/host/test_runner tests/host/test_crypto tests/host/test_hash tests/host/test_elf_validation tests/host/fuzz_parser
	rm -rf $(OBJS) $(TEST_OBJS) $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(BIN) $(TEST_BIN) $(ISO) isodir message.txt gizli.txt

# =============================================================================
# Developer Mode Targets
# =============================================================================

# Fast development build + run. Reduces PBKDF2 to 1000 iterations for instant
# password setup. Preserves disk.img across runs to skip first-boot setup.
# Usage: make run-dev
run-dev: CFLAGS += -DPBKDF2_DEV_ITERATIONS=1000
run-dev: apps/init.elf tools/encrypt_tool $(ISO) hello.elf apps/bin/clear.elf apps/bin/echo.elf apps/bin/sh.elf
	@echo "--- [DEV 1/4] Encrypting ELF binaries..."
	@./tools/encrypt_tool apps/init.elf apps/init_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool hello.elf apps/hello_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/clear.elf apps/bin/clear_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/echo.elf apps/bin/echo_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@./tools/encrypt_tool apps/bin/sh.elf apps/bin/sh_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@echo "--- [DEV 2/4] Generating C data files..."
	@mkdir -p src/resources
	@xxd -i apps/init_encrypted.elf | sed 's/apps_init_encrypted_elf/init_elf/g' > src/resources/init_elf_data.c
	@xxd -i apps/hello_encrypted.elf | sed 's/apps_hello_encrypted_elf/hello_elf/g' > src/resources/hello_elf_data.c
	@xxd -i apps/bin/clear_encrypted.elf | sed 's/apps_bin_clear_encrypted_elf/clear_elf/g' > src/resources/clear_elf_data.c
	@xxd -i apps/bin/echo_encrypted.elf | sed 's/apps_bin_echo_encrypted_elf/echo_elf/g' > src/resources/echo_elf_data.c
	@xxd -i apps/bin/sh_encrypted.elf | sed 's/apps_bin_sh_encrypted_elf/sh_elf/g' > src/resources/sh_elf_data.c
	@echo "--- [DEV 3/4] Rebuilding kernel (dev mode, PBKDF2=1000)..."
	@$(MAKE) CFLAGS="$(CFLAGS)" $(ISO)
	@echo "--- [DEV 4/4] Launching QEMU..."
	@test -f disk.img || dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	$(QEMU) $(QEMU_FLAGS)

# GDB debug target: starts QEMU with GDB stub, waits for debugger connection.
# Usage: make debug (then in another terminal: gdb -ex 'target remote :1234' myos.bin)
debug: $(ISO)
	@test -f disk.img || dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@echo "--- Waiting for GDB connection on :1234 ---"
	@echo "--- Run: gdb -ex 'target remote :1234' myos.bin ---"
	$(QEMU) $(QEMU_FLAGS) -s -S

# Quick restart: just launch QEMU with existing ISO and disk.img (no rebuild)
# Usage: make restart
restart:
	$(QEMU) $(QEMU_FLAGS)

# Reset dev disk: delete disk.img to force first-boot setup on next run-dev
reset-disk:
	rm -f disk.img
	@echo "disk.img removed. Next 'make run-dev' will trigger first-boot setup."
