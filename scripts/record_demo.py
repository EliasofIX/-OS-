#!/usr/bin/env python3
"""Record a scripted Digital Caviar OS demonstration via QEMU QMP."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "demo" / "frames"
VIDEO = Path("/opt/cursor/artifacts/digital-caviar-demo.mp4")
DISK = ROOT / "demo" / "document.raw"
KERNEL = ROOT / "kernel.elf"
WIDTH = 640
HEIGHT = 480


def to_abs(x: int, y: int) -> tuple[int, int]:
    return (
        max(0, min(32767, x * 32767 // (WIDTH - 1))),
        max(0, min(32767, y * 32767 // (HEIGHT - 1))),
    )


class QemuDemo:
    def __init__(self) -> None:
        DISK.parent.mkdir(parents=True, exist_ok=True)
        if not DISK.exists() or DISK.stat().st_size == 0:
            DISK.write_bytes(b"\0" * (1024 * 1024))
        self.frame_index = 0
        FRAMES.mkdir(parents=True, exist_ok=True)
        for old in FRAMES.glob("frame_*.ppm"):
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
        self._read_greeting()
        self._qmp({"execute": "qmp_capabilities"})

    def _read_greeting(self) -> None:
        assert self.proc.stdout
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("QEMU QMP did not respond")

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

    def capture(self, repeats: int = 1) -> None:
        for _ in range(repeats):
            path = FRAMES / f"frame_{self.frame_index:04d}.ppm"
            self._qmp(
                {
                    "execute": "human-monitor-command",
                    "arguments": {"command-line": f"screendump {path}"},
                }
            )
            self.frame_index += 1
            self.pause(0.35)

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
        self.pause(0.45)

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
        self.pause(0.15)

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int = 8) -> None:
        for step in range(steps + 1):
            x = x0 + (x1 - x0) * step // steps
            y = y0 + (y1 - y0) * step // steps
            self.move(x, y)
        self.pause(0.2)

    def type_text(self, text: str) -> None:
        qcodes = {
            " ": "spc",
            "\n": "ret",
            ".": "dot",
            ",": "comma",
            "-": "minus",
        }
        for character in text:
            if character.lower() in "abcdefghijklmnopqrstuvwxyz":
                code = character.lower()
            elif character in qcodes:
                code = qcodes[character]
            else:
                continue
            self._qmp(
                {
                    "execute": "send-key",
                    "arguments": {"keys": [{"type": "qcode", "data": code}]},
                }
            )
            self.pause(0.08)

    def close(self) -> None:
        self._qmp({"execute": "quit"})
        self.proc.wait(timeout=10)


def build_video() -> None:
    VIDEO.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-framerate",
            "2",
            "-i",
            str(FRAMES / "frame_%04d.ppm"),
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-movflags",
            "+faststart",
            str(VIDEO),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def main() -> int:
    if not KERNEL.exists():
        print("Build the kernel first: make", file=sys.stderr)
        return 1

    demo = QemuDemo()
    try:
        demo.pause(2.0)
        demo.capture(3)

        demo.click(130, 190)
        demo.capture(2)

        demo.type_text("Digital Caviar\n\nA quiet machine.\n")
        demo.capture(2)

        demo.click(170, 12)
        demo.capture(1)
        demo.click(202, 35)
        demo.capture(3)

        demo.click(225, 12)
        demo.capture(1)
        demo.click(281, 41)
        demo.capture(3)
        demo.click(402, 318)
        demo.capture(2)

        demo.move(300, 130)
        demo.drag(300, 130, 360, 170)
        demo.capture(2)

        demo.click(560, 410)
        demo.capture(2)
    finally:
        demo.close()

    build_video()
    print(VIDEO)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
