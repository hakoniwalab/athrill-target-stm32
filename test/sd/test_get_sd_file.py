#!/usr/bin/env python3
"""Tests for get-sd-file.py using FAT32 images built by this test."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
GET = ROOT / "examples" / "px4-hakoniwa" / "get-sd-file.py"
PUT = ROOT / "examples" / "px4-hakoniwa" / "put-sd-file.py"
MAKE = ROOT / "examples" / "px4-sih" / "make-sd-image.sh"

SECTOR_SIZE = 512
RESERVED = 32
FAT_COUNT = 2
FAT_SECTORS = 512
CLUSTERS = 65525
FIRST_DATA_SECTOR = RESERVED + FAT_COUNT * FAT_SECTORS
TOTAL_SECTORS = FIRST_DATA_SECTOR + CLUSTERS
EOC = 0x0fffffff


def fat_date(year, month, day):
    return ((year - 1980) << 9) | (month << 5) | day


def fat_time(hour, minute, second):
    return (hour << 11) | (minute << 5) | (second // 2)


def checksum(short):
    value = 0
    for byte in short:
        value = ((value & 1) << 7) + (value >> 1) + byte
        value &= 0xff
    return value


def long_entries(name, short):
    encoded = name.encode("utf-16le")
    units = [int.from_bytes(encoded[index:index + 2], "little")
             for index in range(0, len(encoded), 2)]
    units.append(0)
    while len(units) % 13:
        units.append(0xffff)
    chunks = [units[index:index + 13] for index in range(0, len(units), 13)]
    result = []
    positions = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    for number in range(len(chunks), 0, -1):
        entry = bytearray(32)
        entry[0] = number | (0x40 if number == len(chunks) else 0)
        entry[11] = 0x0f
        entry[13] = checksum(short)
        for position, unit in zip(positions, chunks[number - 1]):
            entry[position:position + 2] = unit.to_bytes(2, "little")
        result.append(bytes(entry))
    return result


def short_entry(short, attributes, cluster, size, date, time, lowercase=0):
    if len(short) != 11:
        raise ValueError("short name must have eleven bytes")
    entry = bytearray(32)
    entry[0:11] = short
    entry[11] = attributes
    entry[12] = lowercase
    entry[20:22] = (cluster >> 16).to_bytes(2, "little")
    entry[22:24] = time.to_bytes(2, "little")
    entry[24:26] = date.to_bytes(2, "little")
    entry[26:28] = (cluster & 0xffff).to_bytes(2, "little")
    entry[28:32] = size.to_bytes(4, "little")
    return bytes(entry)


def directory_entry(short, cluster, date, long=None, parent=0):
    entries = []
    if long:
        entries.extend(long_entries(long, short))
    entries.append(short_entry(short, 0x10, cluster, 0, date, 0, lowercase=parent))
    return entries


def cluster_offset(cluster):
    return (FIRST_DATA_SECTOR + cluster - 2) * SECTOR_SIZE


def write_fat_entry(image, cluster, value):
    with image.open("r+b") as output:
        for fat in range(FAT_COUNT):
            offset = (RESERVED + fat * FAT_SECTORS) * SECTOR_SIZE + cluster * 4
            output.seek(offset)
            output.write(value.to_bytes(4, "little"))


def write_cluster(output, cluster, data):
    if len(data) > SECTOR_SIZE:
        raise ValueError("cluster contents are too large")
    output.seek(cluster_offset(cluster))
    output.write(data)
    output.write(bytes(SECTOR_SIZE - len(data)))


def make_image(path):
    date = fat_date(2026, 11, 4)
    next_date = fat_date(2026, 11, 5)
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xeb\x58\x90"
    boot[3:11] = b"PYTEST  "
    boot[11:13] = SECTOR_SIZE.to_bytes(2, "little")
    boot[13] = 1
    boot[14:16] = RESERVED.to_bytes(2, "little")
    boot[16] = FAT_COUNT
    boot[21] = 0xf8
    boot[32:36] = TOTAL_SECTORS.to_bytes(4, "little")
    boot[36:40] = FAT_SECTORS.to_bytes(4, "little")
    boot[44:48] = (2).to_bytes(4, "little")
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"

    main_data = bytes((index * 17 + 3) & 0xff for index in range(1300))
    with path.open("wb") as output:
        output.truncate(TOTAL_SECTORS * SECTOR_SIZE)
        output.seek(0)
        output.write(boot)

        chains = {0: 0x0ffffff8, 1: EOC, 2: EOC, 3: EOC, 4: EOC,
                  5: 9, 9: 6, 6: EOC, 7: EOC, 8: EOC, 10: EOC}
        for fat in range(FAT_COUNT):
            base = (RESERVED + fat * FAT_SECTORS) * SECTOR_SIZE
            for cluster, following in chains.items():
                output.seek(base + cluster * 4)
                output.write(following.to_bytes(4, "little"))

        root = []
        root.extend(directory_entry(b"LOG        ", 3, date, parent=0x08))
        root.append(short_entry(b"EMPTY   TXT", 0x20, 0, 0, date, 0, lowercase=0x18))
        deleted = bytearray(short_entry(b"GONE    TXT", 0x20, 11, 7, date, 0))
        deleted[0] = 0xe5
        root.append(bytes(deleted))
        write_cluster(output, 2, b"".join(root) + bytes(32))

        log = [short_entry(b".          ", 0x10, 3, 0, date, 0),
               short_entry(b"..         ", 0x10, 2, 0, date, 0)]
        log.extend(directory_entry(b"2026-1~1   ", 4, date, long="2026-11-04"))
        write_cluster(output, 3, b"".join(log) + bytes(32))

        long_short = b"0000~1  ULG"
        subdir = [short_entry(b".          ", 0x10, 4, 0, date, 0),
                  short_entry(b"..         ", 0x10, 3, 0, date, 0)]
        subdir.extend(long_entries("00_00_20 flight.ulg", long_short))
        subdir.append(short_entry(long_short, 0x20, 5, len(main_data), date,
                                  fat_time(0, 0, 20)))
        subdir.append(short_entry(b"OLDER   ULG", 0x20, 7, 3,
                                  fat_date(2026, 11, 3), fat_time(23, 59, 58), 0x18))
        subdir.append(short_entry(b"A       ULG", 0x20, 8, 1, next_date,
                                  fat_time(12, 34, 56), 0x18))
        subdir.append(short_entry(b"Z       ULG", 0x20, 10, 8, next_date,
                                  fat_time(12, 34, 56), 0x18))
        deleted = bytearray(short_entry(b"ERASED  ULG", 0x20, 11, 1,
                                        next_date, fat_time(13, 0, 0)))
        deleted[0] = 0xe5
        subdir.append(bytes(deleted))
        write_cluster(output, 4, b"".join(subdir) + bytes(32))

        for cluster, start in ((5, 0), (9, 512), (6, 1024)):
            write_cluster(output, cluster, main_data[start:start + 512])
        write_cluster(output, 7, b"old")
        write_cluster(output, 8, b"a")
        write_cluster(output, 10, b"newest-z")
    return main_data


class GetSdFileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.image = self.directory / "test.img"
        self.main_data = make_image(self.image)

    def tearDown(self):
        self.temporary.cleanup()

    def run_get(self, *arguments, check=True):
        return subprocess.run(
            [sys.executable, "-I", str(GET), str(self.image), *map(str, arguments)],
            check=check, text=True, capture_output=True)

    def test_list_recurses_and_decodes_long_names(self):
        result = self.run_get("--list")
        self.assertIn("2026-11-04 00:00:00          0 empty.txt", result.stdout)
        self.assertIn("2026-11-04 00:00:20       1300 "
                      "log/2026-11-04/00_00_20 flight.ulg", result.stdout)
        self.assertIn("2026-11-05 12:34:56          8 log/2026-11-04/z.ulg",
                      result.stdout)
        self.assertNotIn("GONE", result.stdout)
        self.assertNotIn("erased", result.stdout)
        self.assertEqual(len(result.stdout.splitlines()), 5)

    def test_copy_follows_a_fragmented_chain(self):
        output = self.directory / "flight.ulg"
        self.run_get("log/2026-11-04/00_00_20 flight.ulg", output)
        self.assertEqual(output.read_bytes(), self.main_data)

    def test_copy_zero_length_file(self):
        output = self.directory / "empty.txt"
        self.run_get("EMPTY.TXT", output)
        self.assertEqual(output.read_bytes(), b"")

    def test_image_does_not_need_write_permission(self):
        output = self.directory / "read-only.ulg"
        self.image.chmod(0o444)
        self.run_get("log/2026-11-04/00_00_20 flight.ulg", output)
        self.assertEqual(output.read_bytes(), self.main_data)

    def test_newest_uses_path_to_break_a_timestamp_tie(self):
        output = self.directory / "newest.ulg"
        result = self.run_get("--newest", "*.ulg", output)
        self.assertEqual(output.read_bytes(), b"newest-z")
        self.assertIn("log/2026-11-04/z.ulg", result.stdout)

    def test_broken_chain_is_rejected_without_partial_output(self):
        write_fat_entry(self.image, 9, 0)
        output = self.directory / "broken.ulg"
        result = self.run_get("log/2026-11-04/00_00_20 flight.ulg", output,
                              check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("broken cluster chain", result.stderr)
        self.assertFalse(output.exists())

    def test_cycle_is_rejected_without_partial_output(self):
        write_fat_entry(self.image, 9, 5)
        output = self.directory / "cycle.ulg"
        result = self.run_get("log/2026-11-04/00_00_20 flight.ulg", output,
                              check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("cycle in cluster chain", result.stderr)
        self.assertFalse(output.exists())

    def test_non_fat32_image_is_rejected(self):
        self.image.write_bytes(bytes(SECTOR_SIZE))
        result = self.run_get("--list", check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not a FAT32 image", result.stderr)

    def test_image_from_make_script_and_put_script(self):
        image = self.directory / "real-blank.img"
        payload = self.directory / "payload.bin"
        copied = self.directory / "copied.bin"
        payload.write_bytes(bytes((index * 29) & 0xff for index in range(9000)))
        subprocess.run(["sh", str(MAKE), str(image)], check=True,
                       text=True, capture_output=True)
        subprocess.run([sys.executable, "-I", str(PUT), str(image),
                        str(payload), "FLIGHT.ULG"], check=True,
                       text=True, capture_output=True)
        result = subprocess.run([sys.executable, "-I", str(GET), str(image), "--list"],
                                check=True, text=True, capture_output=True)
        self.assertIn("2026-11-04 00:00:00       9000 FLIGHT.ULG", result.stdout)
        subprocess.run([sys.executable, "-I", str(GET), str(image), "flight.ulg",
                        str(copied)], check=True, text=True, capture_output=True)
        self.assertEqual(copied.read_bytes(), payload.read_bytes())


if __name__ == "__main__":
    unittest.main()
