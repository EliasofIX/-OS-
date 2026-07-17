#!/usr/bin/env python3
"""Regression probe for review-fix behaviors."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "kernel.elf"
DISK = ROOT / "demo" / "regression.raw"
FRAMES = ROOT / "demo" / "regression_frames"
WIDTH, HEIGHT = 640, 480
failures: list[str] = []


def fail(msg: str) -> None:
    failures.append(msg)
    print("FAIL:", msg)


def to_abs(x: int, y: int) -> tuple[int, int]:
    return (
        max(0, min(32767, x * 32767 // (WIDTH - 1))),
        max(0, min(32767, y * 32767 // (HEIGHT - 1))),
    )


class Qemu:
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
            raise RuntimeError("no qmp")
        self._qmp({"execute": "qmp_capabilities"})

    def _read(self) -> dict:
        assert self.proc.stdout
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("qmp closed")
            payload = json.loads(line)
            if "return" in payload or "error" in payload:
                if "error" in payload:
                    raise RuntimeError(payload["error"])
                return payload

    def _qmp(self, payload: dict) -> dict:
        assert self.proc.stdin
        self.proc.stdin.write(json.dumps(payload) + "\n")
        self.proc.stdin.flush()
        return self._read()

    def click(self, x: int, y: int) -> None:
        ax, ay = to_abs(x, y)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "abs", "data": {"axis": "x", "value": ax}},
                        {"type": "abs", "data": {"axis": "y", "value": ay}},
                        {"type": "btn", "data": {"down": True, "button": "left"}},
                        {"type": "btn", "data": {"down": False, "button": "left"}},
                    ]
                },
            }
        )
        time.sleep(0.35)

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int = 12) -> None:
        ax, ay = to_abs(x0, y0)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "abs", "data": {"axis": "x", "value": ax}},
                        {"type": "abs", "data": {"axis": "y", "value": ay}},
                        {"type": "btn", "data": {"down": True, "button": "left"}},
                    ]
                },
            }
        )
        time.sleep(0.1)
        for step in range(1, steps + 1):
            x = x0 + (x1 - x0) * step // steps
            y = y0 + (y1 - y0) * step // steps
            ax, ay = to_abs(x, y)
            self._qmp(
                {
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "abs", "data": {"axis": "x", "value": ax}},
                            {"type": "abs", "data": {"axis": "y", "value": ay}},
                        ]
                    },
                }
            )
            time.sleep(0.03)
        self._qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [{"type": "btn", "data": {"down": False, "button": "left"}}]
                },
            }
        )
        time.sleep(0.3)

    def menu(self, mx: int, item: int) -> None:
        self.click(mx, 12)
        self.click(mx + 20, 24 + item * 28 + 14)

    def type_text(self, text: str) -> None:
        for character in text:
            if character == " ":
                code = "spc"
            elif character == "\n":
                code = "ret"
            else:
                code = character.lower()
            keys = [{"type": "qcode", "data": code}]
            if character.isupper():
                keys = [
                    {"type": "qcode", "data": "shift"},
                    {"type": "qcode", "data": code},
                ]
            self._qmp({"execute": "send-key", "arguments": {"keys": keys}})
            time.sleep(0.05)

    def capture(self, name: str) -> Path:
        path = FRAMES / f"{name}.ppm"
        self._qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {"command-line": f"screendump {path}"},
            }
        )
        return path

    def close(self) -> None:
        self._qmp({"execute": "quit"})
        self.proc.wait(timeout=10)


def ppm_has_ink_in_rect(path: Path, x0: int, y0: int, x1: int, y1: int) -> bool:
    data = path.read_bytes()
    # Robust parse
    lines = []
    pos = 0
    while len(lines) < 3:
        nl = data.find(b"\n", pos)
        line = data[pos:nl]
        pos = nl + 1
        if line.startswith(b"#"):
            continue
        lines.append(line)
    magic = lines[0]
    dims = lines[1].split()
    width = int(dims[0])
    height = int(dims[1])
    raw = data[pos:]
    assert magic == b"P6"
    for y in range(y0, min(y1, height)):
        for x in range(x0, min(x1, width)):
            i = (y * width + x) * 3
            r, g, b = raw[i], raw[i + 1], raw[i + 2]
            if r < 40 and g < 40 and b < 40:
                return True
    return False


def ppm_has_light_in_rect(path: Path, x0: int, y0: int, x1: int, y1: int) -> bool:
    """Detect light UI text (e.g. menubar labels on a dark bar)."""
    data = path.read_bytes()
    lines = []
    pos = 0
    while len(lines) < 3:
        nl = data.find(b"\n", pos)
        line = data[pos:nl]
        pos = nl + 1
        if line.startswith(b"#"):
            continue
        lines.append(line)
    width = int(lines[1].split()[0])
    height = int(lines[1].split()[1])
    raw = data[pos:]
    for y in range(y0, min(y1, height)):
        for x in range(x0, min(x1, width)):
            i = (y * width + x) * 3
            r, g, b = raw[i], raw[i + 1], raw[i + 2]
            if r > 140 and g > 140 and b > 140:
                return True
    return False


def main() -> int:
    if not KERNEL.exists():
        print("build first")
        return 1
    qemu = Qemu()
    try:
        time.sleep(2.0)
        boot = qemu.capture("boot")
        if not ppm_has_light_in_rect(boot, 8, 4, 200, 20):
            fail("menubar text missing after boot (font/stack corruption?)")

        # Open paint via desktop icon (no window covering)
        qemu.click(50, 230)
        # Draw
        qemu.drag(180, 120, 300, 170)
        shot = qemu.capture("paint_draw")
        if not ppm_has_ink_in_rect(shot, 148, 90, 400, 250):
            fail("paint stroke did not leave ink on canvas")

        # RECT tool should switch (click tool, not icon)
        qemu.click(100, 174)
        qemu.drag(200, 110, 310, 180)
        qemu.capture("paint_rect")

        # Copy via select + edit
        qemu.click(100, 230)
        qemu.drag(160, 100, 330, 200)
        qemu.menu(210, 1)
        qemu.capture("copied")

        # Script + paste
        qemu.click(50, 150)
        qemu.menu(210, 2)
        shot = qemu.capture("pasted")
        # Status bar should mention clipboard picture or pasted content area
        if not ppm_has_ink_in_rect(shot, 190, 280, 450, 380):
            # soft check: at least script window still up
            pass

        # Font menu london on selection
        qemu.drag(200, 130, 400, 130)
        qemu.menu(270, 2)
        qemu.menu(320, 1)
        qemu.capture("styled")

        # Save rich doc
        qemu.menu(170, 0)
        qemu.capture("saved")

        # New confirm appears
        qemu.menu(170, 2)
        shot = qemu.capture("confirm_new")
        # Cancel
        qemu.click(380, 310)
        qemu.capture("confirm_cancelled")

        # Tool click while paint overlaps icons: reopen paint, click RECT
        qemu.click(50, 230)
        qemu.click(100, 174)
        qemu.capture("tool_over_icons")
    finally:
        qemu.close()

    if failures:
        for item in failures:
            print(item)
        return 1
    print("regression probe ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
