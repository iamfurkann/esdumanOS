# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.9.0-beta.1] - 2026-08-30

v1.8.0 brought the USB bus up and proved it with a command that moved nothing. This talks
to what is on it: every connected port is reset, its device is given a slot and an
address, and it is asked to describe itself. `lsusb` stops being a list of ports and
becomes a list of devices.

### Added

- **Port reset, and with it a speed that means something.** PORTSC's speed field is
  undefined until a port has been reset, which is why v1.8.0's `lsusb` could print
  "unknown speed" for a device that was plainly attached. It is also what a device needs
  before it will answer to a slot. The reset carries a budget of its own, shorter than the
  driver's general one: enumeration walks every connected port, and a dead one must not
  spend the whole budget before the next is tried.

- **Enable Slot, Address Device, and control transfers.** A real command ring enqueue
  replaces v1.8.0's single-slot write, with the wrap that follows the Link TRB — which
  cannot be reached at eight devices and three commands each, and is written anyway for
  the reason the Link TRB was installed in the first place. Control transfers are three
  stages on the device's own ring, and the third is where direction stops being obvious: a
  status stage runs opposite to the data stage it follows, so a read ends with an OUT,
  while a transfer with no data stage has no opposite and must be IN.

- **The device and configuration descriptors.** Eight bytes first, because
  `bMaxPacketSize0` cannot be read without a working endpoint zero and endpoint zero
  cannot be sized without it — and eight bytes is what fits in one packet at every size
  the field can hold. Then the whole device descriptor, then the configuration descriptor
  twice: nine bytes to learn `wTotalLength`, then that many, clamped to the buffer rather
  than believed.

- **`xhci_parse_config()`, separated from the transfer that feeds it**, for the same
  reason `keyboard_handle_scancode()` is separate from the port read above it: it can be
  driven with a buffer of one's choosing, and a controller cannot be asked to produce a
  malformed one. Every length it steps by came from the device, so a descriptor claiming
  zero length is refused rather than skipped — that is not a malformed field to step over,
  it is a walk that never ends. The same bound the extended capability chain got in
  v1.8.0, reached from the other side of the machine.

### Changed

- **The context stride is read from the controller, not from `sizeof()`.** A context entry
  is eight doublewords on every controller ever made; the distance between two of them is
  32 or 64 bytes depending on a bit in HCCPARAMS1. Those are the same number on QEMU's
  controller and different on a great many others, so a driver that indexed by the
  structure would be correct on every machine this project can run on and wrong on the
  machines it is aimed at. That is the exact shape of the defect `ata_identify()` carried
  from v0.1.0 to v1.4.0, and it is the reason every context access in the driver goes
  through one helper. The test module checks the placement at both strides, including the
  one no machine here uses.

- **A device context and an input context get a frame each**, and the arithmetic is why
  rather than taste: at a 64-byte stride they are 2048 and 2112 bytes, and 4160 is
  sixty-four more than a frame holds. `mm/pmm.c` is untouched for the third release
  running.

- **The roadmap gained a release.** Enumeration and HID were one row; `drivers/xhci.c` was
  944 lines after v1.8.0 with only the bus in it, and putting both into one release would
  have meant two subsystems at once in the file least able to afford it. The keyboard is
  v1.10.0.

### Found in passing

- **`drivers/keyboard.c` already has the seam the roadmap said v1.9.0 would have to
  build.** The file is 376 lines and exactly one of them touches hardware — `inb(0x60)`.
  `keyboard_handle_scancode()` was split out for testability long before anything needed
  two backends, and `tests/kernel/test_kbd.c` already drives it directly. So the USB
  keyboard will translate HID usage codes into set-1 scancodes and call it, and the
  Turkish layout, AltGr, the Ctrl fold, Ctrl-D and the escape sequences all keep working
  without being touched. This is the fifth consecutive release in which a roadmap row
  asked for more than its release needed.

### Known issues

- **Nothing on the bus can send or receive anything.** No interface is selected, no
  configuration is set, and no endpoint but the control one is opened.

- **Three paths cannot be exercised on any machine available to this project**, and all
  three say so where they are written: the firmware handoff, the scratchpad allocation,
  and now the Evaluate Context that re-opens endpoint zero when a full-speed device
  answers with a packet size other than 8. QEMU's keyboard and mouse answer 8.

- **The 64-byte context layout is arithmetic, not experience.** The driver reads the bit
  and the tests check both strides; no machine here has ever used the wider one.

- **Eight devices.** Each costs four frames. A ninth shows as a connected port with no
  device line under it, and the report says the list stopped.

- **Still a snapshot taken at boot**, with no hotplug, like the PCI inventory. Still no
  ACPI, no Secure Boot, and Multiboot 1 still cannot simply become Multiboot 2.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status
  v1.2.0 gave them. A device larger than about 1 GB is still used only up to that.

## [1.8.0-beta.1] - 2026-08-29

esdumanOS has a USB bus. The keyboard on the machine this project is aimed at is on it,
and so is the stick it is meant to boot from; neither is spoken to yet. What this release
delivers is the controller underneath both of them, brought up far enough to prove it is
working rather than far enough to assume it.

### Added

- **An xHCI driver.** It finds the controller, asks the firmware to release it, resets it,
  installs the device context array and the command and event rings, powers the ports and
  reads what is on them. One controller, one interrupter, one event ring segment, polled —
  every one of those argued in `include/xhci.h` rather than left as a gap.

- **A No Op command, issued once at bring-up, and it is the point of the release.** A
  controller that has been reset and started reports itself as running with its rings
  pointed anywhere at all; the doorbell, the two rings and the cycle-state agreement
  between driver and hardware are only proven by putting a TRB on one ring and reading its
  completion off the other. If it does not come back, the driver reports the controller as
  not running rather than as ready, and the suite goes red.

- **`lsusb`**, the twenty-first program in `/bin`, and `SYSCALL_USBINFO` at 70. Built the
  way `lspci` was and `free` was rebuilt in v0.9.2: the kernel renders the text and the
  program writes it through descriptor 1, so `lsusb > usb.txt` is a file with something in
  it. Root only, through the same check every other rendered diagnostic uses. A machine
  with no controller gets one line saying so and a successful return, because that is the
  answer to the question rather than a failure to answer it.

- **`pci_find_class_if()`**, which matches on the programming interface as well. UHCI,
  OHCI, EHCI and xHCI all report class `0x0C` subclass `0x03` and are four unrelated
  register files, so `pci_find_class()` would have found whichever the bus walk reached
  first and then quietly done nothing on a machine with more than one. `pci_find_class()`
  is now that search with the third field wildcarded rather than a second loop beside it.

- **`test_xhci`**, the forty-first module, and USB hardware on every kernel test target.
  Neither i440fx nor q35 provides a USB controller by default, so `test_kernel`,
  `test_kernel_q35`, `test_kernel_uefi` and `test_smap` are now all given an xHCI
  controller with a keyboard and a mouse on it, from one variable rather than four copies.
  The three that are compared still report the same total.

### Changed

- **The roadmap said this release needed multi-page contiguous physical allocation. It did
  not, and `mm/pmm.c` was not touched.** A ring segment is 4096 bytes and the only
  placement rule it has is that it must not cross a 64 KB boundary, which a 4 KB-aligned
  frame cannot do. The device context array, the segment table and the scratchpad array
  together use 3584 bytes of one more frame. The scratchpad buffers look like the case
  that needs contiguity and are not, because the array holds each address separately. The
  same sentence was written about AHCI in v1.5.0 and was wrong then too; the arithmetic is
  asserted in `tests/kernel/test_xhci.c` now rather than argued in a comment.

- **The event ring is not polled from the timer tick, which the roadmap also called for.**
  Nothing in this release has to be serviced while something else runs: the one command is
  issued on the boot path and waited for inline, with a deadline, the way every AHCI
  command is. A hook in `timer_interrupt_handler()` would have had no caller. It arrives
  in v1.9.0 with the interrupt endpoint that needs it.

- **`make run` and `make run_text` get a controller and a mouse, and deliberately no USB
  keyboard.** QEMU delivers key events to the most recently attached keyboard, so a
  `usb-kbd` on an interactive run would route every keystroke to a device this kernel
  cannot read yet — the PS/2 driver would go silent and the machine would look hung at the
  login prompt. The test targets have one because nothing types at them.

### Found in passing

- **The driver misnamed the one condition it was written to detect, and `make
  test_kernel_uefi` is what found it.** An xHCI controller's first BAR is 64-bit
  capable, so where firmware places it above 4 GB the whole address sits in the BAR
  after it and the low half carries nothing but the type bits. The driver checked
  "is the low half zero" first and reported a controller at a high address as a
  controller with no address at all. Under SeaBIOS the question never comes up, because
  it has no aperture above 4 GB to put anything in; under OVMF it does, and three
  assertions failed on the one target that boots the way a real machine boots. The high
  half is asked about first now, and the BAR's width is read rather than assumed — a
  32-bit BAR's neighbour is an unrelated region, and treating it as the top of an
  address would refuse a perfectly reachable controller on the strength of a number
  that means something else.

- **The driver's one success message did not reach `dmesg`.** `printk()` writes to the
  terminal and the serial port and deliberately not to the log ring, because a log is a
  record of events rather than a transcript of the screen. Every failure path in the
  driver was already recorded; the path that goes right was a `printk` and nothing else,
  so once the screen scrolled there was no evidence anywhere that the controller had come
  up - not in `dmesg`, not in `/var/log/kern.log`. That matters more here than it usually
  would, because "the controller came up" is the whole of what this release claims. It
  now records a line the way `pci_init()` has since v1.4.0. The AHCI driver has the same
  gap and keeps it for now; it is a different file and a different release.

- **`SECURITY.md` had no entry for bus-mastering DMA**, which became true in v1.5.0 when
  the AHCI driver started handing a controller physical addresses. There is no IOMMU here,
  so a bus-mastering device is not constrained by the page tables. It is recorded now,
  covering both drivers, rather than at the release that would have made it look new.

- **Three stale counts in the README**: the file tree said 44 headers and 62 syscall
  numbers, and the test-target block said 39 kernel-mode modules. All three were correct
  when written and none is anywhere near a section anybody re-reads.

### Known issues

- **The firmware handoff is written from the specification and never executes here.**
  QEMU's controller publishes no USB Legacy Support capability, so on every machine this
  project can run on the walk finds nothing and returns. On a UEFI machine the firmware
  has been driving the controller to read its own boot keyboard and does not stop until it
  is asked — which is exactly the machine this code exists for and exactly the machine
  nobody here has.

- **The scratchpad path does not execute here either.** QEMU's controller asks for no
  scratchpad buffers. Real controllers ask for a handful and one that asks for more than
  this driver allocates is refused with its number in the log rather than half-served.

- **No device is enumerated.** No slot is enabled, nothing is addressed, no descriptor is
  fetched, and `lsusb` therefore reports ports and speeds rather than vendors and
  products. A port's speed field is only defined after a port reset, which this release
  does not perform, so a device attached before one may render as "unknown speed".

- **The port list is a snapshot taken at boot**, with the same shape and the same
  limitation as the PCI inventory: there is no hotplug behind it.

- **A controller the firmware places above 4 GB is unreachable, and that is the kernel
  rather than the driver.** There is no PAE here, so a physical address is 32 bits wide;
  such a controller is not slow to reach or partially reachable, it cannot be addressed
  at all. The driver refuses it and names it in the log. `make test_kernel_uefi` passes
  OVMF's own `X-PciMmio64Mb=0` so that every BAR lands in the low aperture, which is the
  accommodation firmware makes for any 32-bit operating system. Relocating a BAR from
  inside the kernel is the real answer and is a release of its own: it needs BAR sizing,
  which is a write to configuration space this tree has never made, and a free-range
  search. It is only worth doing if a physical machine turns out to place this controller
  high, which a fixed-function xHCI in a chipset does not.

- **Still no ACPI**, and on a UEFI machine that remains the real limit it became in
  v1.7.0. Still no Secure Boot. Multiboot 1 still cannot simply become Multiboot 2.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status
  v1.2.0 gave them. A device larger than about 1 GB is still used only up to that.

## [1.7.0-beta.1] - 2026-08-29

esdumanOS boots on a machine whose firmware has no BIOS. That is the second of the three
things standing between it and the hardware it is aimed at, and it cost one line of
bootloader configuration — the roadmap had budgeted a migration to Multiboot 2 and none
of it was needed.

### Fixed

- **The ISO had no EFI boot path at all, and nothing said so.** `grub-mkrescue` builds a
  hybrid image only when the EFI platform files are installed; without
  `grub-efi-amd64-bin` it succeeds, prints nothing, and produces an image that boots on
  BIOS only. Every ISO this project published before this release was one of those, so
  anyone with a UEFI-only machine could not start it. It is a build dependency now, named
  in the README, in CONTRIBUTING and in the CI install step, and `make test_kernel_uefi`
  refuses to run without it rather than testing a BIOS image and reporting success.

- **`grub.cfg` loaded no video driver.** On a BIOS machine `grub-mkrescue` builds VBE
  support into the core image, so the kernel's Multiboot video request has been satisfied
  since the day it was added. On UEFI the driver is a separate module, and without it GRUB
  has no modes to offer: it refuses the kernel with `error: no suitable video mode found`,
  which reads like a complaint about the resolution asked for and is GRUB saying it has no
  video driver at all. `insmod all_video` is the whole of the fix.

- **`test_console` assumed which machine it was running on.** It ended by forcing text
  mode and asserting the backend was named "vga", with a comment explaining that this is
  what both test machines boot in. That was a fact about the two machines rather than
  about the console: under a bootloader that hands over a framebuffer it would take the
  screen away from every module after it while asserting, wrongly, that it had put things
  back — and the fake framebuffer it installs had already overwritten where the real one
  is. It now saves the console on entry and restores it on exit, which is the discipline
  `test_blockdev.c` has had with the root block device since v1.2.0.

### Added

- **`console_save()` and `console_restore()`**, the pair that makes the above possible.
  Restoring a framebuffer goes back through `console_use_framebuffer()` rather than
  putting the fields back, because the shadow and the screen have to agree: whatever ran
  in between drew over both, and a shadow that claims a cell is already correct is a cell
  that never gets redrawn.

- **`make test_kernel_uefi`**, and a sixth CI step. Every other target passes `-kernel`,
  which hands the binary to QEMU's own loader and skips the bootloader entirely — so
  until now nothing tested the part a real machine actually uses. This one builds an ISO
  of its own, because on this path the kernel command line comes from `grub.cfg` rather
  than from `-append`, boots it under OVMF, and runs the whole suite with a framebuffer
  console. It reports the same assertion total as the other two.

### Changed

- **No kernel source changed in this release.** The boot worked as soon as GRUB could set
  a mode: the framebuffer console v1.6.0 added took the screen, the bus walk v1.4.0 added
  found the controller, and the SATA driver v1.5.0 added took the disk, because a q35
  machine has no IDE controller to fall back to. Three releases meeting for the first time
  on the platform they were written for.

### Known issues

- **No ACPI, and on a UEFI machine that stops being a tidy limitation.** Shutdown and
  reboot go through the legacy keyboard controller. QEMU's q35 provides one under UEFI
  firmware so both work there; a physical machine of this class may have no i8042 at all,
  and there is no second way to power it down.

- **No Secure Boot.** The EFI image is unsigned and a machine with Secure Boot enabled
  refuses it.

- **Multiboot 1 cannot simply become Multiboot 2.** MB1 carries what is needed so far —
  the memory map and the framebuffer. MB2 would add the EFI system table and the ACPI
  pointer, and it would have to be added *alongside*: every test target except the UEFI
  one boots with QEMU's `-kernel`, which reads an MB1 header, so the binary would have to
  carry both.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status
  v1.2.0 gave them. A device larger than about 1 GB is still used only up to that.

## [1.6.0-beta.1] - 2026-08-29

The terminal stops knowing where the screen is. It has written characters into text-mode
video memory since the first release, which is a thing no machine built in the last
fifteen years has — and the first of the three reasons esdumanOS cannot boot on the
hardware it is aimed at.

### Added

- **A console backend behind the terminal** (`include/console.h`, `drivers/console.c`).
  `drivers/tty.c` is eight hundred lines of scrollback, escape sequences, three virtual
  terminals and cursor tracking, and exactly six of them stored a word at `0xB8000` or
  programmed a CRT controller register. Those six were the whole of what tied it to a
  machine with a text mode. The seam is two calls — put a cell here, move the cursor
  there — and the entry format is the VGA word `tty.c` already stored, because inventing
  a better one would have been a change to eight hundred lines instead of six.

- **A framebuffer backend.** Each cell is drawn as an 8x16 glyph into the linear pixel
  buffer the bootloader hands over, with the sixteen text-mode colours carried across as
  pixels, a software cursor, and a shadow of what was last drawn. The glyphs are scaled
  by the largest whole number that still leaves all eighty by twenty-five cells on the
  display and the result is centred — 640x400 in the middle of a 1024x768 screen, twice
  that on the 1920x1080 a real panel reports. The shadow is not an
  optimisation for its own sake: `update_screen()` hands over all nineteen hundred cells
  on every repaint, which is four kilobytes of stores in text mode and a quarter of a
  megabyte of pixels here, and typing one character causes one.

- **A font, written here** (`drivers/console_font.c`). Ninety-five glyphs, ASCII 32 to
  126, in binary rather than hex so that a person can see the letter in the source —
  which is the only review a font in a kernel can get. It is written rather than lifted
  because the bitmap fonts hobby kernels embed are dumps of the IBM VGA ROM, widely
  treated as uncopyrightable and with no licence saying so; this repository is MIT
  throughout. Nothing outside that range can be typed anyway: the Turkish keyboard layout
  maps every one of its keys to plain ASCII.

- **Multiboot 1's video request** (`arch/x86/boot/boot.asm`). Four fields in the header
  ask for 1024x768x32, and the bootloader reports where it put the buffer. Multiboot2 is
  the documented path for UEFI and it went to v1.7.0 with the rest of the boot work;
  Multiboot 1 has a video request of its own and it is what this release needed.

- **`tests/kernel/test_console.c`**, the 40th module, and the framebuffer backend is
  tested against an array in RAM. That is possible because `console_use_framebuffer()`
  takes an already-mapped buffer rather than a physical address — the thing that knows
  how to draw a glyph does not also have to know how a framebuffer is found and mapped.
  Neither test machine has a screen and every assertion reaches the same verdict on both.

- **`make run_text`** and a second GRUB entry that sets `gfxpayload=text`, which is the
  only thing outside the test suite that ever walks the text-mode fallback.

### Fixed

- **`clear` would have frozen the screen on a framebuffer boot.** Backend selection was
  put in `terminal_initialize()`, which is the obvious place and the wrong one:
  `SYSCALL_CLEAR_SCREEN` calls that function, so the backend would have been chosen again
  every time a program ran `clear` — dropping the console back into text mode mid-session
  with every write after it going somewhere the display does not read. Which backend is
  right is a fact about how the machine booted, so the boot path is the only thing that
  gets to say. Found by reading the callers rather than by running anything; both test
  machines boot in text mode, where the bug has no effect.

- **The GRUB menu waited for a keypress for ever.** There was no `timeout` in
  `grub/grub.cfg` at all, which was invisible while there was one entry and somebody was
  always watching. It stops anything from booting this image unattended, which is how it
  was found: an automated screendump captured a picture of the menu.

### Changed

- **`make run` serves the display over VNC.** curses renders a text-mode screen as
  terminal characters and answers a graphics mode by printing its dimensions and nothing
  else, which is exactly what it now does. The kernel is drawing correctly and the front
  end cannot show it. Copying the ISO and a disk image to a machine with a screen works
  too, and `make run_text` still puts the whole thing in a terminal.

### Known issues

- **The console is 80x25 whatever the screen is.** The backend draws those cells at the
  largest whole-number scale the display leaves room for and centres the result — one at
  1024x768, two at the 1920x1080 a real panel is likely to report. Growing the terminal
  itself means changing the size of three scrollback buffers in `tty.c`, which is a
  decision about the terminal rather than the screen.

- **32 bits per pixel only**, and the font covers ASCII 32 to 126. The framebuffer is
  mapped uncached rather than write-combining; setting up the page attribute table is
  separate work and nothing has measured a reason for it.

- **Output before the framebuffer is mapped goes only to the serial port.** The mapping
  needs paging. Nothing is lost — the terminal's cell buffers fill the whole time and the
  first repaint after the mapping shows all of it at once — but a machine that fails to
  map its framebuffer boots to a blank screen with only the log to say why.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status
  v1.2.0 gave them. A device larger than about 1 GB is still used only up to that.

## [1.5.0-beta.1] - 2026-08-29

The disk on a machine built this century. v1.4.0 taught the kernel to ask the bus what
was attached and, on a modern board, the answer was a storage controller it could name
and could not drive. This is the driver that answers it — and it is the release the
three before it had been building towards without either of them saying so.

### Added

- **An AHCI driver** (`drivers/ahci.c`, `include/ahci.h`). It registers as a `blockdev_t`
  exactly as the IDE driver does, so nothing under `fs/` learns that a second kind of
  disk exists. That is v1.2.0's block layer being used for the thing its own header said
  it was for; v1.3.0's device-sized geometry then works on whatever the new driver
  reports.

  Deliberately the smallest driver that reads and writes a disk: one controller, one
  port, one command slot, one sector per command, and polled rather than
  interrupt-driven. The interrupt would travel the PCI interrupt line to an IOAPIC, and
  this kernel has no IOAPIC, no ACPI to describe the routing, and an interrupt dispatcher
  that is a hand-written chain of comparisons.

- **A device memory window** (`vmm_map_device()`, `PAGE_KERNEL_MMIO`). Controller
  registers and the buffers a controller reads on its own are mapped uncached into 16 MB
  of previously unclaimed kernel address space. `map_page()` needed no change to honour
  it: it already passed flags through as `(flags & 0xFFF)`, and both cache bits are
  inside that mask.

  There is no contiguous multi-frame allocator, and the arithmetic is why. A command list
  is 1 KB, a received FIS area 256 bytes, a command table with one entry 144, and a
  sector 512 — 2560 bytes with every alignment satisfied inside one 4096-byte frame, and
  `pmm_alloc_frame()` already returns one of those. The planning for this release said
  the allocator would be needed; the arithmetic said otherwise, and writing it anyway
  would have been a structure with no caller.

- **PCI configuration writes** and `pci_enable_device()`. Memory decoding and bus
  mastering are set rather than assigned, so the bits the firmware configured and this
  kernel never looked at survive.

- **`make test_kernel_q35`** and a sixth CI step: the whole suite again on a machine with
  no IDE controller. One flag changes. Everything that touches storage — `test_vfs`,
  `test_bcache`, `test_blockdev` and the file system under all of them — exercises the
  new driver without an assertion being written for it, and both runs report the same
  total.

- **`tests/kernel/test_ahci.c`**, the 39th module. Machine-independent by construction:
  the structure sizes the controller indexes memory by, the mapping's offset and its
  cache-disable bit read back out of the page table, `pci_enable_device()` leaving the
  rest of the command register alone, and the fact that exactly one of the two storage
  drivers owns the disk — the one the bus says this machine has.

### Fixed

- **A regression test had been asserting the wrong thing for four years, and passing.**
  `REG-05` is named "ATA Identify protected with Timeout against hardware lockup" and its
  own comment says the proof is reaching the line at all. The condition underneath then
  asked how large the disk was, which is a fact about the test machine.

  It passed throughout the period when the lockup it names was real. The timeout it was
  written for guarded the `DRQ` wait; the wait for `BSY` sat directly beside it with no
  timeout, and v1.4.0 fixed that. i440fx always has an IDE controller, so the loop under
  test was never entered and the assertion never had an opinion about it. Running the
  suite on q35 asked the question for the first time and the assertion fell over.

  It now asserts what the comment always claimed: the call returns, and the answer is
  coherent for the machine — a disk where there is a controller, nothing where there is
  not.

- **`pmm_alloc_frame()`'s documented failure value was wrong** from the first release
  until now. The header promised 0 and the code returns `0xFFFFFFFF`, and zero is an
  address the allocator deliberately never hands out — so a caller that believed the
  header would have read every failure as a valid low frame. All twenty-one callers test
  against `0xFFFFFFFF` and none was ever misled; the trap was in the header alone, which
  is exactly where `ata.h`'s return contract sat for four years before v1.2.0 read it.

- **A comment in the CI workflow** said "the 23 kernel-mode modules". There are 39.

### Changed

- **The boot path tries IDE first and AHCI only if IDE found nothing.** The order is what
  makes this additive rather than a gamble: on the machine every test in this project
  runs on, `ata_identify()` answers and the new driver is never reached. Preferring AHCI
  where both exist would be the v1.2.0 mistake again — two defensible decisions with an
  unwatched link between them, where a wrong answer from the newer subsystem silently
  removes the disk.

- **The roadmap is a route now.** 2.0.0 is the release that boots on a real machine from
  a USB stick, and the four releases before it each remove one of the three things
  stopping that: the boot contract, the screen and the keyboard. Decided with it: **2.0.0
  ships as a beta as well.** Running on hardware is a claim about reach rather than about
  the system being finished, and this release found a four-year-old test asserting the
  wrong thing while the bug it named was real.

### Known issues

- **One controller, its first port, one command slot, one sector per command.** If a
  machine has two SATA controllers and the first has no disk on it, the driver gives up
  rather than looking at the second. Multi-sector transfers are the hardware's to give
  and `blockdev_t`'s to ask for; it reads and writes a sector at a time, and changing
  that is a decision about every driver.

- **Device mappings are never reclaimed.** The window is a bump pointer with no free,
  because a driver here is loaded at boot and never unloaded.

- **No HBA reset.** The controller is taken as firmware left it with AHCI mode asserted,
  which is what the specification requires before any other register is read. A full
  reset is the more thorough start and there is no machine available to this project on
  which the difference could be observed.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status
  v1.2.0 gave them. A device larger than about 1 GB is still used only up to that.

## [1.4.0-beta.1] - 2026-08-28

The kernel can ask the machine what it has. Every piece of hardware it uses, it has so
far assumed: the disk at 0x1F0, the keyboard at 0x60, the screen at 0xB8000. Each of
those is true of the machine this has always been tested on and false of the machine it
is aiming at, and there was no way to find out which one had booted.

### Added

- **PCI bus enumeration** (`drivers/pci.c`, `include/pci.h`). Configuration space through
  the ports at 0xCF8 and 0xCFC, not the memory-mapped window a modern chipset also
  offers — the ports work on every PC that has ever had PCI and need nothing this kernel
  does not already have, where ECAM would need device memory mapped. Buses behind a
  bridge are walked from a worklist rather than by recursion, with a visited set: the
  kernel stack is 8 KB with interrupts landing on it, and a bridge that names itself as
  its own secondary bus is then a bounded mistake instead of an unbounded one. Up to 32
  functions are recorded (`PCI_MAX_DEVICES`), and a machine with more gets a log line
  saying the list stops there.

- **`inl` and `outl`** in `include/io.h`. The last thing missing from that header, and
  the reason the roadmap had listed PCI as the prerequisite of a prerequisite for three
  releases: configuration space is addressed and read as doublewords, and nothing here
  could write one.

- **`SYSCALL_PCIINFO` (69) and `/bin/lspci`**, the twentieth program. Built the way
  `free` was rebuilt in v0.9.2 — the kernel renders the text and the program writes it to
  descriptor 1 — so `lspci > devices.txt` produces a file with something in it. Root
  only, through the same `diag_ready()` check `MEMINFO`, `HEXDUMP` and `STACK_DUMP` use.
  The second number assigned since the freeze, continuing from `SETKEY` at 68 rather than
  filling one of the holes, which is what makes 68 a rule rather than a one-off.

- **`tests/kernel/test_pci.c`**, the 38th module. There is no fake device: configuration
  space is two I/O ports and nothing can stand between the driver and them, so the
  assertions are about the enumeration rather than the hardware. That the stored vendor
  and class match a fresh read of the same registers is the one that matters most — an
  offset wrong by two bytes fills the table with plausible numbers.

### Fixed

- **`ata_identify()` could hang forever, and on the target machine it would.** The wait
  for the busy bit after `IDENTIFY` was written out inline with no timeout, ninety lines
  below an `ata_wait_bsy()` that has always had one and that every other wait in the
  driver goes through. An undecoded x86 port reads back 0xFF, BSY is bit 7, so on a bus
  with no IDE controller the loop never ended: the kernel did not report a missing disk,
  it stopped — before `init_fs()`, before anything reached the screen. QEMU's i440fx
  always carries a PIIX3 IDE controller, which is why no release ever produced the
  condition: the loop has been there since v0.1.0-alpha, so every release this project has
  made carried it and not one of them could have found it. `-M q35` produces it
  immediately, and README now documents that invocation. An all-ones status is also recognised by name now, so the answer is
  immediate rather than a timeout.

  Same shape as the v1.2.0 defect: the helper existed, and the code beside it did not
  call it.

- **`ATA_TIMEOUT_MS` is `ATA_TIMEOUT_TICKS`.** It was never milliseconds. Every use
  compares it against a difference of `timer_get_ticks()` and `TIMER_HZ` is 100, so the
  budget has always been two seconds. Renamed rather than rescaled — the behaviour is the
  one every release was tested with; the name was the wrong half.

- **A test that had been reading past the end of its own buffer.** `test_vfs`'s "an id
  past 255 survives the trip to the sector and back" read one sector and copied 96 bytes
  from an offset within it. An entry is 96 bytes and a sector is 512, which do not divide,
  so an entry beginning later than byte 416 finishes in the next sector — two slots in
  every sixteen. For those two the copy ran off the end of a 512-byte stack buffer and
  asserted on whatever followed it. It passed because the first free slot had never landed
  on one of those residues; adding a twentieth program to `/bin` moved it onto one.

  The straddle is correct and expected — `save_directory_to_disk()` writes the table as a
  flat byte stream, so an entry across a boundary is the disk's ordinary shape. The test
  now reads it in the two pieces the disk stores it in, which also means the assertion
  covers the straddling case instead of having avoided it for eleven releases.

- **Three stale documentation claims.** The roadmap said `mount` and `umount` could take
  68 and 69, written before `SETKEY` took 68 and left standing for three releases four
  lines below a paragraph saying `SETKEY` took 68; they take 70 and 71. The `test_abi.c`
  row claimed 62 syscall numbers and "that 68 is still free". The shell was listed with
  33 builtins where `sh.c` itself counts 34.

### Changed

- **The boot path enumerates the bus before probing the disk**, and says what it found
  about storage next to where the probe happens: an IDE controller by name, or a storage
  controller this kernel has no driver for, or nothing. On the target machine that line
  is the difference between "no disk" and "the disk is behind a controller this kernel
  cannot drive".

  Nothing is gated on it. Making the ATA probe conditional on the enumeration's verdict
  would be the v1.2.0 mistake again — two defensible decisions with an unwatched link
  between them, where a wrong answer from the new subsystem silently removes the disk.
  What stops the probe hanging is its own timeout.

### Known issues

- **Nothing on the bus is driven.** The base address registers are read and reported and
  none is mapped, so a machine whose disk is behind an AHCI controller is now a machine
  that says so and still cannot read the disk. Mapping them needs device memory mapped
  and, for anything that transfers, DMA — neither exists here.

- **The enumeration is a snapshot taken once, on the boot path.** Nothing rescans and
  there is no hotplug.

- **No BAR sizing.** Finding out how large a region is means writing all ones to the
  register and reading back, and that write moves where the device decodes for as long as
  it takes. Nothing here has a reason to map a region yet, so nothing here has a reason
  to take that risk.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status v1.2.0
  gave them. A device larger than about 1 GB is still used only up to that.

## [1.3.0-beta.1] - 2026-08-28

The file system is sized from the device it is on. Both of its tables were static arrays,
so a 16 GB stick and a 2 MB image were laid out identically.

### Changed

- **The directory table and the allocation table are allocated at mount**, from what the
  superblock records, and the format chooses that from the size of the partition — up to
  8192 entries and a little under 1 GB of data. They were `disk_file_entry_t[512]` and
  `uint32_t[4096]`, which is why a large disk was capped at its first 16 MB and held 512
  files whatever it was. `inode_locks` moved with them: it must be exactly as long as the
  directory table, and nothing outside `vfs.c` names it.

  This is an improvement rather than a fix, and the release note says so. A large disk
  mounted and worked before, capped. What was wrong is that raising the cap alone would
  have bought nothing: 512 entries at 64 KB each is 32 MB of content, so the disk was
  already within twice what the file system could hold. That is why both moved together.

- **No format change.** The superblock has recorded `max_entries`, `dir_sectors`,
  `fat_start`, `data_start` and `total_clusters` since v0.10.0, and
  `fs_cluster_to_sector()` has read those rather than the constants. Turning the
  constants into ceilings was enough. A disk written by v1.1.0 or v1.2.0 mounts unchanged
  with its own 512 entries, and there is no converter because none is needed.

- **`MAX_FILES_IN_DIR` and `FS_MAX_CLUSTERS` were deleted rather than redefined.** They
  bounded forty-three loops between them, and a loop bounded by a ceiling over a table
  allocated to something smaller compiles perfectly and reads past the end. Deleting the
  names is what made the compiler point at all forty-three: two of them meant a ceiling
  and became `FS_MAX_ENTRIES_CAP` / `FS_MAX_CLUSTERS_CAP`, the rest meant a count and
  became `fs_max_entries` / `fs_total_clusters`. The same mistake in the other direction —
  a widening that compiled everywhere and was wrong in eight places — cost v0.9.0 three
  releases.

- **`fs_max_entries` is signed**, because an entry index is an `int` everywhere in this
  tree: `fs_get_entry_idx()` returns one and uses -1 for "no such entry", and every
  `inode_idx` is an `int`. A `uint32_t` would have put a signed comparison in twenty-seven
  loops and ten bounds checks, and the warning would have been right. The count is typed
  the way the indices are typed instead of being cast at every use.

### Fixed

- **`format_disk()` reports failure.** It could not before, and now it can fail: the
  tables are allocated inside it. A caller that did not check would have gone on to read
  the directory table through a null pointer.

- **Three `ft_memset(dir_table, 0, sizeof(dir_table))` calls** would have cleared four
  bytes once the table became a pointer. Two are now `fs_tables_free()`, which is what the
  call sites actually wanted, and the third is done by the allocator.

- **The cluster ceiling was applied after the geometry was computed from it**, so a count
  above the cap would have sized the allocation table for more clusters than the
  superblock then recorded. The caller already clamped, but an invariant that holds
  because of what the caller happens to do is not an invariant.

### Known issues

- **A device larger than about 1 GB is used up to that and no further.** The ceiling is a
  1 MB budget for the allocation table at 4 KB clusters; going beyond it means variable
  cluster sizes, which `fs_cluster_to_sector()` already supports and `load_superblock()`
  does not yet accept.

- **A file is still capped at 64 KB** (`MAX_FILE_WRITE_BYTES`), untouched here.

- `test_time`'s clock round trip is still not deterministic. There is still no
  `mount`/`umount`. The runtime read and write paths still do not check the status v1.2.0
  gave them.

## [1.2.0-beta.1] - 2026-08-28

A disk that cannot be read is no longer mistaken for a disk with nothing on it. It was,
and the mount path formatted it.

### Fixed

- **A failed read was indistinguishable from a sector full of zeros, and the mount path
  formatted the disk on the strength of it.** Every failure path in the ATA driver zeroes
  the caller's buffer — a drive that failed `IDENTIFY`, an LBA past the end of the disk, a
  BSY wait that timed out, an IRQ that never arrived, a hardware error bit — and returns a
  status. The block cache discarded that status at all three call sites and stored the
  zeros as valid data. `disk_region_is_blank()` read through the cache, saw zeros, said
  "blank", and `init_fs()` formatted. On real hardware that is data loss from a loose
  cable. QEMU's disk does not fail, which is why this survived to be found by reading
  rather than by breaking.

  The whole mount path checks now: `load_partition()`, the superblock, the directory
  table, the allocation table, and the blank check itself, which has three answers instead
  of two. A disk that cannot be read is refused, and refused before anything is written to
  it — the test counts writes at the device to say so.

- **`include/ata.h` documented the opposite of what the code did.** It promised "0 on
  success, or a negative error code on failure" while `ata_read_sector()` and
  `ata_write_sector()` returned 1 for success and 0 for failure, and never a negative
  anything. The header and the code had disagreed about the meaning of zero since v0.4, in
  the direction that turns every failure into a success, and nothing caught it because no
  caller read the value at all. Both now return `E_OK` or a negative errno, which is what
  the rest of the kernel means.

- **A failed eviction lost the sector twice.** `bcache_get_lru_slot()` wrote the victim
  out, marked it clean whatever happened, and handed the slot to a different sector — so
  the data never reached the disk and the dirty flag that would have made a later flush
  retry it was gone. A failed eviction now refuses the slot. `bcache_flush()` had the same
  shape: a sector the device rejected was marked clean and skipped by every flush after.

- **`write_mbr()` could fail and the format continued.** The superblock is found through
  the partition table, so a table that never reached the disk makes everything written
  afterwards unreachable. The format is abandoned instead.

### Added

- **A block device layer.** One registered device under the block cache, with the sector
  bounds check in one place rather than in each driver — it lived inside the ATA driver,
  which is exactly the check a second driver forgets. ATA registers itself at boot and
  `ata_identify()` moved out of `init_fs()` into the boot path, so no file under `fs/`
  names a disk driver any more.

  It is also what made the fix testable. QEMU's disk does not fail, so until there was a
  seam to put a failing device into, "a failed read is not an empty disk" was an assertion
  with no way to be written.

- **`tests/kernel/test_blockdev.c`**, against a device made up for the occasion: routing,
  the bounds check refusing without the driver being called, a driver's errno arriving
  unchanged rather than flattened, a read-only device answering `E_ROFS` instead of calling
  a null handler, and `E_NODEV` when nothing is registered. `test_bcache.c` gains the
  failure half — a failed read is not cached, so the next one asks the device again — and
  `test_vfs.c` gains the assertion this release is named for.

### Changed

- **`bcache_read_sector()`, `bcache_write_sector()`, `bcache_flush()`, `fs_read_sector()`
  and `fs_write_sector()` return a status.** They returned void, so the file system had no
  way to learn that the disk had failed even if it had wanted to.

- Version is `1.2.0-beta.1`, and the release has no name — the convention narrowed to
  majors on 2026-08-28.

### Known issues

- **The runtime read and write paths still do not check.** `fs_read()` and the encrypted
  file paths take a failed read as the data it would have been. That is a wrong answer,
  and it is not the same class as the mount path, which formats: nothing there does
  something irreversible on the strength of zeros. Converting them is a larger change than
  this release wanted to carry alongside the layer.

- **`test_time`'s clock round trip is still not deterministic**, and still untouched.

- **There is still no `mount`/`umount`**, and the disk is still capped at 16 MB by a static
  allocation table. Sizing it from the device is what the block layer was the prerequisite
  for.

## [1.1.0-beta.1] - 2026-08-28

The disk is confidential at rest now. Every release before this one encrypted the file
system under a key compiled into the kernel image, which is tamper resistance and not
confidentiality — the boot log said so itself. The key comes from a passphrase entered
at boot instead, and the release is named for what that buys: a machine you can carry.

### Added

- **A passphrase-derived file system key, in two levels.** PBKDF2-HMAC-SHA256 turns the
  passphrase into a key-encryption key; that unwraps a random data key held in a key slot
  in the superblock; the data key encrypts the file system. Two levels rather than one
  because a single level would make changing a passphrase mean re-encrypting every file
  on the disk, and a passphrase change that can fail halfway through is a disk that
  nothing opens.

- **The key slot**, 116 bytes: a 32-byte salt, the iteration count, the IV, the wrapped
  key and an HMAC-SHA256 tag over all of them. The tag covers the salt and IV as well as
  the ciphertext, which is what stops an attacker keeping the wrapped key and
  substituting a salt they chose. A wrong passphrase produces a wrong key-encryption key,
  the tag does not verify, and the disk is refused — three attempts, then the machine
  stops with no way past it.

- **`SYSCALL_SETKEY` (68) and the `setkey` builtin**, root only. Changes the passphrase by
  re-wrapping the same data key under a fresh salt, so the files are never touched and a
  wrong old passphrase returns `E_ACCES` having changed nothing. It is 68 because the
  freeze says new numbers continue from the highest assigned one rather than filling the
  holes at 11, 28, 30, 31, 32 and 99 — the first number handed out since v1.0.0, and the
  rule working rather than an exception to it. The next call takes 69.

- **`tests/kernel/test_keyslot.c`**, against the slot alone: round trip, a wrong
  passphrase refused with the caller's buffer left untouched, every field of the slot
  edited in turn, iteration counts above and below what this build accepts, and the same
  data key wrapped under a second passphrase to prove a passphrase change preserves it.
  The delivery half writes deadlines directly rather than waiting on anything, so nothing
  in it depends on how long the test takes.

### Fixed

- **The programs in `/bin` were stored on disk under the build-time key.** They are baked
  into the kernel image encrypted and were written out with `fs_create_file_raw()`,
  which stores a blob exactly as it arrives — so every system binary on every disk sat
  there readable to anyone holding the kernel image, whatever the file system key was.
  A passphrase would have protected the user's files and left the system's own untouched.
  They are decrypted once with the asset key and re-encrypted under the disk key now.

  This was missed twice before being caught. The audit for this release read the key's
  consumers and concluded no ELF was encrypted at all, which made the work look smaller
  than it was; the code said otherwise.

- **`fs_keyslot_rewrap()` wrote sector 0 with no `IMMUTABLE` check.** Every other write
  path in the VFS refuses at that level and this one did not, so the passphrase could be
  changed on a disk the system had been told was read-only. The guard sits with the other
  eight rather than in the syscall, because a rule enforced at the syscall layer is a rule
  the next in-kernel caller does not get.

- **`QEMU_TEST_TIMEOUT` was too low for the suite it runs.** It reported
  `KERNEL HUNG! No verdict within 300s` while the Ring 3 payload was still making normal
  progress. It is 600 now, and the message names the wrong thing when the cause is the
  budget rather than the kernel.

- **`make run` could not reboot into a formatted disk.** The kernel writes an MBR whose
  signature is `0xAA55`, because the file system uses that signature to recognise its own
  partition table at mount — but its 446-byte boot area is zero and nothing there was ever
  meant to run. The BIOS cannot tell those apart, so on the second boot of a formatted
  image it called the disk bootable, jumped into 446 bytes of zeros and stopped at
  "Booting from Hard Disk...". `-boot d` pins the boot to the CD-ROM. The trap has existed
  since v0.10.0 and went unnoticed because a blank disk carries no signature, so only a
  reboot with the disk still attached reaches it.

### Changed

- **The on-disk format is v3, and a v2 disk is refused by name.** There is deliberately no
  "the key slot is empty, so use the built-in key" path: that would be a downgrade an
  attacker could force by zeroing 116 bytes, which is a hole written into the format
  rather than found in it. Same treatment v0.10.0 gave v0.9.x disks, and for a better
  reason.

- **`init_elf_master_key()` is `init_image_asset_key()`**, and it fills `elf_asset_key`
  rather than `kernel_master_key`. The old name described what the constant was for and
  the old code used it for something else; the two are separate keys now, with the build
  constant reduced to what it always honestly claimed — tamper resistance for the
  embedded images.

- **`CONTRIBUTING`'s list of valid commit prefixes described a rule the project has never
  followed.** Three of the last forty commits matched it.

- **`SECURITY`'s supported-versions table said 0.9.x**, two minor lines after that stopped
  being true — and four lines below a sentence observing that the same table had said
  0.4.x for five releases.

- **README's syscall reference still called 30-32 "reserved for future crypto API"**, one
  release after v1.0.0 retired them.

### Known issues

- **`test_time`'s clock round trip is still not deterministic** and the cause is still not
  established. Not touched here.

- **A forgotten passphrase is a disk nobody can read.** There is one key slot, not the
  several LUKS offers, and no recovery path anywhere. That is the design rather than an
  oversight, and it is the cost of the guarantee.

- **The disk claims to be bootable and is not.** Its MBR carries a valid signature with no
  boot code, which is invisible inside QEMU now that `-boot d` pins the boot medium, and
  will not be invisible on real hardware or a USB stick.

- **There is still no `mount`/`umount`.**

## [1.0.0-beta.1] - 2026-08-27

The system call interface is frozen. No number already assigned changes its value or its
meaning, retired numbers are never reused, and new calls continue from 68 — which means
`mount` and `umount` are not blocked by this and never were. The reasoning that had
deferred the freeze behind them was wrong: freezing stops numbers from changing meaning
and says nothing about adding new ones. What the freeze did have to wait for was the
on-disk format and the shared structures, and those settled in v0.10.0 and v0.10.1.

### Added

- **`tests/kernel/test_abi.c`, which is what the freeze actually is.** All 62 syscall
  numbers asserted by literal value, the 34 error codes, the `exec`/`wait`/`lseek`/
  `klog_ctl` constants, the signal numbers, the security levels, and the retired numbers
  driven through the dispatcher to confirm they still answer `E_NOSYS`. A syscall number
  is written down in four places no compiler compares — the header, the freestanding
  programs under `apps/bin`, the literal strings the static analyser greps for, and the
  table in README — so only the kernel would have noticed one changing. A comment
  promising they would not change would have protected none of them.

- **A real `alarm(seconds)`, and `SIGALRM` (14) to go with it.** `ebx` is a count of
  seconds and 0 cancels; the return is the seconds left on any alarm it displaced, so a
  caller can put back what it had to move. The deadline lives in the caller's process
  control block, which is the only place it could live: the kernel timer slots are global
  and hold a bare `void(*)(void)`, so they carry no pid to signal. `SIGALRM` terminates by
  default, as POSIX has it — a bound whose default is to be ignored is not a bound.

### Changed

- **`YIELD` moved from 99 to 67.** It had sat outside the run every other call lives in
  since the first release, for no reason anybody recorded, which is the shape of a
  placeholder nobody revisited. Moving it is an ABI break and v1.0.0 was the last moment
  one was permitted. 99 is retired.

- **The idle task builds its syscall number from the constant.** Its Ring 3 loop is
  assembled byte by byte in `init_multitasking()`, and the number was a literal `0x63`
  with "mov eax, 99" in the comment beside it — the whole of what tied the scheduler's
  fallback task to the call it makes. This had to land before `YIELD` could move: a stale
  byte there does not fail a test, it stops the machine booting. The loop's length is
  taken from the array now too, instead of a hand-written 9.

- **`SYSCALL_INSN_LEN` is `SYSCALL_TRAP_INSN_LEN`.** It is not a syscall number, it sat
  in the `SYSCALL_*` namespace, and it held the value 2 — which is also `SYSCALL_IPC_SEND`.
  Anything counting the numbers in that header by their prefix counted it and got 65 calls
  where there are 62.

- **The idle task's pending-signal mask is cleared entirely rather than by name.** It
  listed six signals, which was correct only while those were all the signals with a
  default action; adding `SIGALRM` would have left its bit set forever on the one task
  that can never act on it, with nothing to fail. The question being answered is "this
  task cannot act on any signal".

- **Version is `1.0.0-beta.1`.** The `-alpha` suffix had been carried since v0.2.0 with
  the decision deferred, release after release, to 1.0. What 1.0 settles is the
  interface; it does not settle the system underneath it, and a bare `1.0.0` would have
  claimed the second by finishing the first.

### Removed

- **Numbers 30, 31 and 32, reserved for a crypto API that was never designed.** No
  interface, no caller, and no note saying what the three calls would have been. v0.10.1
  removed an AES-256 interface from `crypto.h` that had been declared and never
  implemented; this is the same reservation in another form. Retired rather than released
  back, because the freeze has one rule about holes and three numbers are not worth making
  it two.

- **`alarm_demo_callback()` and the timer slot registered for it at boot.** It printed a
  green line from inside the kernel three seconds after `SYSCALL_ALARM` armed it, which
  made it the last call in the table producing output from Ring 0 on a caller's behalf —
  the class v0.9.2 otherwise cleared out. The timer slots themselves stay, and
  `test_signal.c` exercises them through a slot of its own, so removing the demo costs no
  coverage.

### Fixed

- **README said there was no way to set the clock**, five releases after `SETTIME` (66)
  was added in v0.9.2, with the row for the call two screens above the sentence. Same
  class as the stale version badge v0.10.1 found: documentation organised by topic gets
  audited by section, and a claim in one section about another section's subject belongs
  to neither.

- **The signal documentation said four signals terminate by default and that no
  `SIGALRM` reaches user space.** Five do, and one does.

### Known issues

- **`test_time`'s clock round trip is still not deterministic**, and the cause is still
  not established. Unchanged from v0.10.1 and not addressed here; the freeze is an
  interface change and this is not part of the interface.

- **There is still no `mount`/`umount`.** The partition table holds four entries and the
  kernel reads one. This is the next feature, and the freeze does not stand in its way.

### Limits

The interface freezes with its limits, and one of them is worth stating plainly.
Timestamps are `uint32_t` seconds since the Unix epoch and run out in **2106**. The count
is stored in two frozen places at once — `esd_stat_t`'s `st_mtime` and `st_ctime`, which
cross the syscall boundary, and a directory entry's `mtime` and `ctime`, which are on the
disk — so widening it breaks both, against an entry that is exactly 96 bytes and has a
test saying so. It would also reopen a disk format v0.10.0 had just finished settling, to
push a problem past the lifetime of that format. `esd_time_t` is not the limit and is
sometimes mistaken for it: its `year` is a `uint16_t`.

## [0.10.1-alpha] - 2026-08-26

An audit of the whole tree before freezing the ABI at 1.0. No new features; what
follows is what a systematic read found, and the documentation came out of it worse
than the code did.

### Fixed

- **Seven of the eight `IMMUTABLE` guards asked `==` rather than `>=`.** The security
  levels are an ordered enum and IMMUTABLE is currently the last, so the two forms are
  equivalent today and stop being equivalent the moment a stricter level is added —
  every `==` guard would let the write through. A check that opens when somebody
  appends a line to an enum is not a check, and `SEC_LEVEL_*` is about to be frozen.

- **Six syscalls flattened the path resolver's errno into `E_NOENT`.**
  `split_from_cwd()` distinguishes E_INVAL (component too long), E_NOTDIR and E_NOENT;
  `open`, `create`, `rm`, `mv`, `mkdir` and the directory resolver collapsed all three,
  while `stat` propagated. The same path therefore produced different errnos depending
  on which call you asked, and creating a file with a 250-character name reported "no
  such file or directory" about a file the caller was trying to make.

- **`fs_atomic_update()` answered `E_ACCES` where every other write path answers
  `E_ROFS`** for the identical condition. Noticed in v0.9.3 and left because callers
  treated them alike; that does not survive a freeze.

- **`MAX_FD_PER_TASK` said 32 and the real limit was 16.** The constant was used
  nowhere — every bound goes through the task's own `fd_table_size` — so nothing ever
  compared the two. It says 16 now and `create_process()` uses it.

### Removed

- **An AES-256 interface that was never implemented.** `crypto.h` declared
  `aes256_ctx_t` and six functions, plus a RISC-V acceleration block written in terms
  of that fictional type. Nothing called any of it. The real AES is in `aes.h` and
  `crypto/aes.c`.

- **`update_passwd_file()`**, which rewrote `/etc/passwd` with no check of the caller's
  uid, the file's mode, or the security level. No caller, and no declaration in any
  header. Dead code that would have been a hole the day somebody revived it.

- **`print_two_digits()`**, which had no caller and no declaration.

### Changed

- **Four test assertions passed when their subject was missing.**
  `KTEST_ASSERT(idx < 0 || <property>)` reads as "if it exists it must be so" and
  behaves as "say nothing if it is gone" — `/etc/passwd` among them, with no existence
  check anywhere near it.

- **`test_stress`'s long-name check never reached the VFS.** It accepted any
  non-positive return, and what it was getting was `E_FAULT`: the content argument was
  the literal `""`, which lives in kernel `.rodata`, so `copy_user_string()` refused the
  pointer before a path was ever resolved. A test called "VFS Long Name (Buffer
  Overflow)" spent its life checking pointer validation on its second argument.

- **`esd_stat_t` and `esd_time_t` now have size assertions.** They cross the Ring 0 /
  Ring 3 boundary; `disk_file_entry_t` has had one for the disk, and the argument is
  the same except that there is no magic number here to catch a disagreement.

- **The static analysis of `/bin` covered seventeen of nineteen programs.** The two it
  missed were `stat.c` and `init.c` — and `init.c` is PID 1: if it stops calling `exec`
  there is no shell left to notice with.

- **`pmm.c`'s low-memory floor is named**, not a bare `256` in two places. The number is
  one megabyte over the page size, and this project has been bitten twice by a
  relationship written as a literal.

- **`SYSCALL_ALARM` is described as what it is.** It said "set an alarm signal" and
  keeps none of those promises: no duration, no signal, no relation to the caller — it
  arms a timer for a fixed three seconds and lets the kernel print a line. What becomes
  of number 18 is now part of freezing the ABI, alongside 11, 28 and the gap at 99.

- **The boot sequence diagram documented the ordering bug v0.10.0 shipped**, showing the
  mode pass before the block that writes the `/bin` tools. The code does the opposite,
  and does so because the other order left every program unexecutable.

- **`CONTRIBUTING`'s checklist listed three test targets and claimed CI runs the same
  ones.** CI runs six.

- **The version badge at the top of README said 0.9.2 through three releases.** The
  limits table, the syscall table, the table of contents and the counts were all read and
  reconciled; the badge is the first thing on the page and was the last thing looked at.
  The most visible part of a document is the part nobody audits, because auditing is done
  by section and the badge belongs to no section.

- **Most of the numbers in the project layout tree were wrong**, the example `qemu`
  invocations named an ISO five minor versions old, `mkfs.py` was still listed after
  being removed, and three programs were missing from it.

### Known issues

- **`test_time`'s clock round trip is not deterministic and the cause is not
  established.** It fails roughly two full runs in five. An extra update-in-progress
  wait was tried and made it worse, which also disproved the theory behind it. Both the
  attempt and its refutation are recorded rather than a fix nobody demonstrated.

## [0.10.0-alpha] "Stake" - 2026-08-26

Sector 0 held the superblock from v0.9.0 onward, so the disk could not say where it
began and there was no room for a partition table. It has one now, and the file system
lives in a partition rather than at the front of the device.

The 2 MB ceiling went with it, and the two had to travel together because they move the
same bytes — a user's files are not worth relocating twice. What actually capped the disk
was never the partitioning: the allocation table held one entry per **sector**, a static
`uint32_t[4096]`, so 4096 sectors was the whole of it. It counts clusters of eight
sectors now and covers **16 MB** with the same 16 KB of kernel memory.

**A disk written by v0.9.x will not be read.** It is recognised by the magic number
sitting where the partition table now goes, named as a v0.9.x disk, and refused without
a byte being written to it.

### Added

- **A partition table in sector 0.** One partition of type `0x7F` — the byte reserved by
  convention for individual and experimental use, so this kernel will not mistake a Linux
  or FAT partition for its own and nothing else will mistake this one for something it
  can read. It begins at LBA 64 rather than the 2048 a modern tool would choose, because
  2048 sectors is 1 MB and that is most of a disk small enough to partition by hand. The
  kernel writes the table itself when it formats a blank disk; no external tool is
  involved.

  The superblock stays **partition-relative**, so the file system does not know where it
  lives and moving a partition costs nothing. Exactly one function adds the offset, and
  nothing inside the file system calls the block cache directly any more — a single
  missed offset would read the right sector of the wrong place, and one door is the only
  defence against that which does not depend on remembering.

- **Clusters, and a 16 MB disk.** `FS_CLUSTER_SECTORS` is 8, so an allocation unit is
  4 KB. Clusters 0 and 1 are reserved as in every FAT ever written, which is what lets a
  `start_cluster` of 0 mean "no data at all" — what a directory and an empty file both
  record — without colliding with a real allocation.

  **The bill:** a one-byte file now occupies 4 KB, and with 512 entries the worst case is
  about 2 MB of slack on a disk that can hold 16.

- **The directory table is checked before it is believed.** An open item in
  `SECURITY.md` for several releases. Every used entry must have an id equal to the slot
  it occupies, a parent chain that reaches the root, a start cluster inside the file
  system, and a valid type. A table that fails is **refused rather than repaired**: the
  information needed to repair it is exactly the information in doubt, and quietly
  rewriting somebody's directory tree is a worse answer than declining to mount.

### Removed

- **`SYSCALL_LS_DIR` (28).** It printed a listing from inside the kernel, so its output
  could never reach a pipe; v0.9.2 moved the shell's `ls` onto `READDIR` for that reason
  and left the syscall "for any caller that genuinely wants a screen dump". There was
  never such a caller. Removing it orphaned `fs_list_dir()`, which went with it. The
  number is left free rather than reused — what becomes of 11 and 28 belongs with
  freezing the ABI at 1.0, alongside the gap at 99 where `YIELD` sits. **62 system calls.**

### Changed

- **`start_sector` is `start_cluster`.** The field's type did not change, only its unit,
  and that is precisely why it was renamed: v0.9.3 spent a release finding eight
  `(uint8_t)` casts that the compiler could not object to, and a field whose meaning
  changes under the same name is the same trap. The rename made the compiler name all
  seventeen sites.

- **Bounded string operations in user space.** The unbounded `ft_strcpy()` in the shell
  and in `init` is now `ft_strlcpy_sz()`, which takes the destination's size and returns
  the source's length so a truncation can be told from a fit. Every one of the sixteen
  calls was already safe — each of them because of a check somewhere else, a length test
  forty lines up or a buffer that happened to be as large as its source. That is safety
  that holds until somebody moves the check. `exec` now refuses a path too long for its
  buffer rather than shortening it, because a shortened command is a different command.

## [0.9.4-alpha] - 2026-08-25

A file's own mode decided nothing. `check_vfs_access()` was asked about the **directory**
an operation happened in and never about the file, and no caller anywhere passed it a
file's entry id — so `chmod 600 secret` was recorded, reported by `stat`, shown by
`ls -l`, and stood between nobody and anything.

The load-bearing case is `/etc/shadow`. It carries `0600` inside an `/etc` that carries
`0755`, so on 0.9.1, 0.9.2 and 0.9.3 any account on the system could read the password
database. That is a **regression rather than a gap**: 0.9.0 refused reads of `shadow` by
comparing the name, and 0.9.1 retired the comparison — correctly — without replacing what
it did. Both published releases now carry a banner. The hashes are salted
PBKDF2-HMAC-SHA256, so what was exposed is hashes rather than passwords.

### Fixed

- **A file's own permission bits are consulted.** `check_file_access()` asks them, in
  addition to the directory check rather than instead of it, and `open()` asks it for
  read or for write depending on which the caller wanted — opening for writing truncates,
  so it is a write. `/etc/shadow` is now readable by root alone, which is what its mode
  has claimed since 0.9.0.

- **Three name comparisons were still in the VFS, below the syscall layer.**
  `fs_create_file_raw()`, `fs_delete()` and `fs_rename()` each refused a non-root
  operation on a file called `passwd` — anywhere on the system, so `touch /tmp/passwd`
  was denied while `touch /tmp/shadow` was not, and no user could name a file in their
  own home directory `passwd`. 0.9.1 retired the comparisons in `check_vfs_access()` and
  these survived it because nobody looked below the syscalls. The mode says it properly
  now: `/etc` is `0755` owned by root.

### Added

- **The execute bit decides what may be run.** It was decided by location — anything in
  `/bin` for anybody, anything else for root alone — so executability was a property of
  where a file sat rather than of the file, and the `x` bit stored on every entry since
  0.9.0 meant nothing. `exec` asks for read and search on the directory and the execute
  bit on the file, and nothing else.

  **This opens something that was closed:** a user can run a program they wrote and
  `chmod 755`'d. That is deliberate and it is the Unix arrangement. The ELF validator
  still runs on every load, and a new process is granted nothing its parent lacked.

  Everything in `/bin` is `0755` from the boot-time mode pass. That had to land in the
  commit *before* the enforcement — the programs are written with the `0644` default and
  no execute bit, so a kernel that asked for one first would boot to a shell that could
  not launch a single command, on every existing disk.

- **A sticky bit on `/tmp`, which is `01777`.** Write permission on a directory is
  permission to remove things from it, so a world-writable `/tmp` was also a
  world-deletable one — documented as a limitation since 0.9.1. Removal and renaming are
  now restricted to the owner of the entry, the owner of the directory, and root.
  Renaming is asked as well as removal; without that, renaming somebody else's file to a
  name of your choosing would be the way around the rule.

  The bit was always stored — the mode mask has been `07777` since 0.9.0 and `chmod`
  accepts four octal digits — so what changed is that something reads it. `ls -l` renders
  it as `t`, or `T` when the directory is sticky without being searchable by others.

### Changed

- **`rm` says why it failed.** 0.9.2 stopped it discarding the return value, so
  `rm /nope && echo GONE` no longer prints `GONE`, but it still printed nothing at all
  and left the exit status carrying the whole message. A trailing slash is the case that
  showed why that matters: `rm /tmp/d/` resolves to an empty basename and is refused, and
  the silence made it indistinguishable from having removed the directory.

- **`rm` in `help` says it removes an empty directory too**, which it has done since
  directories could be removed at all.

## [0.9.3-alpha] - 2026-08-25

v0.9.0 widened an entry id from one byte to two, and the release notes for it said what
made that dangerous: the compiler catches the declarations and cannot object to a cast.
Fourteen explicit `(uint8_t)` casts were found and fixed then. Eight were not, and they
were on the paths people use most — `ls`, `cd`, `pwd` and directory deletion. None of
them fail; each answers about a different directory than the one it was asked about.

The entropy pool also stops starting from nothing. A 32-byte seed now lives in
`/var/random-seed` and is carried from one boot to the next, which closes the last of the
cross-boot uniqueness gap. It closes that and nothing else, and the release is careful
about the difference.

### Fixed

- **An entry id is two bytes wide everywhere it is used.** Eight places still narrowed
  one to a byte, which was correct until the directory table grew to 512 entries in
  v0.9.0 and silently wrong above 255 afterwards:

  - `ls <dir>` and `cd` resolve their argument through the same path resolver, whose
    access check ran against the truncated id. The permission question was asked about
    one directory and the operation then happened in another — `ls` listed the right
    directory on the wrong directory's permissions, and `cd` entered one.
  - `pwd` walked the parent chain from a truncated working directory and stored **slot
    indices** in a byte array along the way, so it rendered names belonging to other
    entries — or stopped at the root and answered `/` from inside a subdirectory.
  - `fs_delete()` decided whether a directory was empty by comparing each child's
    `parent_id` against the parent's slot index narrowed to a byte. Above 255 it found no
    children, so a directory that still held files was deleted and its contents were
    orphaned — the exact loss that check exists to prevent, reached through the check.

  An entry id past 255 is reachable with a table of 512, and an `ls` on a directory id
  no entry can hold now reports `E_NOENT` rather than an empty listing.

### Added

- **An entropy seed that survives the machine being switched off.** 32 bytes in
  `/var/random-seed`, mixed into the pool once the disk is mounted and written back at
  `sync`, `halt` and `reboot` alongside the kernel log. Cross-boot uniqueness used to
  rest on the RTC second and the TSC, so two cold boots of one image inside the same
  second were not provably distinct — and an image copied to two machines started both
  identically.

  Two properties are deliberate. The seed is **credited zero entropy bits**, so
  `entropy_quality()` reports exactly what it would have without one: bytes written by a
  pool that never reached cryptographic quality do not acquire any by being stored. And
  the seed on disk is **replaced the moment it is read**, not at the next checkpoint, so
  a machine that loses power before reaching one cannot come back up and mix in a seed it
  has already used.

  It is created `0600` and owned by root, and the boot-time mode pass enforces that
  again on every boot — `/etc/shadow`'s treatment, for `/etc/shadow`'s reason. A refusal
  from `IMMUTABLE` or `LOCKDOWN` is recorded and never escalated: a machine that would
  not halt because it could not refresh its seed would be the worse bargain.

### Found in passing

- **`SYSCALL_LS_DIR` (28) has no caller anywhere.** v0.9.2 moved the `ls` builtin onto
  `READDIR` because this one prints its listing from inside the kernel and never reached
  the caller's descriptor 1, and left the syscall in place "for any caller that genuinely
  wants a screen dump". There is none — not in `/bin`, not in the shell, not in the
  tests. That is exactly where `SYSCALL_CAT_FILE` stood before v0.9.2 removed it, and it
  carries the same defect. Two of the eight truncations fixed here were in it. Left for
  its own release rather than folded in late, alongside the sticky bit.

- **`/var/log` was on the roadmap as a P1 for five releases after it was done.** The row
  asked to separate the log from the screen transcript, wrap the buffer and write it out.
  v0.6.1 did all three. Removed, and moved to the list of things already finished.

- **`/bin`-less `rm` reports failure only in `$?`.** `rm /nope` prints nothing and exits
  1, so a mistyped path — `rm /tmp/d/` with a trailing slash resolves to an empty
  basename and refuses — looks exactly like success. `cd` and `chmod` both print. Not
  introduced here and not fixed here.

- **Three comments said the permission bits were stored but not enforced**, in `fs.h`,
  `stat.h` and `/bin/stat`. Each was written before v0.9.1 and each was still there
  afterwards, telling anyone reading that a mode was decorative.

- **A comment in `sys_fs.c` sized the `getcwd` path buffer against a `MAX_FILENAME` of
  256.** It has been 64 since v0.9.0. The bound it justified is still correct; the
  arithmetic offered for it was not.

- **The two write paths disagree about which errno `IMMUTABLE` returns** —
  `fs_create_file()` answers `E_ROFS` and `fs_atomic_update()` answers `E_ACCES`.
  Deliberately not reconciled here: every caller of both already treats them alike, and
  changing one is a behaviour change that belongs in its own release.

## [0.9.2-alpha] - 2026-08-25

The last category on the list to 1.0: answers that looked right and were not. Every one
of them was broken the same way — the command ran, exited zero, printed the right thing
on the screen, and sent nothing at all to a pipe or a file.

```
meminfo > mem.txt          # empty file, no error
cat_raw f | grep something # empty pipe, no error
grep nothing f && echo hi  # prints hi, having matched nothing
```

The cause was one thing: the output was printed from the kernel with `printk()` and
never reached the caller's descriptor 1. `dmesg` was fixed this way in v0.8.x and the
rest follow it now — the kernel renders, the caller writes.

### Fixed

- **`meminfo`, `stack`, `testmalloc`, `hexdump` and `/bin/free` write their own output.**
  Each renders into a buffer the caller supplies and returns its length, in the shape
  `readdir` already used — buffer in `ebx`, capacity in `ecx`, bytes back in `eax`. The
  loop stays in the shell for the reason `dmesg`'s does: a write into a full pipe blocks,
  and a blocked syscall resumes by re-running from the trap, so a kernel-side dump that
  blocked halfway would start over and emit everything twice. A buffer too small is
  filled rather than refused.

- **`cat_raw` is a program again.** It used to be a syscall that took a path, read the
  whole file and printed it as hex from the kernel. Rendering that into a buffer would
  not have helped — 64 KB of file is 192 KB of hex — so the primitive changed instead of
  its output. `SYSCALL_CAT_RAW` (34) became `SYSCALL_READ_RAW`: `read()` against the
  stored form, same descriptor, same offset, same shape. Opening, looping and formatting
  are the shell's work now, and the only part that needed to be a syscall is the part
  that bypasses decryption.

- **`grep` reports what it found.** 0 matched, 1 did not, 2 could not look — the
  convention it has everywhere. It returned 1 for both "nothing matched" and "no such
  file", which made the status useless for the one thing a status is for: `grep x f &&`
  ran its second half whether or not anything was found. **Errors moved from 1 to 2**,
  which is a behaviour change for anything that was checking.

- **The clock can be set.** `date -s "YYYY-MM-DD HH:MM:SS"`, root only. The RTC could be
  read and never written, so `date` reported whatever the machine came up with and
  nothing could correct it — which mattered more once v0.9.0 started stamping files. The
  write is the read path run backwards: it asks register B what format the chip is in and
  writes that, rather than reconfiguring the hardware to suit itself, and it halts the
  update cycle so a tick cannot land between the hour and the day.

### Removed

- **`SYSCALL_CAT_FILE` (11).** It printed a file's contents from the kernel and had no
  caller at all — the shell's `cat` opens the file and writes it itself, which is why
  `cat` worked in a pipe and this would not have. Dead and wrong at once. The number is
  left free rather than reused; what becomes of it belongs with freezing the ABI at 1.0,
  alongside the gap at 99 where `YIELD` sits.

### Changed

- **`lockdown` keeps printing from the kernel, and now says so in the log.** It was on
  the list and comes off it: what it prints is not output, it is a red banner announcing
  a state change to whoever is at the console — the same class as the message `halt`
  prints on the way down. There is no pipeline that wants it. What it was missing is a
  `klog` record, so `dmesg` can now say when the system was locked.

## [0.9.1-alpha] - 2026-08-25

v0.9.0 wrote a mode, an owner and a group onto every file and consulted none of them.
That was deliberate and it was disclosed, but a field the system reports and nothing
reads is exactly what this project spends its effort avoiding. This release reads them.

What decided access until now was `check_vfs_access()`, and what it did was compare
names: `"tmp"` at the root was writable by anybody, an entry called `"root"` and owned
by uid 0 was closed to everybody, `"shadow"` could not be read, and otherwise you could
write what you owned. It worked, and it made a file's permissions a property of what it
was called — renaming a file changed who could touch it.

### Added

- **Permission bits decide.** The Unix rule, as it is: reaching an entry needs search
  permission on every directory above it, and the operation then needs read or write on
  the directory it happens in. The class that matches — owner, group, other — is the
  only one consulted, so an owner with no permission is refused rather than falling
  through to the group bits. That last part is the one people misremember, and it is
  what `chmod 077` depends on.

  One deliberate difference: the read case asks for `r` *and* `x` rather than `x` alone.
  A directory that is searchable but not readable lets a caller open a file whose name
  it already knows while refusing to list the directory, and this check is not told
  which of the two its caller is about to do. Asking for both is stricter than Unix,
  never looser.

- **`chmod` and `chown`.** Octal modes only — symbolic ones are a parser and a lot of
  behaviour to get subtly wrong, and they say nothing extra about nine bits. `chmod` is
  the owner's and root's; `chown` is root's alone, because giving a file away is how a
  user gets out from under a quota on a system that has one and this restriction is far
  easier to keep now than to add back.

- **`ls -l`** — mode string, owner and group, size and modification time. `readdir`
  returns a name and a type and nothing else, so the rest of each row comes from one
  `stat` per entry; a directory of a few dozen can afford it and it needs no new system
  call. Times are shown in local time like `stat`'s.

- **Tab walks the candidates.** Pressing Tab again moves to the next one and replaces
  the word, wrapping at the end — what a terminal does. The first Tab is unchanged: it
  completes a single match, or narrows to the common prefix, or lists what there is.
  Any other key ends the walk, because the list was gathered for one word in one state
  of the line.

### Fixed

- **The system's own paths get the permissions they must have, on every boot.**
  `/etc/shadow` is `0600`, `/tmp` is `0777`, `/root` is `0700`, and the rest of the top
  level is `0755`. This runs every boot rather than only when the entries are created,
  and that is a security requirement rather than tidiness: everything v0.9.0 created
  took the default `0644`, `/etc/shadow` included, so a kernel that began enforcing
  modes and only stamped new entries would have handed the password database to every
  user the first time it mounted such a disk.

- **`/tmp` is cleared completely on boot again.** The loop that empties it was written
  when the directory table held 256 entries and stayed at 256 when v0.9.0 doubled it, so
  anything landing in a high slot survived the boot that was supposed to remove it. A
  regression introduced in v0.9.0 and reported here rather than quietly corrected.

- **`/home/<user>` gets its group as well as its owner**, and the change is written to
  the disk. It was set by assigning into the directory table directly, which persisted
  only because something later happened to save the table, and which indexed that table
  with an entry id — correct today only because the two happen to coincide.

## [0.9.0-alpha] "Vouch" - 2026-08-24

The disk says what it is. Until this release nothing on it did: `init_fs()` read the
directory table and the allocation table out of their sectors and used whatever was
there, with no magic number, no version and no check of any kind. A kernel could not
tell one of its own images from an older one, from a disk belonging to something else,
or from two megabytes of noise — and that gap kept itself open, because without a
version field the *next* format change could not have recognised these disks either.

This is the one change that cannot be undone, so everything the format was short of
went in at once: permission bits, a group, two timestamps, and entry ids wide enough
to stop capping the file system at 256 files.

### Added

- **A superblock, in sector 0.** Magic, format version, and the geometry the file
  system was laid out with — where the directory table starts and how many sectors it
  occupies, where the allocation table starts, where data begins, how big an entry is,
  how many sectors to a cluster. A mounted file system is read with the geometry its
  superblock records rather than with the constants this build was compiled with, so
  changing the layout later does not need a new version number at all.

  Sector 0 was empty before, reserved for a partition table that has not arrived yet.
  It still is: the superblock is the file system's, and v0.10.0 gets the argument about
  who owns sector 0 when partitions land.

- **`mode`, `owner_gid`, `mtime` and `ctime` on every entry.** Files are created 0644,
  directories 0755, owner and group from the creating task. `mtime` moves when the
  contents change and `ctime` when the entry does — a rename touches the second and not
  the first, which is the whole reason there are two.

  **Nothing enforces the mode yet.** `check_vfs_access()` still decides by comparing
  names against `"tmp"`, `"root"` and `"shadow"`, and replacing it is what v0.9.1 is
  for. A mode that is recorded truthfully and not yet consulted is a smaller lie than
  one invented at the moment something asks for it, and `stat` says which it is.

- **An epoch.** `esdtime.h` said there was no epoch anything in this system agreed on,
  and when it was written that was true — the consumers were `date(1)` and the log, and
  both want the fields broken down. A file's timestamp is the consumer that does not:
  every question anybody asks of one is a comparison. The broken-down form stays and
  the conversions sit beside it, header-only and pure so the suite can call them
  directly. It runs out in 2106, which is written down rather than worked around.

- **`stat` reports the group, the mode and both times**, the times as dates rather
  than as the counts they are stored as. A timestamp of zero prints as a dash, not as
  1970: a field that was never set has no time, and the start of the epoch is a real
  instant that would read like one.

  Stored in UTC, displayed in local time with the offset alongside. Storage has to be
  UTC or the count is not an epoch — two files stamped either side of a timezone change
  would compare by what the clock said rather than by when it happened. Display does
  not, so `stat` asks the clock for the offset the system is set to and puts it back on.

### Changed

- **An entry is 96 bytes, down from 272, and there are 512 of them instead of 256.**
  The name field was 256 bytes of a 272-byte entry — 94% of the format spent on
  something nothing had ever filled past a few dozen characters. At 64 it pays for
  every field above *and* for twice as many entries, and the directory table costs 48 KB
  of kernel memory where it used to cost 68 KB.

- **`entry_id` and `parent_id` are 16 bits.** The file system was capped at 256 entries
  because the field was one byte, which is not a limit anybody chose. They have a name
  now, `fs_id_t`, so the next widening is one line instead of eighty-five.

- **`MAX_FILENAME` and `MAX_PATH` are different constants.** They were the same one,
  so shortening the name would have quietly shortened every path buffer with it. A name
  is one component and a path is all of them.

- **A name too long is refused, not shortened.** Both the VFS and the path resolver used
  to truncate: at 256 bytes that took a name nobody would type, and at 64 it takes an
  ordinary long one — where the result is not an error but a *different file*, created
  under a name the user did not ask for, and two long names sharing a prefix becoming
  one file.

- **An unrecognised disk is refused, and nothing is written to it.** An image from
  v0.8.4 or earlier is recognisable exactly because the old format never used sector 0,
  so a zero superblock over a non-zero directory region can only be one thing. Handed
  one, this kernel would otherwise have read 272-byte entries as 96-byte ones, found a
  directory tree that was not there, and written its own tables over the user's files.

### Removed

- **`tools/mkfs.py`, and the host Python test lane that existed to run it.** The tool
  wrote a format with a 32-byte name field that no version in years has used, and its
  one caller was `tests/host/python/test_mkfs.py`, which asserted that a Python function
  had produced a 2 MB file and nothing else about it.

  It had also become actively wrong: `write_disk()` puts a message in sector 0, which is
  where the superblock now lives, so the image it produced would be declined by the
  kernel it is supposed to make disks for. And it wrote `disk.img` into the working
  directory, meaning `make test` overwrote the real one.

  Rewriting it to emit a valid v0.9.0 image was the alternative and was rejected: the
  kernel formats a blank disk itself on first boot, so nothing needs the tool, and a
  Python copy of the format would be a second definition to keep in step with `fs.h`.
  With its only test gone the `make test` Python lane ran zero tests, which
  `unittest discover` reports as a failure, so the lane goes with it.

## [0.8.4-alpha] - 2026-08-24

The editor became usable and the shell stopped lying about what you had typed. Nothing in
the kernel changed for either of them: the terminal already had every sequence this
needed, and what was missing was arithmetic on the other side of the system call.

### Added

- **Undo in `/bin/edit`, on `u`.** A log of changes and their reverse, and it gives back a
  group at a time rather than a byte: a sentence typed in insert mode is one thing the
  user did, so it comes back in one press. Each command in normal mode is its own group,
  and `o` and `O` keep the line they opened together with the text typed onto it.

  It lives in `include/editbuf.h` with the rest of the buffer, which is what makes it
  testable — an undo that is wrong hands back a file that is not the one the user had, and
  the only way to know it is right is to run it where no screen is involved. The log holds
  256 changes and 8 KB of removed text; a session that exhausts either gives up its oldest
  changes, never its newest, and says so on the status line.

  Redo is deliberately absent. `u` repeats backwards, in the shape `vim` has, rather than
  toggling the way `vi` does — a toggle punishes the second press.

- **Search in `/bin/edit`: `/` to find, `n` and `N` to repeat.** Plain substrings, no
  patterns; both directions wrap, and the status line says when they did. An empty pattern
  after `/` repeats the last one. `:` and `/` read their line through the same function
  rather than two copies of one.

### Fixed

- **A typed command line longer than the screen is wide is now redrawn in full.** It was
  drawn only on its last row, because every redraw reached column zero with a CUB and CUB
  stops at the start of the row it is on. Editing the middle of a wrapped line left the
  rows above it showing whatever they had shown before.

  The prompt is always printed after a newline, so it starts at column 0, and this
  terminal wraps the moment column 80 is written rather than deferring it — which makes
  prompt and line one run of cells where cell *n* is at row *n*/80, column *n*%80. Moves
  are worked out in that arithmetic and made with CUU and CUD for the rows, so they cross
  a row boundary. Nothing asks the terminal where it is, and nothing needs to: every move
  is relative, and output that scrolls the screen moves the line and the cursor together.

  The erase changed with it. A line shrinking from three rows to two leaves its third row
  behind, and EL cannot reach it, so the redraw ends in ED — everything below the line is
  blank anyway, the prompt being the last thing printed.

- **Tab completion no longer keeps its own copy of the prompt.** Listing candidates
  reprinted the prompt from a duplicate of those lines, so the line editor's idea of how
  wide the prompt is would have been left behind by the one path that reprints it.

## [0.8.3-alpha] - 2026-08-23

The thing the last three releases were for. v0.7.1 gave user space memory it could ask
for, v0.8.0 taught the terminal to take orders, v0.8.1 made a program something you could
put down and pick up again, and v0.8.2 gave the keyboard the keys to move a cursor with.
None of them had a consumer. This one is it.

### Added

- **`/bin/edit`, a modal text editor.** In the shape `vi` has: keys are commands until
  `i` puts it in insert mode and Escape brings it back. `h j k l` and the arrows move,
  `0` `$` Home and End go to the ends of a line, `gg` and `G` to the ends of the file;
  `i a A o O` insert, `x` and `dd` delete; `:w` writes, `:q` quits, `:q!` discards,
  `:wq` does both and `:<number>` jumps to a line. Ctrl-L redraws.

  Modal rather than modeless because of what this system has, not out of nostalgia:
  there is no Alt and nothing past F3, so a nano-shaped editor would have to spend
  Ctrl-letter combinations on its commands — and Ctrl-C, Ctrl-D and Ctrl-Z are already
  the terminal's, which is exactly the set such an editor wants for quit, save and cut.

  Ctrl-C is declined, because throwing away an unsaved buffer should take more than one
  key. Ctrl-Z is not: stopping the editor and bringing it back with `fg` is what the job
  control releases were for, and it catches `SIGCONT` to repair the screen the shell drew
  on while it was away — the delivery v0.8.2 added exists for exactly this.

  A file is at most 64 KB, which is the most `MAX_FILE_WRITE_BYTES` will write back. The
  buffer is one allocation of that size through `umalloc()`, which at that size takes a
  mapping of its own — the first real user of the `mmap` v0.7.1 added.

- **`include/editbuf.h`**, the line arithmetic the editor is built on, header-only and
  pure. It makes no system calls and touches no globals, which is what lets the test
  suite include it: `/bin/edit` is a freestanding translation unit with no link step, so
  logic that is not in a header is logic the suite cannot reach. `umalloc.h` is
  header-only for the same reason.

### Fixed

- **Scrollback on Shift with the Page keys never worked.** It shipped that way in v0.8.2
  and could not have worked on any keyboard: pressing an extended key while Shift is held
  makes the controller cancel the shift first and restore it afterwards, so Shift with
  Page Up arrives as `E0 AA`, `E0 49` — the `AA` being a shift *release* the user never
  performed. The driver acted on it and then tested the modifier a byte later, so it was
  always clear at the moment it mattered. A real shift has no `E0` in front of it, which
  is the whole of the distinction; the fake ones are thrown away now.

- **The terminal refreshes once per write, not once per escape sequence.** It repainted
  all 80 by 24 cells for every completed sequence and reprogrammed the hardware cursor
  for every printable character. A shell writing a prompt never noticed; a program
  drawing a screen could not survive it — one editor frame came to roughly fifty full
  repaints and seven thousand port writes, and under emulation each of those port writes
  is a trap out of the guest. What the screen looks like halfway through a write is
  something nobody can see, so the refresh is now owed and paid once at the end. Every
  program gets it, `printk()` included.

- **A record kept off the console is no longer a record thrown away.** `klog_record()`
  writes to the ring without printing, and it now has an integer form. Four signal
  records that had been demoted to `DEBUG` to keep them off the screen are back at
  `INFO` and silent: there is one threshold and it gates the ring as well as the console,
  so demoting a record to hide it discarded it instead — gone from `dmesg` too. Lowering
  the level was the wrong tool and this is the right one.

### Known issues

- **No undo.** An editor without one is an editor you have to be careful with; `:q!` is
  the whole of the recovery.
- **No search, no yank and no put.** `dd` deletes a line and nothing holds it afterwards.
- **A line longer than the screen scrolls sideways** rather than wrapping, and the file
  is capped at 64 KB — both are what the terminal and the file system can do.
- **A file that ends in a newline shows an empty line after it**, where `vi` would show
  only the text. A newline separates lines here rather than ending them, which is the
  honest reading for a buffer that is the file's bytes: pressing Enter at the end of the
  last line produces exactly that newline, and hiding the resulting line would hide the
  one thing the key just did.
- **Ctrl-Z still does not reach the guest under `-display curses`.** Stopping the editor
  from a curses session needs the QEMU monitor's `sendkey ctrl-z`.

## [0.8.2-alpha] - 2026-08-23

v0.8.0 taught the terminal to take orders and said plainly that nothing in user space
sent it any. The consumer is a text editor, and an editor could not be written: the
arrow keys reached no program at all, a program could not tell the Escape key from the
start of an escape sequence, and one that had been stopped and resumed had no way to
learn it must redraw. This release is the input half, and the shell is its first user.

### Added

- **The navigation keys reach programs.** The keyboard sends what a terminal sends —
  `ESC [ A` through `ESC [ D` for the arrows, `ESC [ H` and `ESC [ F` for Home and End,
  and the numbered forms for Insert, Delete and the Page keys. Until now the arrows were
  bound to the scrollback and the rest landed on zero entries in the layout tables and
  were dropped without trace.

  A sequence is placed in the input ring whole or not at all. A reader handed the first
  two bytes of an arrow key has no way to know the rest was dropped: it waits for a byte
  that is not coming, or takes the next unrelated key as the tail of the sequence.

- **The shell edits the line.** Left and right move within it and typing inserts where
  the cursor is, Home and End jump to the ends, Delete removes forward. Up and down walk
  the last eight commands, and the line being typed is saved when you walk away from it.
  This is the first thing in the system to use the escape sequences v0.8.0 taught the
  terminal to draw.

- **`POLL` (63) asks whether a read would block.** One byte both ends the Escape key and
  begins a sequence, and there is no timer fine enough to tell them apart by how long the
  next byte takes. Asking whether a byte is already waiting does tell them apart, and it
  is the only question a program can ask that does not consume a byte it may have to give
  back. End of file counts as "would not block": a read that returns zero has returned.

- **`SIGTTIN` (21) stops a background job that reads the terminal.** There is one input
  ring and every blocked reader is woken when a key arrives, so such a job did not share
  the keyboard with the shell — it raced it for each keystroke, and half of what the user
  typed vanished into a job they had deliberately put out of the way. Stopping loses
  nothing: `jobs` shows it and `fg` gives it the terminal it was asking for.

- **`SIGCONT` is delivered to a process that registered a handler.** Being runnable again
  is the whole of the default action, but a program that draws has to repair what the
  shell wrote over its screen while it was stopped, and there was no moment at which it
  could find out that it had been.

### Changed

- **Scrollback moved from the arrow keys to Shift with the Page keys**, which is where
  every terminal emulator puts it. It had the arrows because nothing else wanted them;
  something does now, and a key the driver consumes is a key no program will ever see.

- **A process ending by signal no longer prints a line of kernel log.** Those records
  moved from `INFO`, which the console shows, to `DEBUG`. A process ending because the
  user pressed Ctrl-C is the user's own doing, and over a full-screen program the line
  lands in the middle of the display. The cost is worth stating: a `DEBUG` record at the
  default threshold is discarded rather than hidden, so it is not in the ring for `dmesg`
  either. Separating the console level from the ring level is the honest fix and belongs
  to a release about the log.

### Known issues

- **The line editor works within one screen row.** The cursor is moved with `CUB`, which
  cannot cross a row boundary, so a command long enough to wrap past column 80 can still
  be typed and run but is redrawn only on its last row.
- **`SIGTTOU` does not exist.** A background job that *writes* to the terminal still
  does, interleaving its output with whatever the shell is drawing.
- **A task that catches or ignores `SIGTTIN` reads from the background as before.** POSIX
  returns `EIO` there; a program that went to the trouble of catching the signal has said
  it knows what it is doing.
- **The Escape key does nothing at the prompt**, and the key pressed after it is held for
  one round while the shell decides whether a sequence is starting. Nothing is lost — the
  byte is put back — but a lone Escape has no effect of its own.

## [0.8.1-alpha] - 2026-08-22

v0.8.0 gave the user a way to stop what they had just started. This one gives them a
way to set it aside instead - which needs something the scheduler never had: a task
that is neither running nor waiting for anything, holding its memory and its place in
the program, until somebody asks for it back.

### Added

- **Ctrl-Z, `fg` and `bg`.** `SIG_TSTP` (20) goes to every process in the foreground
  group and parks it in a new task state, `TASK_STOPPED`. `SIG_CONT` (18) takes it out
  again. `fg` gives a job the terminal and waits for it, `bg` lets it run on without
  one, and both name a job as `%1` or by number - or nothing at all, which means the
  most recent.

  A stopped task remembers what it was doing. Almost every blocking syscall in this
  kernel resumes on the trap instruction and re-evaluates what it was waiting for, so
  such a task is released as runnable and repairs itself; `exec()` is the one that
  does not, because it returns through a register rather than re-running, and a task
  stopped inside one goes back into exactly that wait.

  Both signals are catchable, and that is load-bearing rather than incidental. The
  shell and init are both in the foreground group when no job is running, so a stop
  neither of them could decline would park the session with nothing left able to
  continue it. There is deliberately no `SIGSTOP`: an uncatchable stop would reach
  init the same way and nothing could be done about it.

- **`wait()` can report a child that stopped.** `WUNTRACED` (2) asks for it and
  `WNOHANG` (1) keeps its old meaning; the status comes back as `WSTATUS_STOPPED`
  (0x100) OR'd with the signal. Without the flag a stopped child stays invisible,
  which is the right default - a caller that knows nothing about job control would
  otherwise be handed a pid it would treat as finished for a process still very much
  alive.

- **`exec()` that hands back a pid.** A non-zero mode argument (`EXEC_NOWAIT`) starts
  the program and returns its pid instead of blocking for its exit status. The shell
  could not own a foreground command without it: never learning the pid, it could not
  put the program in a group of its own, could not hand it the terminal, and had no
  way to name a job the user had stopped. A foreground command is now exactly what a
  pipeline already was.

- **`kill()` with a negative pid signals a process group.** Needed to continue a job:
  the process the shell forked is often not the only member, because that process
  started the program the user is looking at and one Ctrl-Z stopped both. Continuing
  only the pid the shell knows about would leave it blocked on a task still stopped.

### Fixed

- **A parent inside `exec()` was woken by whichever child died first.** It used to be
  any child at all, which was the same thing only while `exec()` was the only way to
  have one. With background jobs it is not: `sleep 30 &` finishing while the shell sat
  in `exec()` returned the job's status as the foreground command's, so the shell
  printed a prompt with that command still running and both then read the keyboard.
  The pid being waited for is recorded, and the reaper delivers only for that one.

- **A task that never held the terminal could give it away.** Waking a parent moved the
  terminal to it whatever the dying task was — and a shell's `wait()` is woken by the
  job it is waiting for and by every other child it has. So a background job finishing
  while a foreground job ran took the terminal from the job on the screen, and the next
  Ctrl-C reached a shell that ignores it. The same rule that already kept a pipeline's
  terminal until its last stage exited now covers this: a group that still has a member
  keeps it, and a group that never had it cannot pass it on.

- **The `exec` builtin reported success for a program that failed.** It tested only
  for a negative return, so any exit status counted as 0. It now takes the same path
  as an ordinary command and reports what the program reported.

### Known issues

- **Ctrl-Z does not reach the guest under `-display curses`.** The key gets as far as
  the terminal QEMU runs in — `stty susp undef; cat -v` echoes `^Z` there — but the
  curses front end does not turn it into a guest keypress; Ctrl-C and Ctrl-D are
  delivered normally. The `^Z` echo is the first thing the driver does, so no `^Z` on
  screen means nothing arrived. The QEMU monitor's `sendkey ctrl-z` reaches the guest,
  and so does `kill <pid> 20` from inside the OS.
- A **builtin cannot be stopped**, for the same reason it cannot be interrupted: it is
  the shell, and the shell declines both signals. `sleep 30` typed at the prompt runs
  to completion.
- A job put in the background with `bg` **competes with the shell for the keyboard**
  if it reads standard input. Unix answers this with `SIGTTIN`, which stops a
  background process that tries to read the terminal; there is no `SIGTTIN` here yet.
- **Ctrl-Z at an idle prompt discards the line being typed**, exactly as Ctrl-C does.
  The shell ignores the signal, but the read it is blocked in is still cut short and
  cannot tell which signal cut it.
- **`wait` will not wait for a stopped job.** It says so and stops rather than blocking
  on something that can never finish - the shell ignores Ctrl-C, so there would be no
  way out of it.
- `SIG_CONT` is **not delivered to user space**. It is acted on where it is sent,
  because a stopped process never reaches a delivery point of its own.

## [0.8.0-alpha] - 2026-08-22

The terminal starts taking orders, and a process starts belonging to something. Two
halves of the same idea: a full-screen program needs to say where to draw, and the
user needs to be able to stop what they just started - which is never one process.

### Added

- **ANSI escape sequences.** Cursor positioning and relative motion, erase display and
  line, colour and attributes, a saved cursor, a scroll region, and line insert and
  delete. Enough for a full-screen program to draw with; anything else is swallowed
  rather than printed, because a sequence the terminal does not implement should leave
  no trace instead of spraying its parameters across the screen.

  Rows are counted in the 24 the screen actually shows. There are three coordinate
  spaces in the driver - the 25 rows the hardware has, the 24 of text because row 0 is
  the status bar, and the 100 the scrollback buffer holds - and an escape names rows in
  the second while the cursor is stored in the third.

- **Process groups.** A task belongs to one, named by the pid of the task that founded
  it, and inherited from its creator alongside uid and working directory. The terminal
  now points at a group rather than at a process: `SETPGID` (60) places a process,
  `TCSETPGRP` (61) hands the terminal over, `GETPGID` (62) reads it back.

  Both restricted deliberately. A caller may place itself or a child and nothing else,
  and may hand the terminal only to its own group or one holding a child - otherwise a
  background job could take the terminal from the shell that started it, and the next
  interrupt would reach something the user was not looking at.

- **Ctrl-C.** `SIG_INT` goes to every process in the foreground group. The shell puts
  each pipeline in a group of its own and hands it the terminal, so `ls | grep etc` is
  three tasks the user can stop with one key, and takes the terminal back when the job
  is done. Background jobs get their own group and are never given the terminal, which
  is what keeps the interrupt away from them.

  The shell ignores `SIG_INT` itself. Between commands the foreground group is the
  shell's own - there is nothing else to hand the terminal to - and a shell taking the
  default action would end the session the first time somebody pressed Ctrl-C at an
  idle prompt.

### Changed

- **The keyboard stops delivering Ctrl-C as data.** The driver has folded Ctrl-letter
  combinations into control bytes since v0.5.3, so `0x03` has been arriving in the
  input ring all along and being handed to whatever happened to be reading. What was
  missing was never the key: interrupting one process is not what Ctrl-C means, and
  until a process could belong to a group there was no way to name everything the user
  had started with a single command.

- **The terminal changes hands when a group empties, not when a task dies.** Reaping
  the foreground *task* used to hand the terminal on, which was the same thing only
  while a group could not have two members - the first stage of a pipeline exiting
  would have taken the terminal from the stage still running.

- **The view follows the cursor only when the cursor leaves it.** `view_offset` was
  recomputed from the cursor on every character, which works while the cursor can only
  move forward and becomes impossible once an escape can put it anywhere: positioning
  to the top of the screen and printing one character would have snapped the view down
  by twenty-three rows, so absolute addressing could never have worked. Sequential
  output is unchanged.

- **`jobs` numbers are stable.** It printed the table position, which renumbers every
  time an earlier job is collected - so the name of a job changed under the user
  between one listing and the next. A job is also a group now rather than a pid,
  because that is what the terminal addresses.

## [0.7.2-alpha] - 2026-08-21

A log record becomes a record. v0.6.1 made the ring wrap and stopped `printk()` feeding
it, so it stopped being a transcript of the screen — but a record was still just a line
of text, and every question about one was a question about parsing. When did this
happen. How many did we lose when it wrapped. Show me only the errors. None of them
could be answered, so none of them were asked.

### Added

- **A record ring.** 512 structured records against the 8 KB of flat text that held
  roughly 130 lines, and each carries its own level, module, monotonic timestamp and
  sequence number rather than a seven-character prefix on a string. About 88 KB of
  kernel data, which on a 128 MB machine buys a great deal for very little.

  The timestamp is a tick count, not a wall-clock time. It is monotonic and stays
  correct when the RTC is not, which is the property a log needs, and it is rendered to
  hundredths because `TIMER_HZ` is 100 — printing the six decimals Linux does would be
  printing precision this clock does not have.

- **Sequence numbers, and the count of what was dropped.** The byte ring wrapped
  mid-line and counted nothing, so a gap in the log looked like a quiet period. `dmesg`
  reports the count on descriptor 2, not 1: it is a note about the log rather than part
  of it, and putting it on stdout would drop it into the middle of `dmesg > boot.log`.

- **`KLOG_CTL` (59).** The severity threshold had sat at INFO since boot with nothing
  able to move it, so every DEBUG record the kernel composed was discarded unseen — a
  filter nobody can adjust is a filter that only ever removes. Clearing the log and
  moving the threshold change what everyone else sees and are root's; reading the
  threshold and the counters change nothing and are anyone's, which is what lets an
  ordinary program notice records went missing between two reads.

- **`dmesg -c`, `-n` and `-l`.** `-n` sets the kernel's threshold and prints nothing;
  `-l` chooses what to show out of what was already recorded. Deliberately not the same
  flag: running them together would make `dmesg -n debug` look as though it had lost
  the log. `-l` filters in the shell, as it does on Linux, so the syscall surface stays
  as small as it was.

- **`/dev/kmsg`.** Records in the structured form — `level,seq,ticks,flag;module: text`
  — so a reader wanting only errors reads the first field instead of parsing a prefix
  out of a line.

  Writable by root, which is what Linux's permissions on this device amount to, and
  rate limited. A record a program wrote is marked as one and carries its author's uid,
  so nothing a program writes can be mistaken for something the kernel said. Kernel
  records are deliberately *not* rate limited: a storm of errors is exactly when they
  matter, and suppressing them to protect the ring would throw away the evidence to
  preserve the container.

### Changed

- **The `DMESG` index counts records, not bytes.** A byte position cannot survive a
  record being dropped between two reads — every byte after the gap shifts and the
  reader is handed a torn line. Index 0 is the oldest record still held.

- **`klog_write_char()` is gone.** Feeding the ring one character at a time is how the
  old log came to hold the boot banner, and a ring of records has no meaning for half a
  record. The test that used it to force a wrap emits records instead, which is a
  better test of the same thing.

- **A log record's uid is as wide as a process's.** It was written as a `uint8_t`,
  which looked like plenty next to a handful of accounts and would have recorded this
  system's own `esduman`, uid 1000, as uid 232 — a record attributing itself to a user
  who does not exist. Caught by a compiler warning before it ever ran.

## [0.7.1-alpha] - 2026-08-21

Programs can ask for memory. Until now one had its ELF segments and a fixed 32-page
stack, decided by the loader and never changed again, and every tool in `/bin` worked
from arrays sized at compile time because there was nothing else to work from.

### Added

- **`brk` (56).** Moves a single boundary upwards from the end of the program's image.
  Raw kernel semantics rather than the libc wrapper's: the *resulting* break comes back
  whether or not it is the one that was asked for, so a caller finds out it failed by
  comparing. That also makes `brk(0)` the way to read the current break without moving
  it — zero can never be granted — which is why there is no second syscall for it.

  The loader plants the starting point from the highest address any `PT_LOAD` segment
  reaches, rounded up to a page. A task with no ELF image behind it — the idle task, or
  anything the test suite creates by hand — has no break and is refused rather than
  given one at whatever address its address space happened to be empty.

- **`mmap` (57) and `munmap` (58).** Anonymous, private, zero-filled pages in a region
  below the stack guard page, released independently of anything else. This is the
  primitive for one large buffer that outlives the allocations around it, which is what
  a text editor needs and what the break cannot give: a run can only be returned from
  the top, and the top is rarely the part a program has finished with.

  `munmap` refuses any range outside that region, and that check is the load-bearing
  one. Without it this is an arbitrary unmap: a program could pass its own stack, its
  own text, or the heap its allocator is standing on, and every address involved would
  be legitimately its own — nothing further down would object, and the fault would
  arrive somewhere else entirely.

- **`include/umalloc.h`.** A header-only allocator over both. Every program in
  `apps/bin` is a single translation unit compiled with `-nostdlib`, so there is no
  user-space libc to put an allocator in and no link step that would find one; each
  program takes a private copy by including it, which is the bargain they already make
  with their string helpers. It brings its own syscall wrapper rather than calling the
  program's, so that including it carries no ordering requirement.

  Small requests come off the break and freed blocks are reused, merged with whichever
  neighbours are also free. Requests of 64 KB or more get their own mapping, which
  `ufree()` hands straight back to the kernel.

- **Neither region keeps a list of what it has handed out.** The page tables already
  record exactly that, they are already walked by `fork()` and by process teardown, and
  a second record of the same facts is a second record to keep in step. `mmap` walks to
  find a free run instead of looking one up, which on a machine with sixteen processes
  is not a cost worth a subsystem.

### Changed

- **`ls` reads through the heap.** Its listing buffer was 1024 bytes on the stack, and
  `SYSCALL_READDIR` has always been willing to return 4096 — the listing simply stopped
  at an entry boundary with nothing to say that it had. The buffer is now allocated, and
  sized from the kernel's own ceiling rather than from what would fit in a stack frame.

- **`USER_STACK_TOP` was wrong and unused.** It read `0xBFFFF000` while the ELF loader
  built the stack at `0xB0000000` with its own literal, so the one constant naming the
  boundary was the only thing that disagreed with everyone else. It is the real figure
  now, and the loader reads it — which matters because the mmap region is laid out
  directly beneath the guard page and cannot be positioned from a number that is wrong.

- **Every page handed to user space is zeroed first.** A frame the allocator has just
  returned holds whatever its last owner left in it, so this is a security property
  rather than a courtesy: an unzeroed heap page is another process's memory delivered
  to this one. The pages go through `copy_to_user()` for the same reason the ELF
  loader's do — SMAP forbids a supervisor write to a user page outside that window.

## [0.7.0-alpha] "Cleave" - 2026-08-20

`fork()` stops copying. A child now gets the parent's pages themselves, both sides
give up write access, and the first write from either of them splits the page it
touched. The word cuts both ways on purpose: the pages cling together until
somebody writes, and that write cleaves them apart.

### Added

- **Reference counting in the physical allocator.** A bit answered "is this frame in
  use"; nothing could answer "by how many". One byte per frame — 32 KB at 128 MB of
  RAM — now sits behind the bitmap, so two address spaces can hold the same page and
  let go of it independently. `pmm_free_frame()` drops one owner and releases the
  frame at the last; every existing caller kept working unchanged, which is why the
  count lives there rather than beside it.

  The table is counted into the memory the allocator marks as the kernel's. Anything
  left out of that sum is memory the allocator considers free and will hand out, and
  a reference table handed out is one that gets overwritten by whoever received it.

- **Copy-on-write.** A fork used to duplicate every mapped page of the parent —
  including a 32-page user stack — and the overwhelmingly common next call is
  `exec()`, which throws all of it away. Sharing costs a page directory, the page
  tables under it and the child's `process_t`; on the test payload that is the
  difference between roughly 24 KB and roughly 170 KB per fork.

  Only a page that was writable becomes copy-on-write. One that was already
  read-only — a program's text — is shared exactly as it stands, because writing to
  it is an access violation in the child for the same reason it is in the parent,
  and marking it would have turned that into a silent private copy.

- **`meminfo` reports what is shared.** "Free" stopped being a complete answer the
  moment fork stopped spending memory: how much of what is in use is held by more
  than one address space is not derivable from any other figure on that line.

### Fixed

- **The kernel heap ignored a failed mapping.** `heap_grow()` took frames one at a
  time and mapped each one, dropping the result. `map_page()` fails when the page
  table for a directory entry cannot be allocated — the same exhaustion the free
  memory check above it watches for, one level down — and the loop carried on: the
  heap end advanced over an address with nothing behind it, and the block header was
  written into the hole. A kernel page fault, which is a panic, and it surfaced at
  whatever allocated next rather than at the growth that caused it.

- **The ELF loader leaked a frame whenever a mapping failed.** Allocation, mapping
  and zeroing shared one `||` chain, so a frame that was allocated and then failed to
  map belonged to nobody: not in the directory, so the teardown on the failure path
  could not find it, and never handed back. One frame per failed `exec()`,
  permanently. The chain also collapsed three distinct failures into one message
  naming only the middle one.

- **`kfree()` merged blocks that were neighbours in the list but not in memory.** The
  block list is kept in address order, which invites the assumption that consecutive
  entries are contiguous; the shrink path can leave a few dozen bytes between them.
  Merging across such a gap does not corrupt anything — the merged size comes out
  short of the real span, so the hole is lost rather than handed out — but the size
  stops describing the block, and every later split inherits that. `heap_grow()` had
  always made this check before merging onto the tail. Now both sites do.

### Changed

- **Two paths that a shared page would have broken silently.** Neither was reachable
  from the kernel-side test modules, and both would have failed every program on the
  system.

  `validate_user_writable_pointer()` decides whether a destination is writable by
  reading the page table entry's read/write bit — and after a fork that bit is clear
  on every writable page either side owns. `wait()`, `getcwd()` and every other
  syscall that answers into user memory would have returned `E_FAULT` on pointers
  that were never invalid. A page marked copy-on-write now counts as writable there:
  the write faults, the fault hands over a private copy, and the instruction retries.

  The other is ordering. A syscall writing into a shared buffer faults in *kernel*
  mode, because CR0.WP makes a read-only entry apply to Ring 0 as well. The page
  fault handler checked for an in-progress user copy first and would have sent the
  fault to that copy's fixup label, failing the syscall. The copy-on-write check now
  comes before it.

  A third followed from the second: resolving a fault inside a copy runs another
  copy, whose exit cleared the fixup label the interrupted one was relying on. A
  buffer inside a single page survived that; one reaching into a second shared page
  faulted again, found nothing registered, and would have taken the kernel down.
  Copies made from inside a fault handler now save and restore that state.

- **The FPU stays eager, and this is now a decision rather than an omission.**
  Switching lazily — parking the state behind CR0.TS and restoring it on the first
  use — was on the roadmap as an optimisation. It is the mechanism behind LazyFP
  (CVE-2018-3665), which leaks FPU and SSE register contents across processes
  speculatively, and every major operating system moved back to eager switching in
  2018. Exception 7 also lands in the general panic path here, so enabling TS would
  bring the kernel down on the first floating-point instruction. The cost of staying
  eager is one `fxsave` per context switch across at most 16 tasks.

## [0.6.1-alpha] - 2026-08-16

The log becomes a log: a record of events that wraps, rather than a transcript of the
screen that fills up and stops. And it survives the machine.

### Fixed

- **The log was not a ring buffer**, despite this source and the README both calling it
  one. It filled once to 8 KB and then silently dropped every record after — so `dmesg`
  showed the oldest part of the boot and nothing that had happened since. A log that
  discards the newest records is the opposite of a log. Nothing had noticed because
  nothing had ever filled it on purpose.
- **`printk()` fed every character it printed into that buffer**, so the boot banner, the
  ASCII art and the first-boot password prompts competed for the space with actual
  records — and were what filled it. A log is a record of events, not a transcript of the
  screen. `printk()` no longer feeds it; `klog()` composes a line and records that.

  The boot milestones still print their green `[OK]` list, which is boot UI, *and* appear
  in `dmesg` as records with a level and a module. That needs an entry point that records
  without printing, which is exactly what the split between the two implies.
- **`klog_int()` and `klog_hex()` put their value on the line after the message.** Both
  called `klog()`, which had already ended the line, so every value in the log sat
  orphaned beneath the text it belonged to. The value is a tail on the same line now.
- **A log message containing a `%` was read as a format string.** `klog()` passed the
  message to `printk()` as the format, which is a way to print whatever happened to be
  next on the stack.

### Added

- **`/var/log/kern.log`.** The directory has existed since the FHS hierarchy was created
  and has been empty ever since; the log lived in RAM and went with the machine. It is
  written at `sync`, `halt` and `reboot` — before the block cache is flushed, so the
  sectors go out with everything else.

  Written whole rather than appended to, because the format cannot append: a file is one
  AES-CBC blob authenticated over its entire plaintext, so adding a line means rewriting
  all of it. That is also why it is written at checkpoints rather than per record. The
  ring is snapshotted into a contiguous buffer first — it is not contiguous once it has
  wrapped, and the snapshot keeps the records this write itself produces from chasing
  their own tail into the file.

  Failure is reported and not fatal. A machine that will not halt because it could not
  save its log would be a worse bargain than a lost log — but it is *reported*: the errno
  used to be returned to three callers that all dropped it, so a checkpoint that could not
  write left the file simply absent with nothing to say why.
- **`sync` is a shell command.** `SYSCALL_SYNC` has existed since v0.4.x and nothing in
  user space had ever called it — it sat in the syscall table and in the README's
  reference, reachable from nowhere. That did not matter while it only flushed the block
  cache. It does now that it is also when the log is written, because the other two
  moments that write it are `halt` and `reboot`, and neither leaves a session to look at
  the result in.

### Changed

- `klog_write_char()` and `dump_klog()` are declared in `klog.h` rather than `kernel.h`,
  which pulls in twenty-two other headers. `KLOG_BUF_SIZE` joins them: the ring's size is
  part of its contract now that a reader cannot see past it.

### Known issues

`meminfo`, `hexdump`, `stack` and `/bin/free` still print from inside the kernel and
cannot be piped or redirected. They are root-only diagnostics and each needs a formatter
of its own.

## [0.6.0-alpha] - 2026-08-16

The clock becomes something a program can read, and stops being wrong on the last day of
a month.

### Added

- **`TIME` (55) reports the current wall-clock time.** The RTC was readable from Ring 0
  only — it drew the status bar and nothing else — so `date` printed a string compiled
  into its own binary, the same one on every boot in every year.

  The syscall fills an `esd_time_t` (`include/esdtime.h`), shared verbatim with user space
  the way `esd_stat_t` is, and laid out with no padding holes so `copy_to_user()` cannot
  hand over a byte of kernel stack. The fields are broken down rather than a count of
  seconds since an epoch: the RTC reports them that way, nothing here agrees on an epoch
  to count from, and both consumers in sight — `date` and the timestamps `/var/log` will
  want — need them broken down anyway.
- **`date` prints the actual date**, and **`date -u`** prints it in UTC. The shift between
  the two is done by the kernel rather than in `date`: moving a time between zones means
  the calendar carry, and a second copy of that arithmetic in user space is exactly what
  this release exists to get right once.
- **`/etc/timezone` sets the offset at boot.** It was compiled in, so a machine in the
  wrong place had to rebuild the kernel to see the right time. The file holds a signed
  hour count and says so in its own header — an offset and not a zone name, because
  `Europe/Istanbul` is only meaningful with a timezone database to look it up in, and
  tzdata is measured in megabytes against a 2 MB disk.

  A missing or unparseable file leaves the compiled-in default in place rather than
  failing the boot, and an offset outside −12..+14 is refused: that range is what real
  zones occupy, and anything else is a misparse that would move the date by days. The
  default still matters for exactly one second — the first status bar is drawn before the
  filesystem is up.

### Fixed

- **The date was wrong on the last day of every month.** The timezone offset was applied
  as `hour += 3` with a day carry that never looked at how long the month was, so 21:00
  UTC on 31 August produced **32/08**, and 31 December produced 32/12 rather than 1
  January. The carry uses real month lengths now, with the full Gregorian leap rule —
  including the century cases, where 2100 is not a leap year and 2000 is.

  The offset moved into a named constant, and the arithmetic handles negative offsets as
  well as positive ones. Nothing uses a negative one today, which is exactly why it is
  written: leaving half of it out is a trap for whoever changes the constant.
- **The RTC could be read mid-update.** The update-in-progress flag was checked once and
  then seven registers were read one after another, and the chip is free to begin an
  update in the middle of that. At a second boundary the values straddle it; at a midnight
  boundary they produce a date that never existed. The registers are read twice and
  compared now — two readings that agree cannot straddle an update, because an update
  always changes at least the seconds.
- **The status bar changed its own label one second after boot.** `kernel_main()` drew it
  with the version string and the per-second refresh drew it with the literal
  `"esdumanOS"`, so the left half changed as soon as the clock first ticked. Both read one
  definition now, and it is the name: the version belongs in `/etc/os-release`, where a
  program can read it, rather than in a corner of the screen.
- **The year is printed in full.** The formatter emitted `"20"` followed by the RTC's two
  digits, so the century was a literal in the middle of a string.

### Known issues

The clock still has no way to be **set** — the RTC is read and never written, so a wrong
hardware clock stays wrong. The offset is configuration now, but there is no daylight
saving: this machine's zone has been permanent UTC+3 since 2016, so a fixed offset is
correct here rather than a shortcut, and a zone that still changes twice a year would
need a database this system has no room for.

The RTC is assumed to hold UTC, which is what QEMU presents by default. A machine whose
CMOS holds local time would need `/etc/timezone` set to 0 rather than its real offset.

`stat` still reports no timestamps, because the on-disk format carries none — that is
unchanged and documented where it always was.

## [0.5.4-alpha] - 2026-08-16

The parser stops guessing, and `/etc` stops being an empty directory.

### Fixed

- **A pipeline can have more than two stages, and `>` can be combined with `|`.** The
  parser took whichever of the two it met first and stopped, handing everything after it
  on as ordinary arguments: `a | b | c` ran `a` against the literal tokens `b | c`, and
  `a | b > f` passed `> f` to `b` as two words. Both were silent — the wrong thing ran and
  reported success.

  Up to four stages now, each forked, joined by one pipe per gap, and `>` belongs to the
  stage it appears in. Four is a process budget rather than a preference: an external
  stage costs two tasks, because the forked child runs the program through `exec()`, which
  creates a task of its own. Asking for a fifth is refused with a message. So is a bare or
  doubled `|`, and so is backgrounding a pipeline — that last one used to run in the
  foreground with the `&` quietly dropped.
- **`$VAR` and `~` are expanded in every stage.** Expansion ran after the split, and the
  split had already written a null over the `|`, so the loop stopped there and no token
  past the first stage was ever expanded.
- **Every `~` gets its own storage.** One shared static buffer served the whole line, so
  each expansion overwrote the last and every token ended up pointing at the same string:
  `cp ~/a ~/b` passed `~/b` twice and copied a file onto itself. Expansions come out of an
  arena that is reset per command, and running out of it is reported rather than absorbed.
- **`/bin/rm`, `/bin/mv` and `/bin/kill` are reachable.** All three shipped in the image
  and were unreachable by any spelling, because the builtin table was consulted first and
  each name is also a builtin. A word containing a slash is a path now and is never
  matched against that table, which is the rule every real shell uses.
- **`rm <directory>` no longer orphans what is inside it.** A child records its parent as
  the parent's index in the directory table, and deleting the parent cleared that slot and
  stopped. The children stayed behind pointing at an index that no longer described them —
  unreachable through any path, and visible again as somebody else's contents the moment
  the slot was reused. A directory that still holds something is refused with `ENOTEMPTY`;
  an empty one still goes, which makes this `rmdir(2)` rather than a refusal to remove
  directories at all.

### Added

- **`/etc` has system files in it.** The directory has existed since the FHS hierarchy was
  created and held nothing but the password database, so every fact a tool needed was
  compiled into it instead.

  `/etc/os-release` carries the version the kernel was built with, generated from the same
  macro the status bar uses, so the two cannot drift. `/etc/hostname` is what the prompt
  now reads — the name was a string literal in two places. `/etc/motd` is printed at
  startup. `/etc/profile` is read by the shell before its first prompt: only
  `export KEY VALUE` is recognised, and the file says so in its own opening lines. It is a
  settings file rather than a script, because running arbitrary commands from it would
  mean forking and exec'ing before a prompt appears, and a syntax error in it would be a
  shell that will not start.

  None of the four is required. Each falls back to what the shell already had, so a disk
  image made before this release still boots.

### Known issues

These files are written in the same block as the `/bin` tools, which runs only when
`init.elf` is absent from the disk — so a disk image carried over from an earlier version
keeps its old `/etc` until it is recreated. `make run` clears the disk; `make run-dev` and
`make restart` need `make reset-disk`.

## [0.5.3-alpha] - 2026-08-16

Both ends of a pipeline learn how to stop. The writer gets a death, the reader gets an
end, there are finally programs willing to sit at the far end of a pipe — and `ls` and
`dmesg` stop writing past it to the screen.

### Added

- **`SIGPIPE` (13).** A process that writes to a pipe with no readers left is signalled,
  and the default action terminates it with status 141. `pipe_write()` has refused that
  write since v0.5.2, but a refusal is only a return value and nothing in user space reads
  one — `printk()` discards it and so does every `/bin` tool — so the stage ran to the end
  of its input with every write failing in silence. This is what actually stops it.

  Raised only for writers that arrived from Ring 3. The kernel test modules drive `write`
  through `int 0x80` as the task running the suite, and signalling that task would end the
  run — which would look like a passing one, since an interrupted run still prints
  everything it got through.
- **`SIG_IGN`.** A process can now decline a signal, which is a third state
  `signal_handlers[]` did not have: it held an address or 0 for the default. The
  disposition is stored as the sentinel 1, the value POSIX uses, so inheritance and reset
  come for free — `fork()` already copies the array and a new program image already starts
  with it cleared.

  The shell declines `SIGPIPE` for itself: losing it would end the session, and there is
  no way to get another one. Because a disposition survives `fork()`, every stage the
  shell forks would start out declining it too — which is the exact runaway this release
  exists to stop — so each forked child restores the default before running anything.

  `SIGNAL_REG` now reports what it decided: `E_OK`, `E_FAULT` for a handler address user
  space cannot execute, or `E_INVAL` for a signal number out of range. It used to answer 0
  in every case, including the ones it had refused. The check itself was written out twice
  — once in the syscall and once in `register_user_signal()` — and only one copy was
  relaxed for `SIG_IGN`, so Ring 3 callers were turned away with `E_FAULT` while
  kernel-mode callers of the same function succeeded. There is one copy now.
- **Ctrl-D ends console input.** `sys_read()` on the console either handed back a byte or
  blocked; there was no path that returned 0, so a program reading standard input from a
  terminal could never finish. That was survivable while nothing read standard input. It
  stops being survivable now, because there is one terminal, no Ctrl-C and no job control,
  so a read that cannot end takes the machine with it.

  Ctrl was the one modifier the keyboard driver never tracked. It now folds a letter to
  its control code, which is the ASCII rule, so Ctrl-D produces the end-of-file byte and
  Ctrl-C has a path waiting for it once there are process groups to send it to. Ctrl with
  anything other than a letter is passed through unchanged.
- **`/bin/wc`** counts lines, words and bytes, and reads standard input when no file is
  named. Its output is three numbers however much it consumed, which makes `something | wc`
  the cheapest way to see that a stage reached the end of its input rather than stopping
  early.

### Fixed

- **`ls` and `dmesg` output goes through the process's standard output.** Both produced
  their listing inside the kernel with `terminal_putchar()`, which knows nothing about the
  calling process — so the text went to the screen whatever descriptor 1 pointed at.
  `ls | grep bin` read an empty pipe, `dmesg | head` fed an empty pipe, and `ls > names`
  created an empty file. Redirection has been wired up since v0.4.3 and this was never
  noticed, because until v0.5.2 a pipeline could not run its stages concurrently and until
  this release nothing consumed one.

  `ls` is now built on `READDIR` (44), which already handed entries back in a buffer and
  which tab completion already used; the shell prints them itself. `DMESG` (39) grew a
  buffer form — `dmesg(buf, size, offset)` copies a slice and returns the count — and the
  shell loops over it. Passing a null buffer still dumps to the screen, which is what a
  caller with no descriptors of its own wants.

  The loop is in the shell rather than the kernel on purpose. An 8 KB log does not fit a
  4 KB pipe, so the write blocks, and a blocked syscall resumes by re-running from its
  `int 0x80` — a kernel-side dump that blocked halfway would start over and emit
  everything twice. With the offset held in the caller, each write blocks and restarts
  harmlessly.

  `meminfo`, `hexdump`, `stack` and `/bin/free` still print from the kernel and still
  cannot be piped or redirected. They are root-only diagnostics and each needs its own
  formatter; recorded under Known Limitations rather than fixed here.
- **`ls <directory>` reads its argument.** It passed the id of `.` whatever was typed
  after it, so `ls /bin` listed the working directory — and looked convincingly like
  `/bin` was empty.

  The listing loses its colour in the move: the kernel set it per entry and the shell has
  no syscall for it. The `[DIR]` and `[FILE]` markers carry the same distinction in text,
  and colour codes written into a pipe or a file would have been wrong anyway.
- **`grep` and `head` read standard input when no file is named.** Both opened a file by
  name and nothing else, so a pipeline could be parsed, forked and connected with `cat` as
  the only program in the system willing to consume one. `echo x | grep x` did not work.
- **`grep` no longer stops at the first 511 bytes of a file.** It issued a single read of
  511 bytes and searched what came back, so a match on line 40 of a 2 KB file was simply
  not found — silently, with an exit status of 0. It now assembles lines as bytes arrive,
  which is the same loop the standard-input path needs, so the two became one. A single
  line longer than 256 bytes is truncated rather than split; splitting would report one
  line as two and could match across a boundary that is not in the input.

### Deliberately not done

- **Ctrl-D at the shell prompt does not exit the shell.** The byte is dropped by the input
  loop, which accepts only printable characters, and the read returns 0 once before
  blocking again — no busy loop, no effect. Making it exit is a behaviour change rather
  than part of this one.
- **`grep` still does not distinguish "no lines matched" from "lines matched"** in its
  exit status. Recorded since v0.4.x and still true; changing it would alter what `&&` and
  `||` do with a `grep`.

### Documentation

The README's Known Limitations and Roadmap sections were reconciled — for v0.5.2 as well
as this release. The v0.5.2 documentation commit said it retired the pipeline deadlock and
the absence of job control, but touched only this file, so the README went a release
claiming the shell did not use `fork()` yet, that a pipeline could deadlock, and that
there was no `&` or `jobs`. All three had been false since v0.5.2.

## [0.5.2-alpha] - 2026-08-15

The shell runs on `fork`. The pipeline deadlock is gone, and `kill` can be tried by hand
for the first time.

### Fixed

- **`cmd1 | cmd2` no longer deadlocks.** The shell executed the first stage to completion
  with stdout pointing at the pipe, and only then started the second to drain it. Nothing
  was reading while the first stage wrote, so a first stage producing more than the 4 KB
  the pipe holds blocked with no reader and never resumed — taking the shell with it.
  Both stages are forked now and run at once. This is what `fork()` was added for.

  The shell closes both pipe ends before waiting, which is load-bearing rather than
  tidiness: the reader sees end-of-file only when every write end is shut, and the shell
  holds one.
- **A pipe with no readers accepted data.** `pipe_write()` checked for a departed reader
  only inside the buffer-full branch, so as long as there was room the write succeeded and
  reported the byte count — bytes handed to a pipe nobody would ever read, and a caller
  told they had been written. The condition surfaced only once 4 KB had accumulated.

  Unreachable until this release: with the stages run one after the other, the reader was
  always started after the writer had already finished. Concurrency made it the ordinary
  case. The check now runs first, whatever room is left.

  The warning is logged once per pipe rather than once per rejected write. A writer that
  does not check its write results — `printk()` does not — keeps going until its input is
  exhausted, and a line per attempt buried everything else in the log.
- **`cat` with no file argument reads standard input.** It was an error, which left the
  shell with pipes and nothing able to read one: `grep` and `head` both open a file by
  name, and so did this. `a | b` could be parsed, forked and connected, and there was no
  `b` that would take it. Descriptor 0 is never closed on that path — it belongs to
  whoever started the process, and a builtin closing it would leave the shell without
  input.

### Added

- **Background jobs.** A trailing `&` runs the command in a child and returns the prompt
  immediately. Until now there was no way to hold a prompt while another process ran —
  which is why `kill` went five releases without anyone noticing it did nothing: there
  was never a live target and a prompt at the same time.
- **`jobs`** lists what this shell started and has not yet collected, and **`wait`**
  blocks until all of them have finished. Finished jobs are reported above the next
  prompt rather than the moment they report, so a job ending mid-line does not print over
  what is being typed.

### Changed

- **`wait()` takes the POSIX shape: it returns the pid and writes the status through a
  pointer.** It shipped in v0.5.0 returning the status directly, which is not enough for
  the first thing that needed it — a shell forking two pipeline stages gets two statuses
  back and has to know which is which, because the pipeline's own status is the last
  stage's. The first real consumer is where an API of this kind gets to be wrong, so it
  was changed while there was exactly one.

  A non-zero third argument asks it not to block. The three answers are distinct: a pid
  means a child reported, zero means children exist but none has, and `E_CHILD` means
  there are none at all. Collapsing the middle two would leave a shell either blocking on
  a running job or forgetting one it still has.

  Delivery changed with it. `exec()` still has its status written straight into its saved
  frame — it returns the status itself and cannot re-run without launching the program a
  second time. `wait()` has to write into the caller's memory, which `reap_task()` cannot
  reach from another address space, so its status is parked and the syscall is restarted
  to collect it with the right directory live. The two are told apart by a new wait
  reason.

## [0.5.1-alpha] - 2026-08-15

No kernel code changes. 503 assertions, unaltered — the point of this release is that
getting to them stops costing two minutes.

### Fixed

- **Test and production objects no longer share a tree.** They are compiled from the same
  sources with different flags — test builds carry `-DPBKDF2_DEV_ITERATIONS` — and make
  cannot see a flag change: it compares timestamps, and a differently-compiled object of
  the same age looks current. The only defence was deleting every object before each test
  run, which is why `make clean` was mandatory and why a full rebuild was the price of
  running the suite at all.

  Worse than slow, it was a live hazard in the other direction: a release image built
  without `make clean` first linked whatever the last test run had left behind, and
  inherited its reduced iteration count. The Makefile carried a warning saying exactly
  that, and named this fix.

  Objects now go to `build/<flavour>/`, mirroring the source tree — `prod`, `test` and
  `dev`, the last for `make run-dev`, which had the same problem and was reducing PBKDF2
  cost directly into the production tree. `make` and `make test_kernel` can now be run in
  any order, and a one-file change recompiles one file.
- **`lib/libc.a` was shared across all three flavours too.** No flag that currently
  differs reaches libft, so nothing was wrong today — but one archive serving builds
  compiled differently is the hazard this release exists to remove, and it would have
  been found the hard way the first time that stopped being true. It builds into the
  per-flavour tree with everything else.
- **libft was never rebuilt when a header changed.** `lib/Makefile` compiled with the
  `-MMD -MP` the parent exports, generated a `.d` file for every object, and then never
  read them. A change to a header under `include/` rebuilt every kernel object that used
  it and left libft's alone, so the archive could carry objects compiled against a
  version of a header that no longer existed — the kind of mismatch that surfaces as a
  struct with the wrong layout, a long way from the change that caused it. The
  dependency files are included now.

### Added

- **`make test_kernel MODULE=<name>` runs one module instead of all of them.** Measured
  on the development machine, the build split took a no-change test run from 2m40s to
  1m44s — and the remainder is almost entirely QEMU, which no build change can touch. The
  host is an ARM laptop emulating x86, so the suite runs inside an emulator inside an
  emulator; the only way further down is to run less.

  The module list became a table so it can be searched as well as walked, and the order
  is still the order, so a full run is unaffected. `MODULE=ring3` runs the user-mode
  payload alone. An unknown name prints the available ones and fails, rather than
  executing nothing and reporting a pass — which is what a typo would otherwise look
  like. CI passes no `MODULE` and never will: a filtered run proves one module, not the
  tree.

### Changed

- `make clean` removes `build/` in one step rather than enumerating object paths, so a
  file added to the build no longer has to be remembered in two places. It also sweeps
  the objects left at the old in-source locations — a tree built before this release has
  around 180 of them and the new `rm -rf build` reaches none — plus `qemu.log`, the host
  SAST binary and the Python bytecode the mkfs test writes. The old paths are derived
  from the source lists rather than found with a wildcard, so `clean` names what it
  deletes instead of sweeping for anything that looks like an object.

  `.vscode/` and `compile_commands.json` are deliberately left alone. They are editor
  state, not build output, and deleting the compilation database would silently break
  code navigation until someone thought to run `bear -- make` again.
- The `lib` sub-make runs with `--no-print-directory`. It is still invoked on every
  build, deliberately: making that conditional on `lib/*.c` would skip it when only a
  header had changed, and the sub-make is the only thing that knows its own
  dependencies. With the dependency files now read it does nothing, and says nothing,
  unless there is something to do.
- `kernel_log.txt` is ignored. `make run` writes QEMU's serial output there and it has
  been showing up as untracked ever since.

## [0.5.0-alpha] - 2026-08-15

A process can be made from a process. 503 assertions, 0 failures, up from 490.

Every process in this system has so far come from a file: `exec` builds an address space,
fills it from an ELF image, and blocks the caller until the result exits. That is enough
to run a program and not enough to run two — which is why the shell executes `cmd1 | cmd2`
one stage at a time and deadlocks when the first stage outgrows the pipe buffer.

**Scope is the kernel and its tests.** The shell still runs everything through `exec`.
Moving it onto `fork` is the next release, deliberately separate: building the shell on
an unverified `fork` would mean testing two unknowns at once.

### Added

- **`fork()` (syscall 53).** The child gets a private copy of the parent's user pages,
  its open descriptors, its working directory, uid, signal handlers, priority and FPU
  state, and returns from the call as if it had made it itself — 0 in the child, the
  child's pid in the parent. Everything that can fail happens before the child becomes
  visible to the scheduler, because a task already on the run list cannot be un-created,
  only reaped.
- **`wait()` (syscall 54)**, with a fixed table of parked exit statuses. `exec` never
  needed one: it blocks the caller before the child can run, so a parent is always
  already waiting by the time `reap_task()` delivers. A forked child exits whenever it
  likes, and a status dropped at that point is one `wait()` could never return. Both
  orders now work — the parent arriving first, and the child finishing first. A parent
  with no children left gets `E_CHILD` rather than a block that nothing would end.
- **`copy_user_space()`**, the half of `fork` that copies memory. Two passes, because no
  single directory can see both sides: with the parent's directory live every source page
  is readable at its own address and is copied into a fresh frame through
  `TEMP_MAP_VADDR`, then the clone is loaded into CR3 and the recorded pages are installed
  with `map_page()` — which already builds intermediate tables, sets U/S bits and rejects
  conflicts. Hand-rolling that would have been a second implementation of it.
- `tests/kernel/test_fork.c` and a Ring 3 `fork`/`wait` section in the test payload, 24
  assertions between them. The ones that matter are negative: a `fork` that shared frames
  instead of copying them would pass every content check and fail later, as two processes
  overwriting each other or as a double free at teardown.

### Changed

- **`inherit_fd_table()` is shared between `exec` and `fork`.** It was inline in the ELF
  loader; both callers need the same reference-counted copy, and a descriptor whose
  refcount is not taken means the first of the two tasks to exit destroys the pipe or
  commits and frees the file out from under the other. Standard descriptors are still
  defaulted in the loader alone — a fresh image needs them opened, a fork inherits the
  parent's table verbatim, closed entries included.

### Security

- **`auth_fail_ticks` is inherited by a forked child.** It is the cooldown `sys_auth()`
  imposes after a failed password attempt. A child starting with it clear would let a
  caller fork its way out of the delay and keep guessing at full speed — the copy is a
  rate limit, not context, and it is the one PCB field here that is carried over for a
  reason that is not continuity.

### Known issues

Copies are eager rather than copy-on-write. A child duplicates every page its parent had
mapped at the moment of the call, which is correct and more expensive than it needs to be.
COW requires reference counting on physical frames, and `cleanup_process_memory()` frees
every user frame it finds unconditionally — a shared frame would be released twice. That
is a change to the teardown path and a piece of work in its own right.

The pipeline deadlock is unchanged, because the shell has not moved yet. So is everything
else on the shell side: `rm` on a directory orphans its contents, every `~` expands
through one shared buffer, `grep` reads only the first 511 bytes, and `/bin/rm`,
`/bin/mv` and `/bin/kill` stay shadowed by builtins.

## [0.4.6-alpha] - 2026-08-15

Housekeeping before `fork`. No kernel code changes and no assertion count change — this
is entirely about the build environment and what it tells you when it breaks.

### Fixed

- **`make fuzz` now says why it cannot run instead of dying unreadably.** libFuzzer's
  runtime computes the hamming distance between compared values with the `POPCNT`
  instruction. A CPU that does not implement it raises `SIGILL` inside
  `__sanitizer_cov_trace_const_cmp8` — before any code in this project runs — and
  libFuzzer's own handler reports that as `deadly signal` without ever naming the signal.
  The stack trace points at the fuzz harness, which is the one place the fault is not.

  This is reachable on an emulated x86 host: QEMU's default `qemu64` CPU model omits
  `POPCNT`, so developing on Apple Silicon hits it unless the VM is started with
  `-cpu max`. Real hardware and the CI runners are unaffected, which is why it took a
  host change to surface at all. The target now checks `/proc/cpuinfo` first and prints
  the cause and the fix.

### Changed

- **CI runs on `ubuntu-24.04` rather than `ubuntu-latest`.** The floating label moves onto
  the next LTS on GitHub's schedule, and that swaps the toolchain under a tree nobody
  touched. This project builds with `-Wall -Wextra`, where a major GCC or Clang bump
  reliably surfaces new diagnostics — worth running deliberately, not discovering from a
  red build on an unrelated pull request.

### Documentation

- The `POPCNT` requirement for emulated hosts, in README's Requirements section, with the
  one-line check that confirms it.
- A note that the build host may be 64-bit — `gcc-multilib` is what makes that work and is
  what CI has always used, but the README never said so outright.
- The packages a minimal Debian netinst leaves out that the build needs: `make`, `git`,
  and `xxd`, which the ELF embedding step calls.

## [0.4.5-alpha] - 2026-08-15

Process lifecycle groundwork for `fork`/`wait`, and the bug that groundwork turns out to
fix. Nothing here is new functionality — it is the ability to end a task without being
that task, which the kernel simply did not have.

`exit_current_process()` welded three jobs into one body: release the task's resources,
publish its status to a parent blocked in `wait()`, and switch away from it. `wait()`
needs the first two without the third, so the split was going to happen inside v0.5.0
regardless. Doing it here means the `fork` patch carries one unknown instead of two, and
it means the split arrives with its own tests.

### Fixed

- **`kill` did nothing to a process that had not registered a handler.** `send_user_signal()`
  set a pending bit, and `check_and_deliver_signals()` cleared that bit again with no
  handler to hand it to — so a signal to a process that had never called `signal()` was
  recorded and dropped. Every process is such a process by default, which made `kill(1)`
  a no-op against exactly the runaway task a user needs it for. `SIG_KILL` and `SIG_TERM`
  now terminate a target that has not handled them, with an exit status of `128 + signal`
  — the same encoding the page fault handler already used for `SIGSEGV`.

  A target that is not the running task is reaped where the signal is sent. That is sound
  because the kernel is not preemptible: any task other than the current one is parked at
  a syscall or interrupt boundary with its frame saved in its PCB and nothing live on its
  kernel stack, so there is no context to unwind.
- **`create_process()` left four PCB fields holding whatever the kernel heap last put
  there** — `cmd_args`, `fpu_state`, `signal_saved_regs` and the mailbox. Nothing read
  them before writing them, so it never showed. It would have showed in v0.5.0: `fork()`
  copies a PCB as a whole, and the child would have inherited heap garbage rather than
  its parent's state. The PCB is zeroed at allocation now, which also subsumes the two
  hand-written loops that used to clear the kernel stack and the register frame.
- **A pid in use could be handed out a second time.** `next_pid` only moved forward and
  reset to 2 on overflow, with nothing checking what it landed on. `kill()`, the
  foreground bookkeeping and the parent search all identify a task by pid and stop at the
  first match, so two tasks sharing a number would send signals and exit statuses to
  whichever sat earlier in the list. Two billion `exec()` calls away in practice — but
  `fork()` is what makes pids cheap enough to spend, and the check belongs in the function
  `fork()` mirrors. Zombies are scanned alongside live tasks: a zombie still carries the
  pid its parent has yet to be told about.
- **A mutex held by a killed task is released.** `mutex_unlock()` identifies the owner as
  `current_task`, which was the same thing while a task's locks could only be dropped by
  its own exit. Reached from `kill()` it is not: the ownership test would be made against
  the killer, fail quietly, and strand the lock on a task that no longer exists — and
  nothing revisits a mutex afterwards, so every later waiter would block for the rest of
  the boot. `mutex_release_owned_by()` takes the owner as an argument; `mutex_unlock()` is
  now the guarded entry point that passes `current_task` to it.
- **Killing a background task no longer takes the terminal from the shell.**
  `foreground_task` was reassigned on every exit, which was indistinguishable from the
  correct rule while the only way to die was to be the running — and therefore foreground
  — task. Now the terminal moves when its holder dies, or when a shell blocked on the
  dying task wakes to take it back.

### Added

- `reap_task()` — everything a task's death entails except leaving it. The address space
  is still not freed there; the zombie reaper in `schedule()` does that once another
  task's directory is live.
- `apply_default_signal_action()`, called from exactly one place: the end of
  `syscall_handler()`, after the syscall bookkeeping is closed out. A task that signalled
  itself fatally cannot be reaped at the point the signal is sent — that code is running
  on its own kernel stack — so it is terminated on the way back out to user mode.

  Deliberately *not* folded into `check_and_deliver_signals()`, which is also called from
  the tail of `schedule()`: terminating a task from there would re-enter `schedule()`
  through `exit_current_process()`. For the same reason `check_and_deliver_signals()` now
  leaves an unhandled fatal signal pending instead of clearing it.
- `SIG_KILL` and `SIG_TERM` in `signal.h`. Both numbers were already in use — `kill(1)`
  sent a bare `9` with a comment explaining what it meant — but nothing named them.
- `tests/kernel/test_reap.c`, 31 assertions. Victims are given a real cloned address
  space rather than the fabricated `cr3` the other scheduler tests use: the zombie reaper
  loads that directory into CR3, and a made-up value there is a triple fault rather than
  a failed assertion.
- `tests/user/ktest_signal.c`, a Ring 3 payload that sends itself `SIG_KILL`, with the
  parent asserting the status comes back as 137. The self-signalled path runs only in
  `apply_default_signal_action()`, which is reached only on the way out of
  `syscall_handler()` — and the kernel-mode modules run against a synthetic task that
  never returns through it, so nothing there could cover the half of the default action
  that made restructuring `check_and_deliver_signals()` necessary in the first place.
  Same arrangement `/bin/ktest_crash` already has for the page-fault path, and embedded
  in the test image only.

### Security

- **The idle task is not reapable.** A working `kill()` made it reachable for the first
  time, and `schedule()` reaches the idle task through a named pointer rather than through
  the run list — so reaping it would have left that pointer aimed at memory the zombie
  reaper had already freed, giving an unprivileged `kill 0` a use-after-free on the way
  into a panic. Refused in `reap_task()`, so every caller is covered.

### Known issues

The pipeline deadlock is unchanged and still reachable: the shell runs `cmd1 | cmd2`
sequentially, so a first stage producing more than 4 KB blocks with no reader. `fork` is
the fix, in v0.5.0. `rm <dir>` still orphans its contents, `~` still expands through one
shared buffer, `grep` still reads only the first 511 bytes of a file, and `/bin/rm`,
`/bin/mv` and `/bin/kill` are still shadowed by builtins — all shell-side, and all
deliberately left for after `fork` lands, since the shell is rewritten for concurrent
pipelines then anyway.

## [0.4.4-alpha] - 2026-08-14

Header tidy-up before `fork`/`wait`. No behaviour change: the 445 assertions pass
unaltered, which is the point — the compiler is this release's test.

All 43 headers were audited. The good news first: **no circular includes and no guard
macro collisions.** The header graph is a clean DAG. The problems were in declarations.

### Fixed

- **`timer_ticks` was declared without `volatile`.** `arch/x86/cpu/timer.c` defines it
  `volatile` and increments it from IRQ0; `rtc.h` declared it plain. The tree builds at
  `-O2`, so a loop waiting on the counter through that declaration could have had the
  load hoisted out of it and spun forever. Nothing reads it that way today — every
  caller goes through `timer_get_ticks()` — so this was a loaded gun rather than a live
  miscompile, and it is the same defect class as the historical
  `schedule_kernel_timer()` declared twice with disagreeing signatures.

  It survived because **`timer.c` included none of the headers that declare what it
  defines**, so no definition was ever compared against its declaration. That is fixed
  alongside it: the file now includes `rtc.h`, `isr.h` and `signal.h`.
- **The last three lines of `rtc.h` sat outside the include guard**, after the `#endif`.
  Repeated `extern` declarations are legal so nothing broke, but the first typedef or
  inline added there would have broken every translation unit that includes it, at once.
- **Four ELF blob lengths were declared `const uint32_t` where the other eleven are
  `unsigned int`** — and `xxd -i`, which generates all fifteen, emits `unsigned int`. So
  four disagreed with their own definitions. Incompatible types across translation units
  is undefined behaviour the linker cannot catch.
- **`init_elf.h` used `uint8_t` with no `#include` at all.** It compiled only because its
  one consumer includes `kernel.h` on the line above; swapping those two lines broke the
  build.
- **Host tests were compiled with `-I./include`, which made `<stdio.h>` resolve to the
  kernel's own header** rather than the host's. Measured both ways: with `-I` a bare
  `printf` call fails to compile; with `-iquote` it reaches the host libc. The rules use
  `-iquote` now, so quoted includes still find the kernel headers and angled ones do not.
  A dead `-I./crypto` went with it — there are no headers in that directory.

  The three host tests already declared `printf` by hand and `test_crypto.c` carried a
  comment saying system headers had been removed, while still including `<stdio.h>`. The
  include is gone and the hand-written declaration is what remains, which is what the
  comment always claimed: this project ships no third-party library, and a host test
  borrows exactly one symbol to print a result.

### Removed

- Six declarations of things that do not exist: `init_signals()` (renamed to
  `init_kernel_timers()` and the declaration left behind), `auth_fail_ticks[16]` (a
  global-era leftover — it is a per-process PCB field now), `errno` (the kernel returns
  negative `E_*` codes and never had one), `__bss_end` (the linker script provides
  `_bss_end`, with one underscore), and the unused `signal_t` type.
- Three duplicate declarations: `register_kernel_timer` and `process_pending_kernel_timers`
  (both owned by `signal.h`), and `init_elf`/`init_elf_len` (owned by `init_elf.h`).
- `src/init_elf.h` — empty, unguarded, and not even on the include path.
- Two unused header includes (`pipe.h` → `registers.h`, `crypto.h` → `arch.h`) and 20
  unused includes across the kernel sources, each verified symbol by symbol.

### Changed

- The 49 assembly interrupt stubs are declared together in `isr.h`. Fourteen were there
  and thirty-five in `idt.c`, in two blocks a refactor script had appended at different
  times — exactly complementary, which is the giveaway that nobody chose the split.

### Deliberately not done

`kernel.h` is still a god header: 23 includes for symbols it does not use itself, pulled
in by 11 files, several of them only as a route to `stdio.h`. Splitting it ripples
through every consumer and does not belong in a patch. The same goes for moving
`fs_max_sectors` out of `bcache.h`, moving `klog_write_char`/`dump_klog` into `klog.h`,
and adding the ~20 direct includes that would let the load-bearing chains
(`libft.h` → `kheap.h`, `stdio.h` → `tty.h`, `serial.h` → `io.h`) be trimmed. Those have
an ordering dependency: the direct includes must land before the chains are cut, or the
build breaks.

## [0.4.3-alpha] - 2026-08-14

The kernel can write to a file through a descriptor. Two defects recorded in v0.4.2
came from its absence, and both close here.

### Added

- **`write()` on a regular file descriptor.** `sys_write` handled the console, pipes
  and `/dev` nodes; a regular file fell through to `E_BADF`. That is why `/bin/cp`
  produced an empty destination — it opened, read, wrote and closed correctly, and the
  kernel discarded every write — and why the shell's `>` could never be connected to
  anything.
- **`open()` honours its mode argument.** It was read from nowhere and every descriptor
  was marked read-only, so even `/bin/cp` passing `O_WRONLY` had no effect. Opening for
  writing truncates.
- **Output redirection in the shell.** `cmd > file` creates the target if it does not
  exist, empties it if it does, and sends the command's standard output there.

### How writing works, and why it is narrow

Writes are buffered in the kernel and committed as the file's entire new contents when
the **last** descriptor referring to it closes. That is not a shortcut taken for
convenience: under `SEC_LEVEL_CRYPTO_ENFORCED`, which is the default, a stored file is a
single AES-CBC blob with an HMAC over its whole plaintext, so adding one byte at the end
means re-encrypting and re-authenticating all of it. The VFS has no streaming write
primitive either — `fs_atomic_update()` replaces a whole file and is the only way in.

What follows from that, and is documented rather than half-emulated:

- No appending (`>>`), no writing into the middle of a file, no seeking during a write.
- A file written this way is capped at 64 KB, because the buffer is held in the kernel
  heap until the commit.
- `close()` returns the commit's result. A full disk or a destroyed master key surfaces
  there and nowhere else, so a caller that discards it turns a failure into silent data
  loss — the same shape as the `cp` defect this release fixes.

The commit is tied to the last reference rather than to every `close()`, because `dup2()`
can point several descriptors at one open file; committing on each would publish a
partial buffer. A process that exits still holding a written file commits it too.

### Tests

Test coverage: 411 → 445 assertions.

The Ring 3 half asserts what the semantics actually are: the file on disk is unchanged
while a descriptor is open, `close()` commits it, the bytes survive the encrypt/decrypt
round trip, opening for writing truncates, a read-only descriptor refuses writes, and —
the assertion the design turns on — closing one of two duplicated descriptors does not
commit while closing the second does. It finishes by running `/bin/cp` through `exec`
and checking the copy is the same size as the original, because the defect that made
v0.3.1 necessary lived in argument handling rather than in any syscall.

The size cap is checked from the kernel side instead: reaching it from user space takes
256 syscalls and then commits a 64 KB file, while calling `fs_write_buffered()` directly
costs nothing because the bound is checked before anything is allocated.

### Known and still not fixed

- **The pipeline deadlock is now reachable.** v0.4.2 recorded it as latent because
  nothing could produce more than 4 KB into a pipe. A program can now write that much,
  and the shell runs the two stages of `cmd1 | cmd2` sequentially — so a first stage
  that emits more than the 4 KB pipe buffer will block with no reader running. The real
  fix is `fork` (v0.5.0).
- `>` and `|` cannot be combined; the parser takes whichever it meets first.
- `kill` still has no default action for a process with no handler registered.
- `rm <directory>` orphans the directory's contents.
- Every `~` in one command expands into the same shared buffer.

## [0.4.2-alpha] - 2026-08-14

A stability patch. No new features: the whole v0.4.x tree was audited before starting
work on `fork`/`wait`, and this ships what that audit found. Eight of the defects
below are reachable from an ordinary unprivileged prompt, and three of them hang or
halt the machine.

### Fixed — kernel

- **A segfaulting program left its parent blocked forever and leaked its address
  space.** The page-fault handler's entire teardown was `state = TASK_DEAD` followed
  by a reschedule, so `exit_current_process()` never ran: descriptors were not
  released, a parent waiting on `WAIT_CHILD` was never woken, and the task was neither
  unlinked from the run list nor placed on the zombie list — so the reaper never freed
  its `process_t`, page directory, page tables, user stacks or descriptor table. In
  practice a user program with a null-pointer bug left the shell that started it parked
  on `WAIT_CHILD` with no console. It now exits through the same path `exit()` uses,
  with status 139 (128 + SIGSEGV).
- **Closing a pipe never woke the process blocked on it.** `wakeup_tasks(WAIT_IPC)`
  was called only when data moved, so a reader waiting on an empty pipe whose last
  writer went away never received the EOF the code was ready to give it, and a writer
  parked on a full pipe whose reader disappeared waited for the rest of the boot. All
  three release sites — `close`, `dup2` and process exit — now wake waiters.
- **`open()` dereferenced an unchecked `kmalloc()`.** Exhausting the kernel heap and
  then opening any existing file turned a NULL return into a supervisor write to
  address 0 — a kernel page fault with no fixup, which parks the CPU. Any unprivileged
  process could halt the machine.
- **`create_process()` published a runnable task with a NULL descriptor table.** The
  allocation failure skipped only the initialisation loop; the task was linked in and
  scheduled anyway, and its first `read`/`write`/`open` indexed a NULL table from
  Ring 0. It now frees the task and reports `E_NOMEM`, matching how the `process_t`
  allocation directly above it was already handled.
- **`fs_delete()` walked the FAT chain with no bound.** `file_allocation_table` holds
  4096 entries and the directory table is loaded from disk unvalidated, so a crafted or
  corrupt image gave `rm` a four-byte kernel write at an offset the image chose. Every
  other FAT walk in the file already had this check.
- **`dup2()` did not reference-count files.** Only pipes were counted, so
  `fd = open(f); dup2(fd, 5); close(fd)` freed the `vfs_file_t` while fd 5 still
  pointed at it: a use-after-free on the next read and a double free on the next close.
  Overwriting a file descriptor also leaked its old target. Both directions now behave
  the way the ELF loader's inheritance path already did.
- **The PIC interrupt masks were never programmed.** `pic_remap()` read the firmware's
  masks and wrote them back verbatim, and these were the only writes to the PIC data
  ports in the tree. It works under GRUB and QEMU only because that firmware leaves the
  lines open; a loader that masked them would leave IRQ0 and IRQ1 dead with handlers
  installed for both, hanging the first boot in the keyboard wait. The timer, keyboard,
  cascade and ATA lines are now unmasked explicitly.

### Fixed — shell and tools

- **The shell overran its own stack on ordinary input.** The tokenizer filled a
  32-entry argument array with no bound while the input line allowed about 127 tokens;
  33 short words were enough to corrupt `main`'s frame, and the pass that follows then
  read those slots back as pointers. Tab completion had the same shape twice more: the
  copy loops were clamped but the NUL terminators were not, so a long word or a long
  filename wrote past a 128- and a 64-byte buffer. The argument join and `~` expansion
  were likewise unbounded — and `~` expands to `$HOME`, so a short line could produce
  kilobytes.
- **Thirteen `/bin` tools exited 0 on every error path**, and eighteen shell builtins
  never set the exit status at all — so `rm /nope && echo GONE` printed `GONE`. v0.4.1
  connected the status chain but only `/bin/stat` was ever taught to use it. `cp`,
  `grep`, `head`, `kill`, `mv`, `rm` and `touch` now report failure, and every builtin
  sets a status.
- **`kill` parsed its arguments as hexadecimal**, so `kill 10 9` signalled PID 16, and
  a non-numeric argument silently became PID 0. It parses decimal and rejects junk.
  `/bin/kill` accepted zero digits and sent SIGKILL to PID 0 regardless.
- **`cp` reported success while producing an empty file.** The kernel has no write path
  for a regular file descriptor — `sys_write` handles the console, pipes and devices
  only — so every write returned `E_BADF` and the result was discarded. This does not
  make `cp` work; it turns silent data loss into a visible error. The missing capability
  is v0.4.3.
- **`su` failed silently** on a wrong password, leaving a fresh prompt and no clue.
- **`init` never detected a failed `exec`.** It tested for `-1`, which `exec` never
  returns — a missing `/bin/sh` is `E_NOENT`. PID 1 exited without a diagnostic on a
  broken disk. It now reports and parks rather than printing "System halted." and then
  falling through to exit.

### Documentation

Corrected against the code: the syscall count (the README said both 45 and 50), the
QEMU command's ISO name, the FPU description (eager, not lazy), the kernel log's disk
persistence (`/var/log` is created but nothing is written to it), the shell command
list (`export` takes two words, `su` takes none, `ls` takes none, `echo` and `clear`
are not builtins, twelve builtins were missing) and the supported-version table.

Output redirection is now documented as **not implemented** rather than as a working
feature. It is parsed and discarded, and it cannot work until the kernel can write to
a regular file through a descriptor.

### Tests

The crash-teardown path had no coverage because nothing in the image could produce a
user-mode page fault — every tool exits cleanly and the syscalls validate their
arguments rather than faulting. `tests/user/ktest_crash.c` is a Ring 3 program whose
only job is to write to address 0; it is embedded in the test kernel only and never
reaches the production image.

The Ring 3 payload now execs it and asserts the parent is woken with status 139, then
runs ten more crashes and compares the physical allocator's free-memory figure across
them. The second half matters as much as the first: the parent surviving does not prove
the dead task was reclaimed, and the leak was the larger half of the defect. If either
regresses the run hangs and hits the QEMU timeout rather than passing quietly, which is
the honest failure mode — a hang is the actual symptom.

### Known and deliberately not fixed here

- `kill` has no default action: a signal sent to a process with no handler registered
  for it does nothing, so a runaway process cannot be terminated. Terminating a task
  that is not the current one needs a safe path that does not exist yet.
- Writing to a file through a descriptor, and therefore `cp` and `>`.
- `rm <directory>` orphans the directory's contents.
- Every `~` in one command expands into the same shared buffer.
- Pipelines of more than two stages, and `|` combined with `>`, are mis-parsed.

## [0.4.1-alpha] - 2026-08-14

### Fixed

- **Exit statuses were discarded, so `&&` and `||` decided nothing.** `exit()` never
  read its argument, no field anywhere held a status, and `exec()` reported `E_OK` as
  soon as the child had *started*. The shell then recorded success unconditionally.
  The result was a shell whose conditional operators were decorative:

  ```
  # stat /no_such_file || echo FAILED
  stat: cannot stat '/no_such_file'
  # stat /no_such_file && echo CHAINED
  stat: cannot stat '/no_such_file'
  CHAINED
  ```

  `||` never fired and `&&` always did, whatever the command had done.

  All four links are now connected: `exit()` records its argument in the PCB, masked
  to the low 8 bits; `exit_current_process()` writes it into the waiting parent's
  saved frame; `exec()` returns it instead of `E_OK`; and the shell uses it. A failure
  to start is still a negative errno, and an exit status is 0-255, so the two cannot
  be confused.

  This needed no `fork()`. `exec()` already blocks the caller on `WAIT_CHILD` until
  the child finishes, which is functionally a `wait()` — the only thing missing was
  carrying the number back. The same plumbing is what a real `wait()` will use.

  Nothing caught it because every assertion checked that `exec()` succeeded, and by
  the old contract it always did.

- **`/bin/stat` reported a usage error as success.** Invoked with no argument it
  printed `stat: no file given` and exited 0. Harmless while statuses went nowhere,
  and wrong the moment they started arriving — `stat && echo CHAINED` printed
  `CHAINED` without a file having been named. Found by the first run of the fix
  above, which is the point of connecting the chain at all.

## [0.4.0-alpha] - 2026-08-14

Adds the syscalls a program needs to ask about things rather than only do them:
`stat`, `fstat`, `lseek`, `getpid` and `sleep`. Until now a program could open a file
but not learn its size or whether it was a directory, could read forwards but never
reposition, could not learn its own pid, and had no way to wait for a duration at all.

Two of the five needed groundwork. `stat` has to report the size a `read()` will
return, and that is not the size the directory table records — with encryption on by
default the stored form carries an IV, a header and padding — so sizes come from a new
VFS helper that reads the real length out of the file's header. And `sleep` is the
first production use of `WAIT_TIMER`, which had been defined since the beginning and
appeared in exactly one test.

Test coverage: 345 → 403 assertions.

### Added

- **`stat` (48) and `fstat` (49).** Both fill an `esd_stat_t` (`include/stat.h`),
  shared verbatim between the kernel and user space. `fstat` refuses pipes, the
  console and `/dev` nodes rather than answering for them: a device descriptor stores
  a device table *index* in the field a file descriptor stores a pointer in, and that
  overload is what once made a stale comparison in `open()` an indirect call through
  `dev_table[-2]`. There is deliberately no `st_mtime` — the on-disk entry has no
  timestamps and the RTC is not wired to the VFS, so one would have to be invented.
- **`lseek` (50)**, operating on `vfs_file_t.current_offset`, which was already the
  cursor both read paths use. `SEEK_END` asks the size helper, so seeking to the end
  of an encrypted file lands on the end of the data rather than inside the padding.
  Pipes and devices report `E_SPIPE`.
- **`getpid` (51)** and **`sleep` (52)**. `sleep` takes milliseconds: `TIMER_HZ` is
  100, so the resolution is 10 ms and a seconds-only call could not reach it. The
  shell's new `sleep` builtin still takes seconds.
- **`fs_size()` in the VFS**, branching on security level the way `fs_read()` does,
  refusal after LOCKDOWN included. For an encrypted file it decrypts one AES block
  rather than the whole file: `fs_create_encrypted()` writes the magic and the
  original length as the first eight bytes of the payload, so both sit in ciphertext
  block 0 and the IV in front of it is all CBC needs to get at them.
- **`/bin/stat`**, which prints the readable size and the on-disk size side by side
  so neither looks wrong on its own.
- **A test-build-only `KT_REPORT_TICKS`** on the existing report protocol, because
  Ring 3 has no clock and a `sleep()` that never blocked would otherwise satisfy every
  assertion a payload could make about it. The timing assertions measure.

### Fixed

- **Kernel timers could be counted down and then never run.**
  `process_pending_kernel_timers()` was called at the very end of `schedule()`, which
  put it behind the `current_task == next_task` early return. Whenever the same task
  was reselected — the ordinary case while only the idle task is runnable, with the
  shell blocked on `WAIT_KBD` — an expired timer waited for some unrelated task to
  become runnable before its callback fired. It now runs before the selection passes,
  on the caller's own stack and page directory instead of after CR3 has already been
  switched. The countdown and the drain had no test at all; they have one now.

### Notes

- `st_size` for an encrypted file is **not authenticated**. It is read from the file's
  own header, and the HMAC that would vouch for it covers the plaintext and is checked
  only by `read()` over the whole file. A tampered header can make `stat` report a
  wrong size — the read that follows fails, but the size alone proves nothing. Stated
  in the README and on the function itself rather than left to be discovered.
- `file_descriptor_t.offset` is dead for files; nothing on the read or write path
  consults it. Left in place, since the pipe and device paths do use it.

## [0.3.1-alpha] - 2026-08-13

### Fixed

- **`/bin` tools mis-parsed their arguments.** The shell pasted the current directory
  onto the front of every argument string as an implicit first token, joined with a
  space rather than a slash. In `/home`, `touch notes.txt` produced the argument string
  `/home notes.txt`, and `touch` — which treats its whole argument string as one
  filename — created a file called exactly that. Every tool except `echo` was affected;
  `echo` alone skipped the leading token, so its skip is removed alongside.

  The mechanism dates from when the kernel had no idea where a process was and each
  tool had to be told. v0.3.0 moved the working directory into the PCB but left the
  shell still prepending, so a bare name now resolves correctly on its own.

  Nothing caught this before release: every assertion called the syscalls directly,
  and the defect was in how arguments were assembled before any syscall ran. There is
  now an end-to-end test that execs a real tool with a bare filename and checks the
  file lands in the working directory and not in root.

- **The block cache announced every automatic flush.** Once v0.2.0 gave write-back a
  five-second deadline, the existing INFO line meant a console message every five
  seconds for as long as anything was being written. Flushes performed by the policy —
  the deadline and the high-water mark — now report at DEBUG; an explicit `sync()`,
  reboot or halt still reports at INFO.

## [0.3.0-alpha] - 2026-08-13

Moves the working directory into the kernel. Until now the shell kept it in a userspace
global and passed a directory id into every syscall that touched a path, so a process
chose where its own relative lookups started — and every `/bin` tool passed a hardcoded
0, which meant they all operated on the root directory regardless of where the shell had
`cd`'d to. `cd /home && touch foo` created `/foo`.

Test coverage: 321 → 341 assertions.

### Added

- **`chdir` (46) and `getcwd` (47).** `chdir` validates that the target is a directory
  the caller may read and only then commits, so a failed call leaves the process where
  it was. `getcwd` renders the path by walking the parent chain, bounded so a corrupted
  `parent_id` reports `E_NAMETOOLONG` instead of spinning inside a syscall.
- **`cwd_id` in the PCB**, inherited from the creating process alongside `uid`. `fork()`
  will rely on the same inheritance when it lands.
- Failures are now collected and reprinted as a block just before the tally, with
  `file:line` for kernel-mode assertions and a `ring3` tag for results reported across
  the syscall boundary. A full run prints several hundred lines and hunting the `[FAIL]`
  markers out of that scrollback was its own chore.

### Changed

- **`open`, `create_file`, `rm`, `mv`, `mkdir`, `cat`, `cat_raw`, `get_dir_id` and
  `exec` resolve relative paths against the PCB's working directory.** They no longer
  read a base directory from a register the caller filled in. This is an ABI change: the
  argument that used to carry the directory id is ignored.
- The `/bin` tools are fixed as a side effect, with no changes of their own — they were
  already passing 0 in that slot.
- The shell drops its `current_dir_id` global and roughly 45 lines of hand-rolled path
  canonicalisation — splitting on `/`, pushing and popping tokens to fold `.` and `..` —
  all of which `vfs_resolve_path()` already did. The prompt is refreshed from `getcwd()`
  rather than predicted, so the shell's idea of where it is cannot drift from the
  kernel's.
- `mv` across directories is now refused explicitly. `fs_rename()` renames within one
  directory and has no notion of moving between parents; ignoring the destination
  directory and renaming in place would have been the silent alternative.
- `init` execs an absolute `/bin/sh`. A bare name would resolve from init's own working
  directory — root — and never find the shell.

### Fixed

- **A VFS test had been asserting nothing since the test-mode pointer relaxation was
  removed.** `test_vfs_boundary_and_depth` passed kernel addresses to syscalls: a stack
  array for the directory name, string literals for the out-of-bounds cases.
  `validate_string_pointer` rejects those, so every `sys_mkdir` returned `E_FAULT`, the
  nesting loop broke on its first iteration, and the two assertions that followed passed
  vacuously — a backtrack starting at root has nothing to walk.
- Fixing that exposed a second bug the empty loop had been hiding: the parent walk
  scanned only the first 32 `dir_table` entries, with a comment claiming
  `MAX_FILES_IN_DIR` was 32 when it is 256, so it missed every directory a boot places
  past that index.
- Removed a djb2 checksum in `fs_create_encrypted()` that was computed over the plaintext
  on every encrypted write and never read. The HMAC-SHA256 tag is what detects tampering.

### Security

- User space can no longer nominate the directory a relative lookup starts from. The
  K-10 hardening added validation of the caller-supplied `parent_id`; this removes the
  input instead. The two regression tests that asserted a bogus id was rejected now
  assert that it has no effect, since the rejection they checked for can no longer occur.

## [0.2.0-alpha] - 2026-08-12

A security and correctness release. It is the result of a full read-only audit of the
v0.1.0 tree followed by staged remediation, and it changes the project's honest
maturity claim: before this release **every automated test ran at CPL=0**, so process
isolation, the scheduler and the syscall boundary were written but never exercised.
The suite now crosses into Ring 3 and runs under hardware SMEP/SMAP.

Test coverage went from 268 to 321 assertions, all passing, in both the default-CPU
and RDRAND configurations.

### Added

**Verifiability**
- Ring 3 test payload (`tests/user/ktest_user.c`) loaded as a real encrypted ELF into
  its own address space. It reports results through `SYSCALL_KTEST_REPORT` (200), which
  is serviced only in test builds and answers `-ENOSYS` in production kernels. This is
  the only part of the suite that crosses the privilege boundary.
- Test modules: `test_entropy.c`, `test_lifecycle.c` (address space clone/teardown with
  frame-leak accounting), `test_fault.c` (double-fault infrastructure), `test_elf.c`
  (loader validation). 23 kernel-mode modules total, up from 19.
- `make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"` — reaches the `ENTROPY_OK`
  branch, which `make test_smap` cannot, because `-cpu max` also enables SMAP and the
  kernel-mode modules are skipped under it.

**Entropy (`kernel/security/entropy.c`)**
- Entropy pool: RDRAND first, otherwise interrupt timing jitter from the PIT, keyboard
  and ATA completions. Samples land in a lock-free ring written by interrupt handlers
  and drained with interrupts masked; mixing and extraction are SHA-256 based, with the
  state hashed forward after every extraction for backtracking resistance.
- Per-source **lifetime entropy budgets**, not just per-event caps. The PIT is credited
  zero (it is periodic), ATA is capped at 64 bits for the whole boot, and only the
  keyboard scales. Consequence, and intended: without RDRAND and without someone
  typing, the pool stays at `ENTROPY_WEAK` and says so.
- `entropy_get_stats()` reports observed jitter as statistics only — no raw sample
  leaves the pool — so the suite can measure what the hardware really produced.

**Durability**
- `SYSCALL_SYNC` (45): writes every dirty block-cache sector to disk. Unprivileged, and
  not gated on the security level.
- Block cache write-back policy with two independent bounds: a volume high-water mark
  (32 of 64 slots) enforced inside `bcache_write_sector()`, and a 5-second deadline
  evaluated from `sys_yield()`. `bcache_flush_is_due()` is side-effect free so the
  policy can be asserted without performing I/O.

**Fault handling**
- Double-fault task gate with its own TSS and stack, so kernel stack overflow is caught
  instead of triple faulting silently. The GDT grew from 8 to 9 entries.

**Devices**
- `/dev/urandom`, alongside a rewritten `/dev/random`. Both are ChaCha20 keyed from the
  entropy pool, re-keyed on output volume, on a time interval, and immediately when the
  pool's verdict improves — so a machine that gains entropy after boot benefits without
  restarting. Each request ratchets the context forward.

**API**
- `TIMER_HZ` in `rtc.h`, replacing a literal and three prose restatements of the PIT rate.
- The ISO is now named `esdumanOS-v<version>.iso`, with the version derived by the
  Makefile from the `OS_VERSION_*` macros in `include/kernel.h` — so a version bump
  touches one file. The kernel binary stays `myos.bin`, because `grub/grub.cfg` names it
  and a mismatch there yields an ISO that fails to boot rather than a build error.
- Incremental SHA-256 (`sha256_init`/`update`/`final`), PBKDF2-HMAC-SHA256
  (`crypto/pbkdf2.c`), standalone ELF validation (`kernel/proc/elf_validate.c`, also
  fuzzed), `trap_frame_is_live()`, `syscall_block_and_restart()`, `fs_dir_exists()`,
  `dev_index_is_valid()`, `kernel_master_key_available()`, `crypto_fs_key_is_usable()`.

### Fixed

- **Arbitrary function pointer call from unprivileged code.** `get_device_idx()` reported
  failure as `E_NOENT` (-2) while `sys_read`/`sys_write` compared against -1. The
  descriptor was handed out carrying `ptr == (uint32_t)-2`, and the first read or write
  on it evaluated `dev_table[-2].read` and called through whatever lay in front of the
  table. Reachable by any process.
- **Every user frame leaked on process exit.** `cleanup_process_memory()` was guarded by
  a CR3 equality check that was always true at the call site, so it never ran. Exit now
  hands the task to a zombie list and a reaper in `schedule()` tears the address space
  down once another directory is live.
- **`clone_page_directory()` returned `E_NOMEM` as a physical address** on failure, so
  the caller loaded `0xFFFFFFF4` into CR3.
- **`clone_page_directory()` marked PD[0..3] present at physical frame 0.**
- **`sys_readdir()` wrote to user memory without `copy_to_user()`**, which panics the
  moment SMAP is enabled.
- **`sys_exec()` wrote `regs->eax` after `sleep_current_task()`**, corrupting the
  *next* task's return value.
- **`parent_id` was taken from user space unvalidated**, and `check_vfs_access()` could
  loop forever walking parent links.
- **`schedule()` could `iret` into a task it had just found unrunnable.** With nothing
  runnable it did `sti; hlt; return`, leaving `*regs` untouched, so a task that had
  blocked carried on as though it never had. There is now an always-runnable idle task.
- **Locks were handed `&current_task->regs`** — a saved copy, not the live interrupt
  frame. Writing through it corrupted the stored context while the real frame went
  untouched. `trap_frame_is_live()` now rejects it.
- **`regs->eip -= 2` for syscall restart** assumed every frame was an `int 0x80` entry.
  Replaced with an explicit restart flag driven by the entry EIP the dispatcher records.
- **Kernel stack raised from 4 KB to 8 KB.** The deepest measured path, `sys_auth()`,
  reaches roughly 2.5–3 KB on its own before interrupts nest on top.
- **`schedule_kernel_timer()` had two contradictory declarations.** `process.h` declared
  `int schedule_kernel_timer(int ticks, int pid)`; `signal.c` defines
  `void schedule_kernel_timer(int timer_id, uint32_t delay_ticks)`. The two disagreed on
  the meaning of *both* parameters and on the return type, and `sys_alarm()` — which
  includes `process.h` — was compiled against the one that matched nothing.
- **`signal.h` declared three functions that existed nowhere**, under their pre-rename
  names (`register_signal`, `schedule_signal`, `signal_tick_handler`). Three translation
  units included it.
- **`sys_alarm()` printed "3 seconds" and scheduled 0.55.**
- **`include/types.h` did not compile under C23.** `typedef _Bool bool` is a syntax error
  there, and GCC 15 defaults to `gnu23`; the build pins no `-std`, so this file decided
  whether the tree compiled at all.
- **`.gitignore`'s blanket `*.bin` swallowed the fuzzer corpus seeds**, dropping inputs
  that had found real bugs while the hash-named seeds beside them stayed tracked.

### Changed

- **CryptoFS IVs are derived, not drawn.**
  `HMAC-SHA256(file key, "esdumanOS-iv-v1" ‖ counter ‖ pool bytes)`. The monotonic
  counter makes the input distinct on every call, so two files cannot share an IV even
  with a dead entropy pool; keying with the file key keeps it unpredictable. **The
  on-disk format is unchanged** and files written by earlier builds still decrypt.
- **Shadow salts are derived** as `SHA-256(pool bytes ‖ username)`, so two accounts
  cannot collide even if the pool were producing a constant.
- Password verification uses PBKDF2-HMAC-SHA256; `hmac_sha256()` no longer allocates
  per iteration, and the iteration count read from `/etc/shadow` is clamped to a sane
  range so a tampered file can neither weaken verification nor stall the kernel.
- `CR0.WP` is enabled: the kernel honours read-only user pages. `map_page()` builds new
  page directory entries from the requested flags rather than always user-accessible.
- SMEP/SMAP are enabled when the CPU reports them, with the CPUID leaf checked properly.
- LOCKDOWN now refuses encrypted VFS access after destroying the master key, instead of
  silently encrypting and decrypting with an all-zero key — which had been quietly
  destroying the filesystem — and it blocks starting new programs.
- The kernel's non-preemptibility is now documented as a deliberate invariant with its
  reasoning recorded next to the guard in `schedule()`, rather than looking like an
  oversight. Making the kernel preemptible requires fixing the frame layout, the
  lock-free bcache/pipe/ATA paths and the global uaccess state first.
- `make test_smap` no longer ends in `|| true`, and CI no longer marks it
  `continue-on-error` — a real SMAP regression now fails the build.

### Removed

- `switch_to_user_mode()` and its two declarations. It built an `iret` frame by hand and
  was called from nowhere; `start_first_task()` is the path tasks actually take.
- `include/shell.h`, `include/ft_printf.h`, `include/string.h` (included by nothing),
  `tools/test_passwd.c`, `tools/inject.py`, and a committed compiler artifact
  (`lib/ft_strstr-c36b9fff.o.tmp`).
- `-I src/libc` from `CFLAGS`; the directory does not exist.
- The fictional `schedule_kernel_timer()` declaration in `process.h`.

### Security

- The disk encryption key is a build-time constant compiled into the kernel. That is
  now stated plainly in the README and `SECURITY.md`: it gives tamper resistance, **not**
  confidentiality at rest.
- Entropy quality is reported honestly rather than assumed. Measured under QEMU TCG, the
  TSC advances in multiples of 1000, so only 4 of the 32 possible values of
  `delta & 31` ever occur — the low bits of every timing delta carry no information on
  that platform. An earlier iteration of this release credited 31106 bits from 15629 ATA
  completions and would have claimed cryptographic quality on a machine with none; the
  per-source budgets exist because that measurement was taken.
- `SECURITY.md`'s known-limitations list was substantially wrong in the *pessimistic*
  direction — it claimed no per-user salt and a non-standard KDF, both of which had
  ceased to be true — and wrong about a `1234` default password that exists nowhere in
  the tree. Corrected.

### Documentation

- Reconciled `README.md` against the code: 45 discrepancies. The build instructions
  named the wrong environment variable (`ESDUMAN_KEY` instead of
  `ESDUMAN_ELF_KEY_HEX`, which must be exactly 64 hex characters), so nobody following
  them could build. The PIT rate was documented as 1000 Hz in three places and is 100.
  Four of nine rows in the resource-limits table were wrong. Eight completed items were
  still listed as open on the roadmap. `make run` was described as opening a QEMU window
  with serial on stdio; it uses `-display curses` and logs to `kernel_log.txt`.
- Same corrections applied to `CONTRIBUTING.md`, which carried the same unbuildable
  `ESDUMAN_KEY` instruction.

## [0.1.0-pre-alpha] - 2026-08-03

### Added
- First public Pre-Alpha release of esdumanOS.
- Multiboot-compliant boot via GRUB with full GDT/IDT/TSS initialization.
- Physical memory manager (bitmap allocator, 128 MB addressable).
- Virtual memory with paging (recursive page directory, per-process address spaces).
- Kernel heap allocator with first-fit, coalescing, and corruption detection.
- Preemptive priority-based scheduler with kernel-mode preemption guard.
- ELF binary loader with PT_LOAD segments and user stack guard page.
- IPC: message passing (8-slot mailbox) and anonymous/named pipes.
- Signal subsystem (32 slots per process + kernel timer signals).
- Lazy FPU save/restore (FXSAVE/FXRSTOR).
- VFS with custom FAT-based file system, CRUD operations, atomic file updates.
- CryptoFS: transparent AES-256-CBC encryption with per-file random IVs.
- Block cache (64-slot LRU with write-through policy).
- DevFS with `/dev/null` and `/dev/random` device nodes.
- ATA/IDE PIO disk driver with IRQ-based waiting.
- PS/2 keyboard driver with US and Turkish layouts.
- VGA text mode with 3 virtual terminals (F1-F3), 80x100 scrollback buffer.
- RTC and serial port drivers.
- 42 system calls via INT 0x80.
- Multi-layered security model (NORMAL → LOCKDOWN → CRYPTO_ENFORCED → IMMUTABLE).
- User authentication via `/etc/shadow` with SHA-256 hashed passwords.
- AES-256 (ECB, CBC, CTR), SHA-256, HMAC, ChaCha20 cryptographic primitives.
- Unix-style shell with 20+ builtins, pipes, redirection, chaining, variable expansion.
- User-space programs: sh, hello, echo, clear, touch, rm, mv, cp, free, whoami, kill, grep, head, date.
- FHS directory layout created at boot (`/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var`).
- 19 kernel self-test modules covering VFS, memory, pipes, security, and more.
- Host-side tests, fuzzing (45 corpus files), and CI pipeline (GitHub Actions).
- Experimental RISC-V 64-bit skeletal support.
- MIT License.

### Fixed
- `kfree` triple fault crash during process exit by deferring active stack unmapping.
- Kernel memory leak in `exit_current_process` by properly reclaiming zombie task memory.
- Boot hangs caused by debug paging identity maps.

### Security
- Replaced hardcoded AES IV with dynamic `/dev/urandom` 16-byte generation.
- Removed plaintext `passwords.txt` from repository.

### Changed
- Modularized `kernel_main` into smaller init subsystems (boot, memory, fs, userspace).
- Adopted semantic versioning starting at v0.1.0-pre-alpha.
