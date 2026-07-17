#!/usr/bin/env python3
"""Graphically probe Digital Caviar OS and capture issue evidence frames."""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "demo" / "probe_frames"
DISK = ROOT / "demo" / "probe.raw"
KERNEL = ROOT / "kernel.elf"
WIDTH = 640
HEIGHT = 480
ISSUES: list[str] = []


def to_abs(x: int, y: int) -> tuple[int, int]:
    return (
        max(0, min(32767, x * 32767 // (WIDTH - 1))),
        max(0, min(32767, y * 32767 // (HEIGHT - 1))),
    )


class QemuSession:
    def __init__(self) -> None:
        DISK.parent.mkdir(parents=True, exist_ok=True)
        DISK.write_bytes(b"\0" * (1024 * 1024))
        FRAMES.mkdir(parents=True, exist_ok=True)
        for old in FRAMES.glob("*.ppm"):
            old.unlink()
        self.proc = subprocess.Popen(
            [
                "qemu-system-aarch64",
                "-M",
                "virt",
                "-cpu",
                "cortex-a72",
                "-m",
                "128M",
                "-global",
                "virtio-mmio.force-legacy=false",
                "-device",
                "ramfb",
                "-device",
                "virtio-keyboard-device",
                "-device",
                "virtio-tablet-device",
                "-drive",
                f"if=none,id=document,file={DISK},format=raw,cache=directsync",
                "-device",
                "virtio-blk-device,drive=document",
                "-kernel",
                str(KERNEL),
                "-display",
                "none",
                "-qmp",
                "stdio",
                "-monitor",
                "none",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        assert self.proc.stdin and self.proc.stdout
        if not self.proc.stdout.readline():
            raise RuntimeError("QEMU QMP did not respond")
        self._qmp({"execute": "qmp_capabilities"})

    def _read_response(self) -> dict:
        assert self.proc.stdout
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("QEMU QMP closed")
            payload = json.loads(line)
            if "return" in payload or "error" in payload:
                if "error" in payload:
                    raise RuntimeError(payload["error"])
                return payload

    def _qmp(self, payload: dict) -> dict:
        assert self.proc.stdin
        self.proc.stdin.write(json.dumps(payload) + "\n")
        self.proc.stdin.flush()
        return self._read_response()

    def pause(self, seconds: float) -> None:
        time.sleep(seconds)

    def capture(self, name: str) -> Path:
        path = FRAMES / f"{name}.ppm"
        self._qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {"command-line": f"screendump {path}"},
            }
        )
        self.pause(0.25)
        return path

    def click(self, x: int, y: int) -> None:
        abs_x, abs_y = to_abs(x, y)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "abs", "data": {"axis": "x", "value": abs_x}},
                        {"type": "abs", "data": {"axis": "y", "value": abs_y}},
                        {"type": "btn", "data": {"down": True, "button": "left"}},
                        {"type": "btn", "data": {"down": False, "button": "left"}},
                    ]
                },
            }
        )
        self.pause(0.4)

    def move(self, x: int, y: int) -> None:
        abs_x, abs_y = to_abs(x, y)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "abs", "data": {"axis": "x", "value": abs_x}},
                        {"type": "abs", "data": {"axis": "y", "value": abs_y}},
                    ]
                },
            }
        )
        self.pause(0.08)

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int = 12) -> None:
        abs_x, abs_y = to_abs(x0, y0)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "abs", "data": {"axis": "x", "value": abs_x}},
                        {"type": "abs", "data": {"axis": "y", "value": abs_y}},
                        {"type": "btn", "data": {"down": True, "button": "left"}},
                    ]
                },
            }
        )
        self.pause(0.15)
        for step in range(1, steps + 1):
            x = x0 + (x1 - x0) * step // steps
            y = y0 + (y1 - y0) * step // steps
            self.move(x, y)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "btn", "data": {"down": False, "button": "left"}},
                    ]
                },
            }
        )
        self.pause(0.35)

    def type_text(self, text: str) -> None:
        qcodes = {
            " ": "spc",
            "\n": "ret",
            ".": "dot",
            ",": "comma",
            "-": "minus",
            "!": "1",
        }
        for character in text:
            if character.lower() in "abcdefghijklmnopqrstuvwxyz":
                keys = [{"type": "qcode", "data": character.lower()}]
                if character.isupper():
                    keys = [
                        {"type": "qcode", "data": "shift"},
                        {"type": "qcode", "data": character.lower()},
                    ]
                self._qmp({"execute": "send-key", "arguments": {"keys": keys}})
            elif character == "!":
                self._qmp(
                    {
                        "execute": "send-key",
                        "arguments": {
                            "keys": [
                                {"type": "qcode", "data": "shift"},
                                {"type": "qcode", "data": "1"},
                            ]
                        },
                    }
                )
            elif character in qcodes:
                self._qmp(
                    {
                        "execute": "send-key",
                        "arguments": {
                            "keys": [{"type": "qcode", "data": qcodes[character]}]
                        },
                    }
                )
            self.pause(0.06)

    def close(self) -> None:
        self._qmp({"execute": "quit"})
        self.proc.wait(timeout=10)


def note(issue: str) -> None:
    ISSUES.append(issue)
    print(f"ISSUE: {issue}")


def main() -> int:
    if not KERNEL.exists():
        print("Build the kernel first: make")
        return 1

    qemu = QemuSession()
    try:
        qemu.pause(2.0)
        qemu.capture("01_boot_desktop")

        # SYSTEM desktop icon should feel interactive but does nothing.
        qemu.click(50, 80)
        qemu.capture("02_system_icon_dead")
        note(
            "Desktop SYSTEM icon is drawn but not clickable; only SCRIPT/NOTES "
            "and Harvester NOTES open Script."
        )

        # Open Script via desktop NOTES.
        qemu.click(560, 410)
        qemu.capture("03_script_opened")

        qemu.type_text("\n\nhello from the demo!")
        qemu.capture("04_typed_into_script")

        # File → Save
        qemu.click(170, 12)
        qemu.capture("05_file_menu")
        qemu.click(202, 35)
        qemu.capture("06_saved")

        # View → Acknowledgment
        qemu.click(225, 12)
        qemu.capture("07_view_menu")
        qemu.click(281, 41)
        qemu.capture("08_acknowledgment")
        qemu.click(402, 318)
        qemu.capture("09_ack_dismissed")

        # Drag Script window (with button held).
        qemu.drag(300, 120, 430, 200)
        qemu.capture("10_script_dragged")

        # Close Harvester while Script is open — no way back.
        qemu.click(76, 71)
        qemu.capture("11_harvester_closed")
        qemu.click(50, 80)
        qemu.capture("12_cannot_reopen_harvester")
        note(
            "Closing Harvester while Script is open permanently hides Harvester; "
            "no desktop affordance reopens it."
        )

        # File → Close Script
        qemu.click(170, 12)
        qemu.capture("13_file_menu_close")
        qemu.click(202, 63)
        qemu.capture("14_script_closed_via_menu")
        note(
            "After Harvester was closed, File→Close leaves only desktop icons; "
            "Harvester stays missing for the session."
        )

        # Reopen Script, then try closing Harvester alone after restoring it
        # by... we can't restore. Boot a fresh observation via close box on
        # the only remaining window path: open Script again, then note alone.
        qemu.click(560, 410)
        qemu.capture("15_script_only_world")

        # Close Script via close box — forces Harvester visible again.
        # Script frame starts at 190,112; close box at +10,+9.
        qemu.click(200, 121)
        qemu.capture("16_script_closebox_restores_harvester")
        note(
            "Closing the last Script window forcibly resurrects Harvester even "
            "if the user previously closed it — inconsistent window lifecycle."
        )

        # Close Harvester when it is alone — it immediately reappears.
        qemu.click(76, 71)
        qemu.capture("17_harvester_refuses_to_close")
        note(
            "Harvester close box cannot dismiss the last window: the desktop "
            "forces Harvester visible again, so the close control is a no-op."
        )

        # Harvester SYSTEM row looks like an item but only NOTES opens Script.
        qemu.click(120, 170)
        qemu.capture("18_harvester_system_row")
        note(
            "Inside Harvester, the SYSTEM row is decorative only; NOTES is the "
            "sole actionable list item, which mismatches the Macintosh Finder "
            "expectation that icons launch."
        )

        # File → Close while Harvester is front does nothing useful.
        qemu.click(170, 12)
        qemu.pause(0.2)
        qemu.click(202, 63)
        qemu.capture("19_file_close_on_harvester")
        note(
            "File→Close only closes Script when Script is frontmost; with "
            "Harvester front, Close is a silent no-op despite remaining in menu."
        )

        # Branding / naming inconsistencies visible on screen.
        note(
            "Naming drift: desktop icon says NOTES but opens Script; Harvester "
            "NOTES likewise opens Script; menu brand flips between DIGITAL CAVIAR "
            "and SCRIPT."
        )
        note(
            "Dirty-document title uses 'SCRIPT -' rather than a conventional "
            "Macintosh dirty marker (bullet/diamond), which reads as a truncated "
            "title."
        )
        note(
            "scripts/record_demo.py drag() only moves the pointer without holding "
            "the mouse button, so the shipped demo never actually demonstrates "
            "window dragging."
        )

        qemu.capture("20_final_desktop")
    finally:
        qemu.close()

    report = ROOT / "ISSUES.md"
    lines = [
        "# Issues found during graphical walkthrough",
        "",
        "Captured while navigating Digital Caviar OS (Macintosh System Software–inspired desktop) via QEMU tablet/keyboard input.",
        "",
    ]
    for index, issue in enumerate(ISSUES, start=1):
        lines.append(f"{index}. {issue}")
    lines.append("")
    lines.append("Evidence frames: `demo/probe_frames/`.")
    lines.append("")
    report.write_text("\n".join(lines))
    print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
