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
        
        # 1. Kontrol: disk.img dosyasi basariyla olustu mu?
        self.assertTrue(os.path.exists("disk.img"), "HATA: disk.img dosyasi olusturulamadi!")
        
        # 2. Kontrol: Dosya boyutu Kernel standardı olan 4096 sektor * 512 byte = 2097152 byte mi?
        boyut = os.path.getsize("disk.img")
        
        # [DÜZELTME]: 51200 baytlık (50KB) eski assert, 2MB'lık yeni standarda yükseltildi.
        self.assertEqual(boyut, 2097152, f"HATA: Beklenen boyut 2MB (2097152 byte), ancak {boyut} byte bulundu!")
        
if __name__ == '__main__':
    unittest.main()