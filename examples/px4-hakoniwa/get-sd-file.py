#!/usr/bin/env python3
"""Lists and copies files from a FAT32 SD card image without mounting it.

    get-sd-file.py <sd.img> --list
    get-sd-file.py <sd.img> <path-in-image> <out-file>
    get-sd-file.py <sd.img> --newest <pattern> <out-file>
"""
import fnmatch
import os
from pathlib import Path
import sys
import tempfile


class FatError(Exception):
    pass


class Entry:
    def __init__(self, path, size, date, time, cluster, attributes):
        self.path = path
        self.size = size
        self.date = date
        self.time = time
        self.cluster = cluster
        self.attributes = attributes

    @property
    def is_directory(self):
        return bool(self.attributes & 0x10)

    @property
    def timestamp(self):
        return self.date, self.time

    def time_text(self):
        year = 1980 + ((self.date >> 9) & 0x7f)
        month = (self.date >> 5) & 0x0f
        day = self.date & 0x1f
        hour = (self.time >> 11) & 0x1f
        minute = (self.time >> 5) & 0x3f
        second = (self.time & 0x1f) * 2
        return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}"


def little(data, offset, size):
    return int.from_bytes(data[offset:offset + size], "little")


class Fat32:
    def __init__(self, path):
        self.path = path
        self.file = open(path, "rb")
        try:
            self._read_bpb()
        except Exception:
            self.file.close()
            raise

    def close(self):
        self.file.close()

    def _read_bpb(self):
        boot = self.file.read(512)
        if len(boot) != 512 or boot[510:512] != b"\x55\xaa":
            raise FatError(f"{self.path}: not a FAT32 image (bad boot sector)")

        self.sector_size = little(boot, 11, 2)
        self.sectors_per_cluster = boot[13]
        self.reserved_sectors = little(boot, 14, 2)
        self.fat_count = boot[16]
        root_entries = little(boot, 17, 2)
        total16 = little(boot, 19, 2)
        fat16 = little(boot, 22, 2)
        total32 = little(boot, 32, 4)
        self.fat_sectors = little(boot, 36, 4)
        flags = little(boot, 40, 2)
        version = little(boot, 42, 2)
        self.root_cluster = little(boot, 44, 4)

        if self.sector_size < 512 or self.sector_size > 4096:
            raise FatError(f"{self.path}: not a FAT32 image (bad sector size)")
        if self.sector_size & (self.sector_size - 1):
            raise FatError(f"{self.path}: not a FAT32 image (bad sector size)")
        if not self.sectors_per_cluster:
            raise FatError(f"{self.path}: not a FAT32 image (zero cluster size)")
        if self.sectors_per_cluster & (self.sectors_per_cluster - 1):
            raise FatError(f"{self.path}: not a FAT32 image (bad cluster size)")
        if not self.reserved_sectors or not self.fat_count or not self.fat_sectors:
            raise FatError(f"{self.path}: not a FAT32 image (bad BPB geometry)")
        if root_entries or fat16 or version:
            raise FatError(f"{self.path}: not a FAT32 image")

        self.total_sectors = total16 or total32
        first_data = self.reserved_sectors + self.fat_count * self.fat_sectors
        if self.total_sectors <= first_data:
            raise FatError(f"{self.path}: not a FAT32 image (no data area)")
        data_sectors = self.total_sectors - first_data
        self.cluster_count = data_sectors // self.sectors_per_cluster
        if self.cluster_count < 65525 or self.cluster_count > 0x0fffffed:
            raise FatError(f"{self.path}: not a FAT32 image ({self.cluster_count} clusters)")
        if self.fat_sectors * self.sector_size // 4 < self.cluster_count + 2:
            raise FatError(f"{self.path}: not a FAT32 image (FAT is too small)")

        active_fat = 0
        if flags & 0x80:
            active_fat = flags & 0x0f
        if active_fat >= self.fat_count:
            raise FatError(f"{self.path}: not a FAT32 image (bad active FAT)")
        self.fat_offset = (self.reserved_sectors + active_fat * self.fat_sectors) * self.sector_size
        self.data_offset = first_data * self.sector_size
        self.cluster_size = self.sector_size * self.sectors_per_cluster
        self.max_cluster = self.cluster_count + 1
        self._check_cluster(self.root_cluster)

        self.file.seek(0, os.SEEK_END)
        if self.file.tell() < self.total_sectors * self.sector_size:
            raise FatError(f"{self.path}: truncated FAT32 image")

    def _check_cluster(self, cluster):
        if cluster < 2 or cluster > self.max_cluster:
            raise FatError(f"{self.path}: invalid cluster {cluster}")

    def _fat_entry(self, cluster):
        self._check_cluster(cluster)
        self.file.seek(self.fat_offset + cluster * 4)
        data = self.file.read(4)
        if len(data) != 4:
            raise FatError(f"{self.path}: truncated FAT")
        return int.from_bytes(data, "little") & 0x0fffffff

    def chain(self, first):
        cluster = first
        seen = set()
        while True:
            self._check_cluster(cluster)
            if cluster in seen:
                raise FatError(f"{self.path}: cycle in cluster chain at {cluster}")
            seen.add(cluster)
            yield cluster
            following = self._fat_entry(cluster)
            if following >= 0x0ffffff8:
                return
            if following == 0:
                raise FatError(f"{self.path}: broken cluster chain after {cluster} (free cluster)")
            if following == 0x0ffffff7:
                raise FatError(f"{self.path}: broken cluster chain after {cluster} (bad cluster)")
            if following >= 0x0ffffff0:
                raise FatError(f"{self.path}: broken cluster chain after {cluster} (reserved value)")
            if following < 2 or following > self.max_cluster:
                raise FatError(f"{self.path}: broken cluster chain after {cluster}")
            cluster = following

    def read_cluster(self, cluster):
        self._check_cluster(cluster)
        offset = self.data_offset + (cluster - 2) * self.cluster_size
        self.file.seek(offset)
        data = self.file.read(self.cluster_size)
        if len(data) != self.cluster_size:
            raise FatError(f"{self.path}: truncated cluster {cluster}")
        return data

    def directory_entries(self, first, parent):
        long_entries = []
        for cluster in self.chain(first):
            data = self.read_cluster(cluster)
            for offset in range(0, len(data), 32):
                raw = data[offset:offset + 32]
                if raw[0] == 0:
                    return
                if raw[0] == 0xe5:
                    long_entries = []
                    continue
                if raw[11] == 0x0f:
                    long_entries.append(raw)
                    continue

                short = short_name(raw)
                name = long_name(long_entries, raw[0:11]) or short
                long_entries = []
                attributes = raw[11]
                if attributes & 0x08 or name in (".", ".."):
                    continue
                if not name or "/" in name or "\\" in name:
                    continue
                path = f"{parent}/{name}" if parent else name
                high = little(raw, 20, 2)
                low = little(raw, 26, 2)
                first_cluster = (high << 16) | low
                yield Entry(path, little(raw, 28, 4), little(raw, 24, 2),
                            little(raw, 22, 2), first_cluster, attributes)

    def files(self):
        result = []
        directories = {self.root_cluster}

        def visit(first, parent):
            for entry in self.directory_entries(first, parent):
                if entry.is_directory:
                    self._check_cluster(entry.cluster)
                    if entry.cluster in directories:
                        raise FatError(f"{self.path}: directory cycle at {entry.path}")
                    directories.add(entry.cluster)
                    visit(entry.cluster, entry.path)
                else:
                    result.append(entry)

        visit(self.root_cluster, "")
        result.sort(key=lambda entry: entry.path)
        return result

    def validate_file(self, entry):
        if entry.size == 0:
            return
        if entry.cluster == 0:
            raise FatError(f"{self.path}: {entry.path}: missing first cluster")
        needed = (entry.size + self.cluster_size - 1) // self.cluster_size
        clusters = self.chain(entry.cluster)
        for _ in range(needed):
            try:
                next(clusters)
            except StopIteration:
                raise FatError(f"{self.path}: {entry.path}: cluster chain is too short") from None
        try:
            next(clusters)
        except StopIteration:
            return
        raise FatError(f"{self.path}: {entry.path}: cluster chain is longer than its size")

    def copy_file(self, entry, output):
        if same_file(self.path, output):
            raise FatError(f"{output}: output file is the SD image")
        output_path = Path(output)
        output_dir = output_path.parent
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix=f".{output_path.name}.",
                                             dir=output_dir, delete=False) as destination:
                temporary = Path(destination.name)
                remaining = entry.size
                if remaining:
                    clusters = self.chain(entry.cluster)
                    while remaining:
                        try:
                            cluster = next(clusters)
                        except StopIteration:
                            raise FatError(
                                f"{self.path}: {entry.path}: cluster chain is too short") from None
                        data = self.read_cluster(cluster)
                        count = min(remaining, len(data))
                        destination.write(data[:count])
                        remaining -= count
                    try:
                        next(clusters)
                    except StopIteration:
                        pass
                    else:
                        raise FatError(
                            f"{self.path}: {entry.path}: cluster chain is longer than its size")
            os.replace(temporary, output_path)
            temporary = None
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except FileNotFoundError:
                    pass


def short_name(raw):
    name = bytearray(raw[0:8])
    extension = raw[8:11]
    if name and name[0] == 0x05:
        name[0] = 0xe5
    base = bytes(name).rstrip(b" ").decode("cp437", "replace")
    ext = extension.rstrip(b" ").decode("cp437", "replace")
    flags = raw[12]
    if flags & 0x08:
        base = base.lower()
    if flags & 0x10:
        ext = ext.lower()
    if ext:
        return f"{base}.{ext}"
    return base


def lfn_checksum(short):
    checksum = 0
    for byte in short:
        checksum = ((checksum & 1) << 7) + (checksum >> 1) + byte
        checksum &= 0xff
    return checksum


def long_name(entries, short):
    if not entries:
        return None
    count = entries[0][0] & 0x1f
    if not entries[0][0] & 0x40 or count != len(entries) or not 1 <= count <= 20:
        return None
    expected = list(range(count, 0, -1))
    if [entry[0] & 0x1f for entry in entries] != expected:
        return None
    checksum = lfn_checksum(short)
    for index, entry in enumerate(entries):
        allowed_order = (count | 0x40) if index == 0 else count - index
        if entry[0] != allowed_order:
            return None
        if entry[11] != 0x0f or entry[12] != 0 or entry[13] != checksum:
            return None
        if entry[26:28] != b"\x00\x00":
            return None

    units = []
    for entry in reversed(entries):
        encoded = entry[1:11] + entry[14:26] + entry[28:32]
        units.extend(int.from_bytes(encoded[index:index + 2], "little")
                     for index in range(0, len(encoded), 2))
    name_units = []
    ended = False
    for unit in units:
        if unit == 0:
            ended = True
            continue
        if unit == 0xffff and ended:
            continue
        if ended or unit == 0xffff:
            return None
        name_units.append(unit)
    encoded = b"".join(unit.to_bytes(2, "little") for unit in name_units)
    try:
        return encoded.decode("utf-16le")
    except UnicodeDecodeError:
        return None


def same_file(left, right):
    try:
        return os.path.samefile(left, right)
    except FileNotFoundError:
        return Path(left).resolve() == Path(right).resolve()


def requested_file(entries, path):
    clean = path.replace("\\", "/").strip("/")
    if not clean or any(part in ("", ".", "..") for part in clean.split("/")):
        raise FatError(f"invalid path in image: {path}")
    for entry in entries:
        if entry.path == clean:
            return entry
    matches = [entry for entry in entries if entry.path.casefold() == clean.casefold()]
    if len(matches) == 1:
        return matches[0]
    raise FatError(f"file not found in image: {path}")


def usage(program):
    return (f"usage: {program} <sd.img> --list\n"
            f"       {program} <sd.img> <path-in-image> <out-file>\n"
            f"       {program} <sd.img> --newest <pattern> <out-file>")


def run(arguments):
    program = Path(arguments[0]).name
    if len(arguments) not in (3, 4, 5):
        raise FatError(usage(program))
    image = arguments[1]
    mode = arguments[2]
    if mode == "--list" and len(arguments) != 3:
        raise FatError(usage(program))
    if mode == "--newest" and len(arguments) != 5:
        raise FatError(usage(program))
    if mode.startswith("--") and mode not in ("--list", "--newest"):
        raise FatError(usage(program))
    if mode not in ("--list", "--newest") and len(arguments) != 4:
        raise FatError(usage(program))

    filesystem = Fat32(image)
    try:
        entries = filesystem.files()
        if mode == "--list":
            for entry in entries:
                filesystem.validate_file(entry)
                print(f"{entry.time_text()} {entry.size:10d} {entry.path}")
            return
        if mode == "--newest":
            pattern = arguments[3]
            matches = [entry for entry in entries
                       if fnmatch.fnmatchcase(entry.path.casefold(), pattern.casefold())]
            if not matches:
                raise FatError(f"no file matches in image: {pattern}")
            entry = max(matches, key=lambda item: (item.timestamp, item.path))
            output = arguments[4]
        else:
            entry = requested_file(entries, mode)
            output = arguments[3]
        filesystem.copy_file(entry, output)
        print(f"{entry.path} -> {output} ({entry.size} bytes)")
    finally:
        filesystem.close()


def main():
    try:
        run(sys.argv)
    except (FatError, OSError) as error:
        print(f"{Path(sys.argv[0]).name}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
