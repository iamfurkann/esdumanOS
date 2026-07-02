import sys, os

if len(sys.argv) < 3:
    print("Kullanim: python3 inject.py disk.img dosya1 dosya2 ...")
    sys.exit(1)

disk_img = sys.argv[1]
files = sys.argv[2:]

dir_sector = bytearray(512)
current_sector = 2

with open(disk_img, 'r+b') as f:
    for i, filepath in enumerate(files):
        if i >= 8:
            break
        
        filename_str = os.path.basename(filepath)
        file_size = os.path.getsize(filepath)
        file_data = open(filepath, 'rb').read()
        
        b_name = filename_str.encode('utf-8').ljust(32, b'\x00')
        b_start = current_sector.to_bytes(4, 'little')
        b_size = file_size.to_bytes(4, 'little')
        b_used = (1).to_bytes(4, 'little')
        
        offset = i * 44
        dir_sector[offset:offset+44] = b_name + b_start + b_size + b_used
        
        f.seek(current_sector * 512)
        f.write(file_data)
        
        sectors_needed = (file_size // 512) + 1
        current_sector += sectors_needed

    f.seek(512)
    f.write(dir_sector)

print(f"[ENJEKTOR] Toplam {len(files)} dosya diske basariyla gomuldu!")