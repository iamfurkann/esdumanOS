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

# Build flavour, and the object tree that belongs to it.
#
# Test builds compile the same sources with -DPBKDF2_DEV_ITERATIONS, which make
# cannot see: a flag change does not make an object out of date. Objects were
# therefore shared between `make` and `make test_kernel`, and the only defence
# was deleting them before every test run - which is why a full rebuild cost two
# minutes, and why a production image built without `make clean` first would
# quietly inherit the reduced iteration count.
#
# Separate trees remove the hazard by construction rather than by discipline, and
# incremental builds work again: change one file, compile one file.
BUILD ?= prod
BUILD_DIR := build/$(BUILD)

CORE_SRCS = kernel/core/kernel.c \
			kernel/core/klog.c \
			kernel/core/uaccess.c \
            kernel/proc/signal.c \
            kernel/proc/process.c \
            kernel/proc/pipe.c \
			kernel/proc/elf_validate.c \
            kernel/proc/elf.c \
            kernel/syscall/syscall.c \
			kernel/syscall/sys_fs.c \
			kernel/syscall/sys_ipc.c \
			kernel/syscall/sys_mem.c \
			kernel/syscall/sys_process.c \
			kernel/syscall/sys_sec.c \
			kernel/syscall/sys_utils.c \
			kernel/security/passwd.c \
			kernel/security/security.c \
			kernel/security/entropy.c \
			kernel/security/stack_protect.c \
            src/resources/init_elf_data.c \
            src/resources/hello_elf_data.c \
            src/resources/clear_elf_data.c \
            src/resources/echo_elf_data.c \
			src/resources/sh_elf_data.c \
			src/resources/touch_elf_data.c \
			src/resources/rm_elf_data.c \
			src/resources/mv_elf_data.c \
			src/resources/cp_elf_data.c \
			src/resources/free_elf_data.c \
			src/resources/whoami_elf_data.c \
			src/resources/kill_elf_data.c \
			src/resources/grep_elf_data.c \
			src/resources/head_elf_data.c \
			src/resources/wc_elf_data.c \
			src/resources/date_elf_data.c \
			src/resources/stat_elf_data.c \
			fs/bcache.c \
            fs/vfs.c \
            fs/crypto_fs.c \
			fs/devfs.c \
            mm/pmm.c \
            mm/paging.c \
            mm/kheap.c \
            lib/stdio.c \
            lib/stack.c \
			lib/utils.c \
			lib/utils2.c \
            crypto/aes.c \
			crypto/sha256.c \
			crypto/hmac.c \
			crypto/pbkdf2.c \
			crypto/chacha20.c

CORE_OBJS = $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)

TEST_SRCS = tests/kernel/selftest.c \
            tests/kernel/test_string.c \
            tests/kernel/test_memory.c \
            tests/kernel/test_pipe.c \
            tests/kernel/test_vfs.c \
			tests/kernel/test_devfs.c \
			tests/kernel/test_passwd.c \
            tests/kernel/test_security.c \
            tests/kernel/test_stress.c \
            tests/kernel/test_adversarial.c \
            tests/kernel/test_integration.c \
            tests/kernel/test_regression.c \
			tests/kernel/test_concurrency.c \
            tests/kernel/test_paging.c \
            tests/kernel/test_pmm.c \
			tests/kernel/test_lifecycle.c \
			tests/kernel/test_fork.c \
			tests/kernel/test_cow.c \
			tests/kernel/test_umem.c \
			tests/kernel/test_fault.c \
            tests/kernel/test_syscall.c \
            tests/kernel/test_klog.c \
            tests/kernel/test_tty.c \
            tests/kernel/test_process.c \
			tests/kernel/test_signal.c \
			tests/kernel/test_reap.c \
			tests/kernel/test_elf.c \
            tests/kernel/test_crypto.c \
			tests/kernel/test_entropy.c \
			tests/kernel/test_bcache.c \
			tests/kernel/test_time.c \
			src/resources/ktest_user_elf_data.c \
			src/resources/ktest_crash_elf_data.c \
			src/resources/ktest_signal_elf_data.c

TEST_OBJS = $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
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
    
    ARCH_ASM_SRCS = arch/x86/boot/boot.asm \
                arch/x86/cpu/gdt_s.asm \
                arch/x86/cpu/idt_s.asm \
                mm/paging_s.asm

    ARCH_C_SRCS = arch/x86/cpu/gdt.c \
                arch/x86/cpu/idt.c \
                arch/x86/cpu/isr.c \
                arch/x86/cpu/timer.c \
                arch/x86/cpu/tss.c \
                drivers/tty.c \
                drivers/keyboard.c \
                drivers/ata.c \
                drivers/rtc.c \
                drivers/serial.c

    ARCH_OBJS = $(ARCH_C_SRCS:%.c=$(BUILD_DIR)/%.o) \
                $(ARCH_ASM_SRCS:%.asm=$(BUILD_DIR)/%.o)

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

    ARCH_ASM_SRCS =
    ARCH_C_SRCS = arch/riscv/boot/boot.c \
                arch/riscv/cpu/trap.c \
                arch/riscv/drivers/uart.c

    ARCH_OBJS = $(ARCH_C_SRCS:%.c=$(BUILD_DIR)/%.o)
else
    $(error "Unsupported architecture: $(ARCH). Please select x86 or riscv64.")
endif

export CC AS LD CFLAGS

OBJS = $(CORE_OBJS) $(ARCH_OBJS)

-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)

BIN = myos.bin
TEST_BIN = myos_test.bin
LIBC = $(BUILD_DIR)/lib/libc.a

# Release version, read out of the one place that defines it.
#
# include/kernel.h is the single source of truth: the kernel builds its own
# banner string from these same macros. Deriving the ISO name from them means a
# version bump touches exactly one file, instead of leaving a second copy here to
# drift out of step - which is the failure mode this build already had for the
# PIT rate and the syscall count.
#
# BIN stays myos.bin on purpose: grub/grub.cfg names it, and a mismatch between
# the two produces an ISO that fails to boot rather than a build error.
# The \# escapes are required: make treats an unescaped # as a comment even inside
# a $(shell ...) call, which silently truncates the sed script mid-expression.
OS_VER_MAJOR := $(shell sed -n 's/^\#define OS_VERSION_MAJOR *\([0-9][0-9]*\).*/\1/p' include/kernel.h)
OS_VER_MINOR := $(shell sed -n 's/^\#define OS_VERSION_MINOR *\([0-9][0-9]*\).*/\1/p' include/kernel.h)
OS_VER_PATCH := $(shell sed -n 's/^\#define OS_VERSION_PATCH *\([0-9][0-9]*\).*/\1/p' include/kernel.h)
OS_VER_PRE   := $(shell sed -n 's/^\#define OS_VERSION_PRE  *"\(.*\)".*/\1/p' include/kernel.h)
OS_VERSION   := $(OS_VER_MAJOR).$(OS_VER_MINOR).$(OS_VER_PATCH)$(OS_VER_PRE)

# Fail loudly rather than silently producing "esdumanOS-v..iso" if the parse breaks.
ifeq ($(strip $(OS_VER_MAJOR)),)
$(error Could not read OS_VERSION_MAJOR from include/kernel.h)
endif
ifeq ($(strip $(OS_VER_MINOR)),)
$(error Could not read OS_VERSION_MINOR from include/kernel.h)
endif
ifeq ($(strip $(OS_VER_PATCH)),)
$(error Could not read OS_VERSION_PATCH from include/kernel.h)
endif

ISO = esdumanOS-v$(OS_VERSION).iso

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

# Run a single test module instead of all of them: make test_kernel MODULE=fork
#
# A full run boots the OS and executes every module, which under nested emulation
# costs minutes whether or not the change being tested touches any of it. The
# name is appended to the kernel command line and selftest.c looks it up in its
# module table; an unknown name lists what is available and fails, rather than
# running nothing and reporting a pass.
#
# "ring3" runs only the user-mode payload. An empty MODULE runs everything, which
# is what CI does and what a release has to pass.
MODULE ?=
ifeq ($(strip $(MODULE)),)
    KERNEL_PASS := selftest
else
    KERNEL_PASS := selftest:$(strip $(MODULE))
endif

.PHONY: all clean run run-dev debug restart reset-disk test test_kernel test_smap fuzz start

all: $(ISO)

.PHONY: force_libc

# libc goes into the same per-flavour tree as everything else. It carries no
# flag that currently differs between the two, but sharing one archive between
# builds compiled differently is exactly the hazard this split exists to remove,
# and it would be found the hard way the first time it stopped being true.
#
# The delegation is unconditional on purpose. Making it depend on lib/*.c instead
# would skip the sub-make when only a header had changed, and headers are half of
# what libft is compiled against - the sub-make is the only thing that knows its
# own dependencies, so it gets asked every time and decides for itself. With
# --no-print-directory it says nothing at all when there is nothing to do.
$(LIBC): force_libc
	@$(MAKE) --no-print-directory -C lib BUILD_DIR=../$(BUILD_DIR)/lib

force_libc:

# The mkdir is per-object rather than an order-only prerequisite on the directory:
# a directory's timestamp changes whenever anything is written into it, which
# would make every object in it look out of date on the next build.
$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
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

apps/bin/wc.elf: apps/bin/wc.c
	$(CC) $(USER_CFLAGS) apps/bin/wc.c -o apps/bin/wc.elf

apps/bin/date.elf: apps/bin/date.c
	$(CC) $(USER_CFLAGS) apps/bin/date.c -o apps/bin/date.elf

apps/bin/stat.elf: apps/bin/stat.c
	$(CC) $(USER_CFLAGS) apps/bin/stat.c -o apps/bin/stat.elf


# Ring 3 half of the kernel self-test suite. Built, encrypted and embedded the
# same way as the /bin programs, but only linked into $(TEST_BIN).
tests/user/ktest_user.elf: tests/user/ktest_user.c
	$(CC) $(USER_CFLAGS) tests/user/ktest_user.c -o tests/user/ktest_user.elf

# Faults on purpose, so the crash-teardown path can be tested. Embedded only in
# $(TEST_BIN) - it must never reach the production image.
tests/user/ktest_crash.elf: tests/user/ktest_crash.c
	$(CC) $(USER_CFLAGS) tests/user/ktest_crash.c -o tests/user/ktest_crash.elf

src/resources/ktest_crash_elf_data.c: tests/user/ktest_crash.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool tests/user/ktest_crash.elf tests/user/ktest_crash_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i tests/user/ktest_crash_encrypted.elf | \
	sed 's/tests_user_ktest_crash_encrypted_elf/ktest_crash_elf/g' > src/resources/ktest_crash_elf_data.c

# Signals itself fatally, so the self-signalled half of the default action can be
# tested. Only a real Ring 3 process returns through syscall_handler(), which is
# where apply_default_signal_action() runs. Embedded only in $(TEST_BIN).
tests/user/ktest_signal.elf: tests/user/ktest_signal.c
	$(CC) $(USER_CFLAGS) tests/user/ktest_signal.c -o tests/user/ktest_signal.elf

src/resources/ktest_signal_elf_data.c: tests/user/ktest_signal.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool tests/user/ktest_signal.elf tests/user/ktest_signal_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i tests/user/ktest_signal_encrypted.elf | \
	sed 's/tests_user_ktest_signal_encrypted_elf/ktest_signal_elf/g' > src/resources/ktest_signal_elf_data.c

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

src/resources/wc_elf_data.c: apps/bin/wc.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/wc.elf apps/bin/wc_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/wc_encrypted.elf | \
	sed 's/apps_bin_wc_encrypted_elf/wc_elf/g' > src/resources/wc_elf_data.c

src/resources/date_elf_data.c: apps/bin/date.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/date.elf apps/bin/date_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/date_encrypted.elf | \
	sed 's/apps_bin_date_encrypted_elf/date_elf/g' > src/resources/date_elf_data.c

src/resources/stat_elf_data.c: apps/bin/stat.elf tools/encrypt_tool
	@mkdir -p src/resources
	@./tools/encrypt_tool apps/bin/stat.elf apps/bin/stat_encrypted.elf $(ESDUMAN_ELF_KEY_HEX)
	@xxd -i apps/bin/stat_encrypted.elf | \
	sed 's/apps_bin_stat_encrypted_elf/stat_elf/g' > src/resources/stat_elf_data.c


test:
	@echo "--- Running Host Unit Tests ---"
	
	@gcc -Wall -Wextra -iquote ./include -DARCH_X86 tests/host/c/test_crypto.c crypto/aes.c -o tests/host/test_crypto
	@./tests/host/test_crypto
	
	@gcc -Wall -Wextra -iquote ./include -DARCH_X86 tests/host/c/test_hash.c -o tests/host/test_hash
	@./tests/host/test_hash

	@gcc -Wall -Wextra -iquote ./include -DARCH_X86 tests/host/c/test_elf_validation.c kernel/proc/elf_validate.c -o tests/host/test_elf_validation
	@./tests/host/test_elf_validation

	@mkdir -p tests/host/bin
	@gcc tests/host/c/test_elf_sast.c -o tests/host/bin/test_elf_sast
	@./tests/host/bin/test_elf_sast

	@python3 -m unittest discover -s tests/host/python -p "test_*.py"

# Built into build/test, which is what makes this incremental.
#
# These objects carry -DPBKDF2_DEV_ITERATIONS and the production ones do not, and
# make cannot tell them apart by flags - it only compares timestamps. They used to
# share one tree, so every test run began by deleting every object and paying for
# a full rebuild, and a production image built without `make clean` first would
# quietly inherit the reduced iteration count. Separate trees end both.
test_kernel: hello.elf
	@$(MAKE) BUILD=test EXTRA_CFLAGS='-DPBKDF2_DEV_ITERATIONS=$(PBKDF2_TEST_ITERATIONS)' $(TEST_BIN)
	@echo "--- Running Kernel QEMU Self-Tests (PBKDF2=$(PBKDF2_TEST_ITERATIONS) iterations, CPU='$(QEMU_TEST_CPU)') ---"
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@echo "Merhaba Hard Disk! Ben esdumanOS!" > message.txt
	@echo "Bu bir esdumanOS gizli metin belgesidir!" > gizli.txt
	@dd if=message.txt of=disk.img bs=512 seek=2048 conv=notrunc > /dev/null 2>&1
	@if timeout --foreground $(QEMU_TEST_TIMEOUT) $(QEMU) -kernel $(TEST_BIN) $(QEMU_TEST_CPU) -append "kernel_pass=$(KERNEL_PASS)" \
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
	@$(MAKE) BUILD=test EXTRA_CFLAGS='-DPBKDF2_DEV_ITERATIONS=$(PBKDF2_TEST_ITERATIONS)' $(TEST_BIN)
	@echo "[SMAP TEST] Running kernel self-tests with -cpu max (SMEP/SMAP enabled)..."
	@dd if=/dev/zero of=disk.img bs=512 count=4096 > /dev/null 2>&1
	@if timeout --foreground $(QEMU_TEST_TIMEOUT) $(QEMU) -kernel $(TEST_BIN) -cpu max -append "kernel_pass=$(KERNEL_PASS)" \
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

# Refuse to start rather than fail unreadably on a CPU without POPCNT.
#
# libFuzzer computes the hamming distance between compared values, which its
# runtime does with the POPCNT instruction. A CPU that does not implement it
# raises SIGILL inside __sanitizer_cov_trace_const_cmp8, before any code in this
# project runs - and libFuzzer's own handler reports that as "deadly signal"
# without ever naming the signal, so the output points at the fuzz harness and
# not at the machine. Diagnosing it from that output takes hours.
#
# Reachable on an emulated x86 host: QEMU's default qemu64 CPU model has no
# POPCNT, so developing on Apple Silicon means hitting this unless the VM is
# started with -cpu max. Real hardware and the CI runners are unaffected. See
# the note in README.md under Requirements.
fuzz:
	@echo "--- Starting Fuzzing (libFuzzer) ---"
	@grep -q '\bpopcnt\b' /proc/cpuinfo || { \
		echo "ERROR: this CPU does not implement POPCNT, which libFuzzer's runtime requires."; \
		echo "       It would abort with SIGILL before reaching any of this project's code."; \
		echo "       On an emulated host, start the VM with '-cpu max' (QEMU's default"; \
		echo "       qemu64 model omits POPCNT). See README.md, Requirements."; \
		exit 1; \
	}
	@mkdir -p tests/host/corpus
	@clang -g -O1 -fsanitize=fuzzer,address -iquote ./include -DARCH_X86 tests/host/c/fuzz_parser.c kernel/proc/elf_validate.c -o tests/host/fuzz_parser
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
	# build/ holds every flavour's objects, so one rm covers prod, test and dev -
	# including lib/libc.a, which now lives inside each of them.
	rm -rf build
	# Objects from before the per-flavour split, when they were written next to
	# their sources. A tree built at v0.5.0 or earlier still has 180-odd of them
	# and the rm above does not reach any. Derived from the source lists rather
	# than found with a wildcard: the old path was exactly <source>.o, so this
	# names what it deletes instead of sweeping for anything that looks like it.
	rm -f $(CORE_SRCS:.c=.o) $(CORE_SRCS:.c=.d)
	rm -f $(TEST_SRCS:.c=.o) $(TEST_SRCS:.c=.d)
	rm -f $(ARCH_C_SRCS:.c=.o) $(ARCH_C_SRCS:.c=.d)
	rm -f $(ARCH_ASM_SRCS:.asm=.o)
	rm -f lib/*.o lib/*.d lib/libc.a
	# Left by the test targets: QEMU's instruction log, the host SAST binary, and
	# the bytecode the Python test writes beside its sources.
	rm -f qemu.log
	rm -rf tests/host/bin tests/host/python/__pycache__ tools/__pycache__
	# Not removed on purpose: .vscode/ and compile_commands.json are editor state,
	# not build output. Deleting the compilation database would silently break
	# code navigation until someone thought to run `bear -- make` again.
	rm -f apps/bin/hello.o apps/bin/hello.elf hello.elf apps/init.o apps/init.elf apps/init_encrypted.elf tools/encrypt_tool apps/hello_encrypted.elf
	rm -f apps/bin/*.elf apps/bin/*_encrypted.elf
	rm -f tests/user/*.elf
	rm -rf src/resources/*_data.c src/resources/*.o
	rm -f disk.img kernel_log.txt
	rm -f tests/host/test_runner tests/host/test_crypto tests/host/test_hash tests/host/test_elf_validation tests/host/fuzz_parser
	rm -f $(BIN) $(TEST_BIN)
	rm -rf isodir message.txt gizli.txt
	# Wildcard, not just $(ISO): a version bump renames the ISO, and the image
	# built under the previous version would otherwise never be cleaned up.
	rm -f esdumanOS-v*.iso myos.iso

# =============================================================================
# Developer Mode Targets
# =============================================================================

# Fast development build + run. Reduces PBKDF2 to 1000 iterations for instant
# password setup. Preserves disk.img across runs to skip first-boot setup.
# Usage: make run-dev
# Builds into build/dev. The reduced iteration count used to be applied by adding
# to CFLAGS for this target, which put dev-cost objects into the tree the release
# image is linked from - the same hazard the test targets had.
run-dev: apps/init.elf tools/encrypt_tool hello.elf apps/bin/clear.elf apps/bin/echo.elf apps/bin/sh.elf
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
	@$(MAKE) BUILD=dev EXTRA_CFLAGS='-DPBKDF2_DEV_ITERATIONS=1000' $(ISO)
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
