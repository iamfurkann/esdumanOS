import unittest
import sys
import os

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../tools')))
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