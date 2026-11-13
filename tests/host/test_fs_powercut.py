#!/usr/bin/env python3
"""Measure v3 power-cut outcomes on newly created QEMU images, never user disks.

Run after make test_build. Metadata failures are recorded as observed durability
limits, not counted as successful recovery. The synced case must reopen with
its complete new payload. Invalid snapshots must be refused without writes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_fs_image import InvalidImage, check_image


CASES = ('create-fat', 'create-dir', 'update-dir', 'delete-dir', 'rename-dir', 'synced')


def run_guest(kernel, image, log_path, module, marker, timeout, provision=False):
    cmdline = 'kernel_pass=selftest:crash-' + module
    if provision:
        cmdline += ' mkfs=ata0'
    args = ['qemu-system-i386', '-kernel', str(kernel), '-append', cmdline,
            '-drive', f'format=raw,file={image},if=ide,index=0,media=disk',
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
            '-serial', 'stdio', '-display', 'none', '-no-reboot']
    deadline = time.monotonic() + timeout
    output = bytearray()
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    killed = False
    with selectors.DefaultSelector() as selector, log_path.open('xb') as log:
        selector.register(process.stdout, selectors.EVENT_READ)
        try:
            while time.monotonic() < deadline:
                for key, _ in selector.select(timeout=min(1, max(0, deadline - time.monotonic()))):
                    chunk = os.read(key.fd, 65536)
                    if chunk:
                        log.write(chunk)
                        log.flush()
                        output.extend(chunk)
                if marker.encode() in output:
                    # The probe parks the CPU after the requested driver write.
                    # SIGKILL avoids QEMU's normal block-device shutdown flush.
                    process.kill()
                    killed = True
                    break
                if process.poll() is not None:
                    break
            if marker.encode() not in output:
                raise RuntimeError(f'{module}: required marker missing; see {log_path}')
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            process.stdout.close()
    return {'marker': marker, 'sigkill_after_marker': killed, 'exit_code': process.returncode}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, default=Path('myos_test.bin'))
    parser.add_argument('--output', type=Path, default=Path('build/powercut'))
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--cases', nargs='+', choices=CASES, default=CASES)
    args = parser.parse_args()
    kernel = args.kernel.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    # Pin this run's executable even if another test rebuilds the source tree.
    saved_kernel = root / 'myos_test.bin'
    shutil.copyfile(kernel, saved_kernel)
    report = {'kernel_sha256': hashlib.sha256(saved_kernel.read_bytes()).hexdigest(), 'cases': []}
    print(f'Evidence directory: {root}', flush=True)
    for case in args.cases:
        image = root / f'{case}.img'
        with image.open('xb') as new:
            new.truncate(4096 * 512)
        item = {'case': case}
        item['cut'] = run_guest(saved_kernel, image, root / f'{case}.log', case,
                                '[CRASH] STOP ' + case, args.timeout, provision=True)
        before = hashlib.sha256(image.read_bytes()).hexdigest()
        try:
            item['metadata'] = {'valid': True, **check_image(image)}
        except InvalidImage as error:
            item['metadata'] = {'valid': False, 'error': str(error)}
        assert hashlib.sha256(image.read_bytes()).hexdigest() == before, 'checker changed the snapshot'
        item['snapshot_sha256'] = before
        # Reopen a copy so the original abrupt-stop snapshot remains inspectable.
        reopened = root / f'{case}-reopen.img'
        shutil.copyfile(image, reopened)
        if item['metadata']['valid']:
            verifier = 'verify-synced' if case == 'synced' else 'verify'
            item['reopen'] = run_guest(saved_kernel, reopened, root / f'{case}-reopen.log',
                                       verifier, 'RESULT: 1 PASSED | 0 FAILED', args.timeout)
        else:
            if case == 'synced':
                raise RuntimeError('fully synchronized snapshot failed metadata validation')
            item['reopen'] = run_guest(saved_kernel, reopened, root / f'{case}-reopen.log',
                                       'verify', 'No mountable filesystem found.', args.timeout)
            assert hashlib.sha256(reopened.read_bytes()).hexdigest() == before, 'rejected image was modified'
        report['cases'].append(item)
        (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'case': case, 'metadata': item['metadata'], 'reopen': item['reopen']}), flush=True)
    print(f'Completed {len(report["cases"])} measured cuts. See {root / "report.json"}', flush=True)


if __name__ == '__main__':
    main()
