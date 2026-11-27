#!/usr/bin/env python3
"""Check how the console checker handles Athrill's JIT statistics."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECK = Path(__file__).resolve().parents[2] / "examples" / "console-check.py"
STATS = "INFO: JIT block runs from C=12 insns=34 translated=56 failed=0 held=78 code=90 bytes links=1"


class ConsoleCheckTests(unittest.TestCase):
    def check_console(self, line, allow=""):
        with tempfile.TemporaryDirectory() as directory:
            allow_file = Path(directory) / "test.allow"
            log_file = Path(directory) / "stderr.txt"
            allow_file.write_text(allow, encoding="utf-8")
            log_file.write_text(line + "\n", encoding="utf-8")
            return subprocess.run([sys.executable, str(CHECK), str(allow_file), str(log_file)],
                                  capture_output=True, text=True)

    def test_zero_failures_allowed(self):
        result = self.check_console(STATS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nonzero_failures_rejected_even_if_allow_list_matches(self):
        result = self.check_console(STATS.replace("failed=0", "failed=12"),
                                    "board | ^INFO: JIT | JIT log messages\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("JIT translation failures", result.stdout)

    def test_only_complete_statistics_line_is_exempt(self):
        for line in ("[px4] " + STATS, STATS + " ERROR", STATS.replace("held=78", "held=?"),
                     "INFO: PX4 logger failed"):
            with self.subTest(line=line):
                result = self.check_console(line)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("not in the allow-list", result.stdout)


if __name__ == "__main__":
    unittest.main()
