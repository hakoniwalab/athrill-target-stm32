#!/usr/bin/env python3
"""Unit tests for the dependency-free ArduPilot DoS evidence oracle."""

import importlib.util
from pathlib import Path
import sys
import unittest


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "examples" / "ardupilot-hakoniwa" / "dos_oracle.py"
SPEC = importlib.util.spec_from_file_location("dos_oracle", MODULE_PATH)
dos_oracle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dos_oracle)


class VerdictTests(unittest.TestCase):
    def test_evidence_matrix(self):
        cases = (
            ("confirmed", dict(sent=1, reboot_seen=True, watchdog_seen=True, fell=True,
                               link_failed=False, heartbeat_stale=False), 0),
            ("no attack", dict(sent=0, reboot_seen=True, watchdog_seen=True, fell=True,
                               link_failed=False, heartbeat_stale=False), 2),
            ("reboot only", dict(sent=1, reboot_seen=True, watchdog_seen=False, fell=False,
                                 link_failed=False, heartbeat_stale=False), 2),
            ("watchdog only", dict(sent=1, reboot_seen=False, watchdog_seen=True, fell=False,
                                   link_failed=False, heartbeat_stale=False), 2),
            ("descent only", dict(sent=1, reboot_seen=False, watchdog_seen=False, fell=True,
                                  link_failed=False, heartbeat_stale=False), 2),
            ("link lost", dict(sent=1, reboot_seen=False, watchdog_seen=False, fell=False,
                               link_failed=True, heartbeat_stale=False), 2),
            ("heartbeat stale", dict(sent=1, reboot_seen=False, watchdog_seen=False, fell=False,
                                     link_failed=False, heartbeat_stale=True), 2),
            ("confirmed but heartbeat stale",
             dict(sent=1, reboot_seen=True, watchdog_seen=True, fell=False,
                  link_failed=False, heartbeat_stale=True), 2),
            ("no effect", dict(sent=1, reboot_seen=False, watchdog_seen=False, fell=False,
                               link_failed=False, heartbeat_stale=False), 1),
        )
        for name, evidence, expected in cases:
            with self.subTest(name=name):
                self.assertEqual(dos_oracle.classify(**evidence).exit_code, expected)

    def test_link_health_is_required_even_with_reset_evidence(self):
        verdict = dos_oracle.classify(
            sent=1, reboot_seen=True, watchdog_seen=True, fell=False,
            link_failed=True, heartbeat_stale=False)
        self.assertEqual(verdict.exit_code, 2)


class SourceFilterTests(unittest.TestCase):
    def test_exact_flight_controller_is_accepted(self):
        self.assertTrue(dos_oracle.is_target_message(1, 1, 1, 1))

    def test_other_system_or_component_is_rejected(self):
        self.assertFalse(dos_oracle.is_target_message(2, 1, 1, 1))
        self.assertFalse(dos_oracle.is_target_message(1, 42, 1, 1))

    def test_unknown_target_component_accepts_same_system(self):
        self.assertTrue(dos_oracle.is_target_message(1, 42, 1, 0))


if __name__ == "__main__":
    unittest.main()
