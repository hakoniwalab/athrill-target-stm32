#!/usr/bin/env python3
"""List a partitionless FAT32 image recursively without mounting it."""

import argparse
import struct
import sys


class Fat32:
    def __init__(self, path):
        self.path = path
        self.image = open(path, "rb")
        boot = self.image.read(512)
        if len(boot) != 512 or boot[510:512] != b"\x55\xaa":
            raise ValueError("not a FAT boot sector")
        self.bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
        self.sectors_per_cluster = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fat_count = boot[16]
        fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        self.root_cluster = struct.unpack_from("<I", boot, 44)[0]
        if self.bytes_per_sector == 0 or self.sectors_per_cluster == 0 or fat_sectors == 0:
            raise ValueError("image is not FAT32")
        self.fat_offset = reserved * self.bytes_per_sector
        self.data_offset = (reserved + fat_count * fat_sectors) * self.bytes_per_sector
        self.cluster_bytes = self.bytes_per_sector * self.sectors_per_cluster

    def close(self):
        self.image.close()

    def fat_entry(self, cluster):
        self.image.seek(self.fat_offset + cluster * 4)
        data = self.image.read(4)
        if len(data) != 4:
            raise ValueError("FAT entry lies outside the image")
        return struct.unpack("<I", data)[0] & 0x0FFFFFFF

    def chain(self, first):
        seen = set()
        cluster = first
        while 2 <= cluster < 0x0FFFFFF8:
            if cluster in seen:
                raise ValueError(f"cycle in cluster chain at {cluster}")
            seen.add(cluster)
            yield cluster
            cluster = self.fat_entry(cluster)

    def cluster(self, number):
        offset = self.data_offset + (number - 2) * self.cluster_bytes
        self.image.seek(offset)
        data = self.image.read(self.cluster_bytes)
        if len(data) != self.cluster_bytes:
            raise ValueError(f"cluster {number} lies outside the image")
        return data

    @staticmethod
    def long_name(entry):
        data = entry[1:11] + entry[14:26] + entry[28:32]
        text = data.decode("utf-16le", errors="replace")
        return text.split("\x00", 1)[0].replace("\uffff", "")

    @staticmethod
    def short_name(entry):
        base = entry[0:8].decode("ascii", errors="replace").rstrip()
        extension = entry[8:11].decode("ascii", errors="replace").rstrip()
        return base if not extension else f"{base}.{extension}"

    def directory(self, first, parent=""):
        long_parts = []
        for cluster in self.chain(first):
            data = self.cluster(cluster)
            for offset in range(0, len(data), 32):
                entry = data[offset:offset + 32]
                if entry[0] == 0x00:
                    return
                if entry[0] == 0xE5:
                    long_parts.clear()
                    continue
                attributes = entry[11]
                if attributes == 0x0F:
                    long_parts.append(self.long_name(entry))
                    continue
                name = "".join(reversed(long_parts)) if long_parts else self.short_name(entry)
                long_parts.clear()
                if attributes & 0x08 or name in (".", ".."):
                    continue
                high = struct.unpack_from("<H", entry, 20)[0]
                low = struct.unpack_from("<H", entry, 26)[0]
                child = (high << 16) | low
                size = struct.unpack_from("<I", entry, 28)[0]
                path = f"{parent}/{name}"
                if attributes & 0x10:
                    print(f"d {path}/")
                    yield from self.directory(child, path)
                else:
                    print(f"f {size:>10} {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    args = parser.parse_args()
    try:
        fat = Fat32(args.image)
        try:
            list(fat.directory(fat.root_cluster))
        finally:
            fat.close()
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())
