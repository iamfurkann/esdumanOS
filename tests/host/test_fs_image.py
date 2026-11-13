"""Validate the image checker against independent v3 fixtures and corruptions."""
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_fs_image import InvalidImage, check_image


class ImageChecks(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'fixture.img'
        self.disk = bytearray(544 * 512)
        self.disk[510:512] = b'\x55\xaa'
        self.disk[450] = 0x7f
        struct.pack_into('<II', self.disk, 454, 64, 480)
        struct.pack_into('<IHH4IHH4I', self.disk, 64 * 512,
                         0x46647365, 3, 512, 480, 1, 96, 512, 96, 8, 97, 98, 35, 0)
        self.fat = (64 + 97) * 512
        struct.pack_into('<III', self.disk, self.fat, 0xffffffff, 0xffffffff, 0xffffffff)
        self.entry(1, b'file', 4, 2)

    def entry(self, index, name, size=0, cluster=0, parent=0, kind=0):
        struct.pack_into('<64s6I3H2B', self.disk, 65 * 512 + index * 96,
                         name, size, cluster, 0, 0, 0, 0, index, parent, 0o644, kind, 1)

    def check(self, invalid=None):
        self.path.write_bytes(self.disk)
        before = self.path.read_bytes()
        if invalid:
            with self.assertRaisesRegex(InvalidImage, invalid):
                check_image(self.path)
        else:
            result = check_image(self.path)
            self.assertEqual(result['format'], 3)
            self.assertEqual(result['allocated_clusters'], 1)
        self.assertEqual(self.path.read_bytes(), before, 'checker changed the image')

    def test_valid_v3_and_empty_raw_file(self):
        self.entry(2, b'empty', 0, 0)
        self.entry(3, b'old-empty', 0, 0xffffffff)
        self.check()
        self.entry(1, b'allocated-empty', 0, 2)
        self.check()

    def test_truncated(self):
        self.disk.pop()
        self.check('truncated')

    def test_geometry(self):
        for offset, value, error in [(20, 0xffffffff, 'limits'), (28, 1, 'ordering'),
                                     (36, 48, 'geometry'), (8, 481, 'capacity')]:
            with self.subTest(offset=offset):
                old = self.disk[64 * 512 + offset:64 * 512 + offset + 4]
                struct.pack_into('<I', self.disk, 64 * 512 + offset, value)
                self.check(error)
                self.disk[64 * 512 + offset:64 * 512 + offset + 4] = old

    def test_bad_names(self):
        for name in [b'x' * 64, b'', b'.', b'..', b'a/b']:
            with self.subTest(name=name):
                self.entry(1, name, 4, 2)
                self.check('name')

    def test_parent_and_duplicates(self):
        self.entry(1, b'file', 4, 2, parent=7)
        self.check('parent')
        self.entry(1, b'file', 4, 2)
        self.entry(2, b'file')
        self.check('duplicate')
        self.entry(2, b'cycle', parent=2, kind=1)
        self.check('cycle')

    def test_chain_corruption(self):
        self.entry(1, b'file', 4097, 2)
        self.check('short')
        struct.pack_into('<I', self.disk, self.fat + 8, 2)
        self.check('cyclic')
        struct.pack_into('<I', self.disk, self.fat + 8, 0xffffffff)
        self.entry(1, b'file', 4, 2)
        self.entry(2, b'shared', 4, 2)
        self.check('shared')
        self.entry(2, b'empty')
        struct.pack_into('<I', self.disk, self.fat + 12, 0xffffffff)
        self.check('orphan')


if __name__ == '__main__':
    unittest.main()
