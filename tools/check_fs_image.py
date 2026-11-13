#!/usr/bin/env python3
"""Inspect an offline esdumanOS v3 raw image without modifying or decrypting it."""
import argparse
import json
from pathlib import Path
import struct

SECTOR = 512
EOF = 0xffffffff
SUPER = struct.Struct('<IHH4IHH4I')
ENTRY = struct.Struct('<64s6I3H2B')


class InvalidImage(ValueError):
    """The image violates the v3 metadata contract."""


def require(condition, message):
    if not condition:
        raise InvalidImage(message)


def check_image(path):
    # Read only the bounded metadata tables. Even a very large sparse image
    # must not make validation allocate memory proportional to device capacity.
    with Path(path).open('rb') as disk:
        disk.seek(0, 2)
        size = disk.tell()
        require(size >= SECTOR and size % SECTOR == 0, 'image is truncated or not sector-aligned')

        def read(offset, length):
            require(0 <= offset <= size and 0 <= length <= size - offset, 'metadata extends beyond image')
            disk.seek(offset)
            result = disk.read(length)
            require(len(result) == length, 'short metadata read')
            return result

        mbr = read(0, SECTOR)
        require(mbr[510:512] == b'\x55\xaa', 'missing MBR signature')
        partitions = []
        for index in range(4):
            offset = 446 + 16 * index
            if mbr[offset + 4] == 0x7f:
                start, count = struct.unpack_from('<II', mbr, offset + 8)
                if start > 0 and count > 200 and start < size // SECTOR and count <= size // SECTOR - start:
                    partitions.append((start, count))
        require(partitions, 'no bounded esdumanOS partition')
        # The kernel selects the first valid owned partition in MBR order.
        start, sectors = partitions[0]
        sb = read(start * SECTOR, SECTOR)
        (magic, version, sector_size, total, directory, directory_sectors,
         count, entry_size, cluster_sectors, fat_start, data_start, clusters, flags) = SUPER.unpack_from(sb)
        require((magic, version, sector_size, entry_size, cluster_sectors) ==
                (0x46647365, 3, SECTOR, ENTRY.size, 8), 'unsupported filesystem format or layout')
        require(0 < count <= 8192 and 2 < clusters <= 262144, 'table limits exceeded')
        require(total == sectors, 'superblock and partition capacity disagree')
        dir_bytes, fat_bytes = count * ENTRY.size, clusters * 4
        require(0 < directory < fat_start < data_start < sectors, 'invalid metadata ordering')
        require(directory_sectors == (dir_bytes + SECTOR - 1) // SECTOR and
                directory_sectors <= fat_start - directory and
                (fat_bytes + SECTOR - 1) // SECTOR <= data_start - fat_start,
                'metadata tables overlap')
        require(clusters <= (sectors - data_start) // cluster_sectors, 'data geometry exceeds partition')
        raw_entries = read((start + directory) * SECTOR, dir_bytes)
        fat = struct.unpack(f'<{clusters}I', read((start + fat_start) * SECTOR, fat_bytes))
        entries, names = {}, set()
        for index, entry in enumerate(ENTRY.iter_unpack(raw_entries)):
            name, length, cluster, uid, gid, mtime, ctime, entry_id, parent, mode, kind, used = entry
            if not used:
                continue
            require(index != 0 and used == 1 and entry_id == index, f'entry {index}: invalid identity')
            require(b'\0' in name, f'entry {index}: unterminated name')
            name = name.split(b'\0', 1)[0]
            require(name and name not in (b'.', b'..') and b'/' not in name, f'entry {index}: invalid name')
            require((parent, name) not in names, f'entry {index}: duplicate name in directory')
            names.add((parent, name))
            require(kind in (0, 1), f'entry {index}: invalid file type')
            entries[index] = (parent, kind, length, cluster)

        for index, (parent, kind, length, cluster) in entries.items():
            visited = {index}
            while parent:
                require(parent not in visited, f'entry {index}: parent cycle')
                require(parent in entries and entries[parent][1] == 1, f'entry {index}: missing directory parent')
                visited.add(parent)
                parent = entries[parent][0]

        require(fat[0] == EOF and fat[1] == EOF, 'reserved clusters are not reserved')
        owners = {}
        for index, (parent, kind, length, cluster) in entries.items():
            if kind == 1:
                require(length == 0 and cluster == 0, f'entry {index}: directory owns file data')
                continue
            if length == 0 and cluster in (0, EOF):
                continue
            needed = 1 + (length - 1) // (SECTOR * cluster_sectors) if length else 1
            require(needed <= clusters - 2, f'entry {index}: impossible file length')
            for _ in range(needed):
                require(2 <= cluster < clusters, f'entry {index}: short or out-of-range chain')
                require(cluster not in owners, f'entry {index}: cyclic or shared cluster {cluster}')
                owners[cluster] = index
                cluster = fat[cluster]
            require(cluster == EOF, f'entry {index}: chain exceeds recorded file length')
        for cluster in range(2, clusters):
            require(cluster in owners or fat[cluster] == 0, f'cluster {cluster}: orphan allocation')

        return {'format': version, 'partition_lba': start, 'partition_sectors': sectors,
                'entries': len(entries), 'allocated_clusters': len(owners), 'clusters': clusters,
                'keyslot_present': struct.unpack_from('<I', sb, SUPER.size + 32)[0] != 0,
                'scope': 'metadata only; file contents and keyslot authentication are not verified'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='offline raw image or a consistent snapshot')
    args = parser.parse_args()
    try:
        result = check_image(args.image)
    except (OSError, InvalidImage) as error:
        print(json.dumps({'valid': False, 'error': str(error)}))
        return 1
    print(json.dumps({'valid': True, **result}, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
