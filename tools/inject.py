import sys, os

if len(sys.argv) < 3:
    print("Kullanim: python3 inject.py disk.img dosya1 dosya2 ...")
    sys.exit(1)

disk_img = sys.argv[1]
files = sys.argv[2:]

dir_sector = bytearray(512)
current_sector = 2 # 0: Boot, 1: Dizin Tablosu, 2+: Veri Sektörleri

with open(disk_img, 'r+b') as f:
    for i, filepath in enumerate(files):
        if i >= 8: # MAX_FILES_IN_DIR sınırımız
            break
        
        filename_str = os.path.basename(filepath)
        file_size = os.path.getsize(filepath)
        file_data = open(filepath, 'rb').read()
        
        # 1. Directory Entry Oluştur (44 byte)
        b_name = filename_str.encode('utf-8').ljust(32, b'\x00')
        b_start = current_sector.to_bytes(4, 'little')
        b_size = file_size.to_bytes(4, 'little')
        b_used = (1).to_bytes(4, 'little')
        
        # 2. Dizin Tablosu Buffer'ına ekle
        offset = i * 44
        dir_sector[offset:offset+44] = b_name + b_start + b_size + b_used
        
        # 3. Dosyanın gerçek içeriğini diske (ilgili sektöre) yaz
        f.seek(current_sector * 512)
        f.write(file_data)
        
        # 4. Bir sonraki dosyanın sektör başlangıcını hesapla
        sectors_needed = (file_size // 512) + 1
        current_sector += sectors_needed

    # 5. Tüm dosyalar bitince güncellenmiş Dizin Tablosunu diske yaz
    f.seek(512)
    f.write(dir_sector)

print(f"[ENJEKTOR] Toplam {len(files)} dosya diske basariyla gomuldu!")