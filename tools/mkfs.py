#!/usr/bin/env python3

SECTOR_SIZE = 512
DISK_SIZE = 100 * SECTOR_SIZE

def write_disk():
    # disk oluştur
    disk = bytearray(DISK_SIZE)

    # -------------------------
    # SECTOR 0 - boot/message
    # -------------------------
    msg = b"Merhaba Hard Disk! Ben esdumanOS!"
    disk[0:len(msg)] = msg

    # -------------------------
    # SECTOR 1 - directory
    # -------------------------
    filename = b"selam.txt".ljust(32, b"\x00")
    start_sector = (2).to_bytes(4, "little")
    file_size = len(b"esdumanOS Dosya Sistemine Hosgeldin!").to_bytes(4, "little")
    is_used = (1).to_bytes(4, "little")

    entry = filename + start_sector + file_size + is_used

    disk[512:512+len(entry)] = entry

    # -------------------------
    # SECTOR 2 - file data
    # -------------------------
    file_data = b"esdumanOS Dosya Sistemine Hosgeldin!"
    disk[1024:1024+len(file_data)] = file_data

    # write to disk image
    with open("disk.img", "wb") as f:
        f.write(disk)

    print("[+] disk.img oluşturuldu")

if __name__ == "__main__":
    write_disk()