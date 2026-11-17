"""Dependency-free evidence rules for the ArduPilot DoS reproduction test."""

from typing import NamedTuple, Optional


class Verdict(NamedTuple):
    exit_code: int
    label: str


def is_target_message(source_system: int, source_component: int,
                      target_system: int, target_component: Optional[int]) -> bool:
    """Return whether a MAVLink message belongs to the selected flight controller."""
    if source_system != target_system:
        return False
    return target_component in (None, 0) or source_component == target_component


def classify(sent: int, reboot_seen: bool, watchdog_seen: bool, fell: bool,
             link_failed: bool, heartbeat_stale: bool) -> Verdict:
    """Classify independent observations without importing pymavlink."""
    if sent <= 0:
        return Verdict(2, "no SERIAL_CONTROL sent")
    if link_failed:
        return Verdict(2, "MAVLink disconnected during the observation window")
    if heartbeat_stale:
        return Verdict(2, "flight-controller heartbeat became stale")
    if reboot_seen and watchdog_seen:
        return Verdict(0, "IWDG underflow and ArduPilot reboot corroborated")
    if reboot_seen or watchdog_seen or fell:
        return Verdict(2, "reboot, reset, or descent observed without full causality evidence")
    return Verdict(1, "no confirmed watchdog reset, reboot, or descent")
