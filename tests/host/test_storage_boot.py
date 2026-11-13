#!/usr/bin/env python3
"""Exercise the production ISO through its real keyboard, login and shell.

Use --kernel to provision a fresh disk through the production first-boot
prompts, or --seed for a disposable production-strength disk with known test
credentials. Every supplied image is copied before QEMU sees it. Reduced-KDF
self-test images are intentionally rejected by a production kernel.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_fs_image import check_image


def terminal_symbols(kernel):
    symbols = {}
    for line in subprocess.check_output(['nm', '-S', '--defined-only', str(kernel)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in ('terminals', 'current_terminal'):
            symbols[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
    # drivers/tty.c: three i386 terminal_state_t objects, 72-byte header and
    # 100 x 80 uint16 cells each. Refuse a changed ABI instead of reading guesses.
    assert symbols['terminals'][1] == 3 * (72 + 100 * 80 * 2)
    assert symbols['current_terminal'][1] == 4
    return symbols


class TerminalTranscript:
    """Follow appended terminal cells, ignoring cursor-only redraws."""
    def __init__(self, wait_for_login=False):
        self.previous = ''
        self.text = ''
        self.wait_for_login = wait_for_login

    def update(self, raw):
        x, y = struct.unpack_from('<II', raw)
        # The shell moves the cursor left and back after every inserted key.
        # A snapshot between those writes still contains the whole input line.
        # Do not truncate it and later replay the restored prompt as new output.
        if x >= 80 or y >= 100:
            return self.text
        rows = [raw[72 + row * 160:72 + (row + 1) * 160:2].decode('ascii', 'replace')
                for row in range(100)]
        # init explicitly clears the boot console before drawing its login
        # screen. Start this transcript after that known screen transition.
        if self.wait_for_login:
            if not any('login: ' in row for row in rows):
                return self.text
            self.wait_for_login = False
        if rows[y][x:].strip() or any(row.strip() for row in rows[y + 1:]):
            return self.text
        screen = '\n'.join(row.rstrip() for row in rows[:y]) + '\n' + rows[y][:x]
        if self.previous.startswith(screen):
            return self.text
        overlap = self.previous
        while not screen.startswith(overlap):
            # At 100 rows the terminal discards complete leading rows. Match
            # the retained suffix instead of replaying its old prompts.
            newline = overlap.find('\n')
            if newline < 0:
                raise RuntimeError('terminal history changed outside append/scroll')
            overlap = overlap[newline + 1:]
            if not overlap:
                raise RuntimeError('terminal history has no retained overlap')
        self.text += screen[len(overlap):]
        self.previous = screen
        return self.text


class QMPChannel:
    """Bounded, framed QMP replies; a delayed fragment is never retransmitted."""
    def __init__(self, connection, timeout=120):
        self.connection = connection
        self.timeout = timeout
        self.buffer = b''
        self.sequence = 0
        greeting = self.receive(time.monotonic() + timeout)
        if 'QMP' not in greeting:
            raise RuntimeError('QMP greeting missing')

    def receive(self, deadline):
        while b'\n' not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('QMP reply deadline exceeded')
            self.connection.settimeout(min(1, remaining))
            try:
                fragment = self.connection.recv(65536)
            except socket.timeout:
                # Unlike a timed-out socket.makefile(), this retains the partial
                # JSON frame and remains usable until the total deadline.
                continue
            if not fragment:
                raise RuntimeError('QMP disconnected before its reply')
            self.buffer += fragment
        line, self.buffer = self.buffer.split(b'\n', 1)
        return json.loads(line)

    def command(self, command, arguments=None):
        self.sequence += 1
        request = {'execute': command, 'id': self.sequence}
        if arguments is not None:
            request['arguments'] = arguments
        deadline = time.monotonic() + self.timeout
        self.connection.settimeout(self.timeout)
        self.connection.sendall(json.dumps(request).encode() + b'\n')
        while True:
            if time.monotonic() >= deadline:
                raise TimeoutError(f'QMP deadline exceeded for {command}')
            message = self.receive(deadline)
            if 'event' in message:
                continue
            if message.get('id') != self.sequence:
                raise RuntimeError('QMP reply does not match the pending request')
            if 'error' in message:
                raise RuntimeError(message['error'])
            if 'return' in message:
                return message['return']
            raise RuntimeError('QMP reply has neither return nor error')


class Guest:
    def __init__(self, work, iso, root, data, firmware, label, symbols, kernel=None,
                 login_password='test'):
        self.log = work / (label + '.serial.log')
        self.screen_log = work / (label + '.terminal.log')
        self.memory = work / (label + '.terminal.bin')
        self.screenshot = work / (label + '.ppm')
        self.symbols = symbols
        self.login_password = login_password
        self.screen_mode = False
        self.terminal = TerminalTranscript(wait_for_login=True)
        qmp = work / (label + '.sock')
        args = ['qemu-system-x86_64' if firmware == 'uefi' else 'qemu-system-i386',
                '-m', '128', '-cdrom', str(iso), '-boot', 'd',
                '-drive', f'format=raw,file={root},if=ide,index=0,media=disk',
                '-device', 'qemu-xhci,id=xhci',
                '-drive', f'format=raw,file={data},if=none,id=data',
                '-device', 'usb-storage,drive=data,bus=xhci.0,port=1',
                '-serial', 'file:' + str(self.log), '-display', 'none', '-no-reboot',
                '-qmp', f'unix:{qmp},server=on,wait=off']
        if kernel:
            args += ['-kernel', str(kernel), '-append', 'mkfs=ata0']
        if firmware == 'uefi':
            variables = work / (label + '.vars.fd')
            shutil.copyfile('/usr/share/OVMF/OVMF_VARS_4M.fd', variables)
            # OVMF needs its own aperture setting; the q35 host-hole property
            # alone still permits a 64-bit xHCI BAR above this kernel's range.
            args += ['-M', 'q35', '-fw_cfg', 'name=opt/ovmf/X-PciMmio64Mb,string=0',
                     '-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
                     '-drive', f'if=pflash,format=raw,file={variables}']
        self.stderr = (work / (label + '.qemu.log')).open('xb')
        self.process = subprocess.Popen(args, stdout=self.stderr, stderr=subprocess.STDOUT)
        self.cursor = 0
        self.sequence = 0
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(15)
        deadline = time.monotonic() + 30
        while not qmp.exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('QEMU did not open its control socket')
            time.sleep(.1)
        self.socket.connect(str(qmp))
        self.channel = QMPChannel(self.socket)
        self.qmp('qmp_capabilities')

    def qmp(self, command, arguments=None):
        return self.channel.command(command, arguments)

    def content(self):
        if self.screen_mode:
            # SYSCALL_WRITE goes to the terminal, not the kernel serial logger.
            # Read the actual terminal cells without calling guest functions or
            # modifying guest RAM. Both BIOS and framebuffer consoles use them.
            address, size = self.symbols['terminals']
            current, _ = self.symbols['current_terminal']
            error = self.qmp('human-monitor-command', {'command-line':
                             f'memsave {current:#x} 4 "{self.memory}"'})
            if error:
                raise RuntimeError(error)
            selected, = struct.unpack('<I', self.memory.read_bytes())
            if selected >= 3:
                raise RuntimeError('invalid current terminal')
            error = self.qmp('human-monitor-command', {'command-line':
                             f'memsave {address + selected * (size // 3):#x} {size // 3} "{self.memory}"'})
            if error:
                raise RuntimeError(error)
            raw = self.memory.read_bytes()
            before = self.terminal.text
            transcript = self.terminal.update(raw)
            if transcript != before:
                self.screen_log.write_text(transcript)
            return transcript
        return self.log.read_text(errors='replace') if self.log.exists() else ''

    def wait(self, text, timeout=900):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f'QEMU exited before {text!r}; see {self.log}')
            output = self.content()
            for failure in ('Controller decodes above 4 GB',
                            'No USB storage device on the bus.',
                            'USB storage was present and none of it came up.'):
                if failure in output:
                    raise RuntimeError(f'USB test topology unavailable: {failure}; see {self.log}')
            found = output.find(text, self.cursor)
            if found >= 0:
                self.cursor = found + len(text)
                return
            for failure in ('The key slot names a work factor outside', 'No mountable filesystem found.',
                            'Passphrase rejected three times', '[PANIC]'):
                if failure in output[self.cursor:]:
                    raise RuntimeError(f'boot refused: {failure}; see {self.log}')
            if self.process.poll() is not None:
                raise RuntimeError(f'QEMU exited before {text!r}; see {self.log}')
            time.sleep(.15)
        raise RuntimeError(f'timeout waiting for {text!r}; see {self.log}')

    def line(self, text):
        keys = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus',
                '$': 'shift-4', '?': 'shift-slash', '\n': 'ret'}
        for char in text + '\n':
            key = keys.get(char, char)
            response = self.qmp('human-monitor-command', {'command-line': 'sendkey ' + key + ' 50'})
            if response:
                raise RuntimeError('sendkey failed: ' + response)
            time.sleep(.13)

    def status(self, expected=0):
        self.sequence += 1
        marker = f'status-{self.sequence}'
        start = self.cursor
        # The shell expands a whole $? token, not a suffix inside a word.
        self.line('echo ' + marker + ' $?')
        self.wait(' # ')
        observed = self.content()[start:self.cursor]
        if '\n' + marker + ' ' + str(expected) + '\n' not in observed:
            raise RuntimeError(f'unexpected command status: {observed!r}')

    def command(self, text, expected=0):
        self.line(text)
        self.wait(' # ')
        self.status(expected)
        print(f'command passed: {text}', flush=True)

    def require_storage(self):
        if '[USBMSC] USB disk usb0: 4096 sectors' not in self.log.read_text(errors='replace'):
            raise RuntimeError(f'expected USB data disk was not registered; see {self.log}')

    def login(self, unlock=True):
        if unlock:
            self.wait('Disk passphrase: ')
            self.line('test')
            self.wait('[OK] Disk unlocked.')
        self.wait('Boot process completed. Dropping to User Mode (Ring 3)...')
        self.require_storage()
        self.screen_mode = True
        self.cursor = 0
        self.wait('login: ')
        if len(self.login_password) == 63:
            self.line('root')
            self.wait('password: ')
            self.line(self.login_password + 'x')
            self.wait('Input exceeds the supported length.')
            self.wait('login: ')
        self.line('root')
        self.wait('password: ')
        self.line(self.login_password)
        self.wait(' # ')
        self.qmp('screendump', {'filename': str(self.screenshot)})
        print('Login completed through the production terminal.', flush=True)

    def mount(self, password='datatest', expected=0):
        self.line('mount usb0')
        self.wait('Disk passphrase: ')
        self.line(password)
        self.wait(' # ')
        self.status(expected)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        self.socket.close()
        self.stderr.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--seed', type=Path)
    parser.add_argument('--expect-seed-version',
                        help='verify an existing seed version and its upgrade probe before refresh')
    parser.add_argument('--kernel', type=Path, required=True,
                        help='matching production kernel, also used for terminal symbols')
    parser.add_argument('--long-password', action='store_true',
                        help='use a 63-character root password ending in a space and reject overflow')
    parser.add_argument('--legacy-kernel', type=Path,
                        help='provision using an unmodified v1.13 production binary before upgrading')
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--refresh', action='store_true', help='back up and reinstall changed image programs')
    parser.add_argument('--output', type=Path, default=Path('build/storage-boot'))
    args = parser.parse_args()
    if args.expect_seed_version and not args.seed:
        parser.error('--expect-seed-version requires --seed')
    if args.legacy_kernel and (args.seed or args.long_password or not args.refresh):
        parser.error('--legacy-kernel needs --refresh and excludes --seed/--long-password')
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.firmware + '-', dir=args.output.resolve()))
    shutil.copyfile(__file__, work / 'harness.py')
    progress = {'completed': []}

    def record(stage):
        progress['completed'].append(stage)
        (work / 'progress.json').write_text(json.dumps(progress, indent=2) + '\n')
        print(f'completed stage: {stage}', flush=True)

    iso, root, data = work / 'release.iso', work / 'root.img', work / 'data.img'
    shutil.copyfile(args.iso, iso)
    kernel = work / 'production.bin'
    shutil.copyfile(args.kernel, kernel)
    symbols = terminal_symbols(kernel)
    password = 'r' * 62 + ' ' if args.long_password else 'test'
    if args.seed:
        shutil.copyfile(args.seed, root)
        check_image(root)
    seed_hash = hashlib.sha256(root.read_bytes()).hexdigest() if args.seed else None
    if not args.seed:
        if not args.kernel:
            parser.error('--kernel is required when --seed is absent')
        with root.open('xb') as new:
            new.truncate(4096 * 512)
    with data.open('xb') as new:
        new.truncate(4096 * 512)
    blank_hash = hashlib.sha256(data.read_bytes()).hexdigest()
    print(f'Evidence directory: {work}', flush=True)
    blank_root = work / 'unprovisioned-root.img'
    with blank_root.open('xb') as new:
        new.truncate(4096 * 512)
    blank = Guest(work, iso, blank_root, data, args.firmware, 'blank', symbols)
    try:
        blank.wait('No mountable filesystem found.')
        blank.require_storage()
    finally:
        blank.close()
    assert hashlib.sha256(blank_root.read_bytes()).hexdigest() == blank_hash
    assert hashlib.sha256(data.read_bytes()).hexdigest() == blank_hash
    print('Default ISO boot refused both blank disks without writes.', flush=True)
    record('blank boot without writes')
    if not args.seed:
        provision_kernel = kernel
        if args.legacy_kernel:
            provision_kernel = work / 'legacy.bin'
            shutil.copyfile(args.legacy_kernel, provision_kernel)
        provision = Guest(work, iso, root, data, 'bios', 'provision', terminal_symbols(provision_kernel),
                          kernel=provision_kernel, login_password=password)
        try:
            if not args.legacy_kernel:
                provision.wait('Set disk passphrase: ')
                provision.line('a' * 64)
                provision.wait('Confirm disk passphrase: ')
                provision.line('a' * 64)
                provision.wait('A passphrase is required, and must be shorter than 64 characters.')
            for prompt in ('Set disk passphrase: ', 'Confirm disk passphrase: ',
                           'Set root password: ', 'Confirm root password: ',
                           'Set esduman password: ', 'Confirm esduman password: '):
                provision.wait(prompt)
                provision.line(password if 'root password' in prompt else 'test')
            provision.login(unlock=False)
            provision.command('write /upgrade-probe upgradeproof14')
            provision.command('sync')
            provision.line('reboot')
            assert provision.process.wait(timeout=60) == 0
        finally:
            provision.close()
        check_image(root)
        shutil.copyfile(root, work / 'provisioned-seed.img')
        (work / 'provision-report.json').write_text(json.dumps({
            'kernel_sha256': hashlib.sha256(provision_kernel.read_bytes()).hexdigest(),
            'seed_sha256': hashlib.sha256(root.read_bytes()).hexdigest(),
            'legacy': bool(args.legacy_kernel), 'metadata': check_image(root),
        }, indent=2) + '\n')
        print('Production first boot completed and seed preserved.', flush=True)
        record('first boot, login, sync and reboot')
    guest = Guest(work, iso, root, data, args.firmware, 'first', symbols, login_password=password)
    try:
        guest.login()
        if args.expect_seed_version:
            guest.line('cat /etc/os-release')
            guest.wait(args.expect_seed_version)
            guest.wait(' # ')
            guest.status()
            guest.line('cat /upgrade-probe')
            guest.wait('upgradeproof14')
            guest.wait(' # ')
            guest.status()
            record('seed version and existing user payload')
        elif args.seed:
            guest.command('write /upgrade-probe upgradeproof14')
        if args.refresh:
            for path in ('/bin/sh', '/bin/mount', '/bin/umount', '/etc/os-release', '/etc/motd', '/init.elf'):
                guest.command('mv ' + path + ' ' + path + '.pre-v114')
            guest.command('sync')
            guest.line('reboot')
            assert guest.process.wait(timeout=60) == 0
            guest.close()
            guest = Guest(work, iso, root, data, args.firmware, 'refreshed', symbols, login_password=password)
            guest.login()
            record('program backup, refresh and login')
        guest.line('cat /upgrade-probe')
        guest.wait('upgradeproof14')
        guest.wait(' # ')
        guest.status()
        guest.command('mount usb0', expected=1)
        assert hashlib.sha256(data.read_bytes()).hexdigest() == blank_hash, 'blank mount wrote to the data disk'
        guest.line('mkfs usb0 4096')
        guest.wait('New disk passphrase: ')
        guest.line('a' * 64)
        guest.wait('Input exceeds the supported length.')
        guest.wait(' # ')
        guest.status(1)
        assert hashlib.sha256(data.read_bytes()).hexdigest() == blank_hash, 'rejected password initialized the disk'
        record('blank mount and overlong mkfs password without writes')
        guest.line('mkfs usb0 4096')
        guest.wait('New disk passphrase: ')
        guest.line('datatest')
        guest.wait('Repeat disk passphrase: ')
        guest.line('datatest')
        guest.wait('Disk initialized. Use mount to attach it.')
        guest.wait(' # ')
        guest.status()
        record('explicit production mkfs')
        guest.mount('wrong', expected=1)
        guest.mount()
        record('wrong password refusal and independent-volume attach')
        guest.command('write /mnt/probe volumeproof14')
        guest.command('write /root-probe rootproof14')
        guest.command('cd /mnt')
        guest.command('umount', expected=1)
        guest.command('cd /')
        guest.command('umount')
        guest.mount()
        guest.line('cat /mnt/probe')
        guest.wait('volumeproof14')
        guest.wait(' # ')
        guest.status()
        record('two-volume I/O, busy CWD, detach and remount')
        guest.command('sync')
        guest.line('reboot')
        assert guest.process.wait(timeout=60) == 0, 'reboot did not reset the machine'
        record('sync and reboot')
    finally:
        guest.close()
    guest = Guest(work, iso, root, data, args.firmware, 'second', symbols, login_password=password)
    try:
        guest.login()
        guest.mount()
        for path, value in [('/mnt/probe', 'volumeproof14'), ('/root-probe', 'rootproof14')]:
            guest.line('cat ' + path)
            guest.wait(value)
            guest.wait(' # ')
            guest.status()
        guest.line('halt')
        assert guest.process.wait(timeout=60) == 0, 'halt did not power off the machine'
        record('reopened payloads and ACPI shutdown')
    finally:
        guest.close()
    report = {'firmware': args.firmware, 'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
              'program_refresh': args.refresh,
              'long_password': args.long_password,
              'seed_sha256': seed_hash,
              'seed_version_verified': args.expect_seed_version,
              'legacy_kernel_sha256': hashlib.sha256((work / 'legacy.bin').read_bytes()).hexdigest()
                                      if args.legacy_kernel else None,
              'root': check_image(root), 'data': check_image(data),
              'verified': ['default blank boot without writes', 'preserved pre-upgrade file',
                           'blank mount without writes', 'explicit mkfs', 'wrong password refusal',
                           'two-volume CLI', 'busy CWD', 'detach and remount',
                           'sync and reboot persistence', 'ACPI shutdown']}
    (work / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
