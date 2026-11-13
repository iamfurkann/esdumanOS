"""Exercise interactive image creation and cleanup inside a disposable directory."""
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

makefile = Path(__file__).resolve().parents[2] / 'Makefile'
names = ('disk.img', 'usb.img', 'usb2.img')
with TemporaryDirectory() as folder:
    work = Path(folder)
    # Version parsing happens even for image and clean targets. Keep the
    # fixture self-contained without pointing cleanup at the source tree.
    (work / 'include').mkdir()
    (work / 'include/kernel.h').write_bytes((makefile.parent / 'include/kernel.h').read_bytes())
    def make(*targets):
        subprocess.run(['make', '-s', '-f', str(makefile), *targets], cwd=work, check=True)

    make(*names)
    assert [int((work / name).stat().st_size) for name in names] == [2 << 20, 2 << 20, 4 << 20]
    for name in names:
        (work / name).write_bytes(('existing ' + name).encode())
    make(*names)
    make('clean')
    for name in names:
        assert (work / name).read_bytes() == ('existing ' + name).encode()
print('Interactive image creation, reuse and clean passed.')
