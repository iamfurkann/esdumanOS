import unittest
import sys
import os

current_dir = os.path.dirname(os.path.abspath(__file__))
tools_dir = os.path.abspath(os.path.join(current_dir, '../../../tools'))
sys.path.insert(0, tools_dir)

import mkfs

class TestMkfs(unittest.TestCase):
    def test_disk_img_olusturma(self):
        if os.path.exists("disk.img"):
            os.remove("disk.img")
            
        mkfs.write_disk()
        
        self.assertTrue(os.path.exists("disk.img"), "HATA: disk.img dosyasi olusturulamadi!")
        
        boyut = os.path.getsize("disk.img")
        self.assertEqual(boyut, 51200, f"HATA: Beklenen boyut 51200 byte, ancak {boyut} byte bulundu!")

if __name__ == '__main__':
    unittest.main()