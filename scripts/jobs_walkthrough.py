#!/usr/bin/env python3
"""Steve Jobs–style walkthrough of Digital Caviar OS with narrated video."""

from __future__ import annotations

import json
import subprocess
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "demo" / "walkthrough_frames"
AUDIO_DIR = ROOT / "demo" / "narration"
DISK = ROOT / "demo" / "walkthrough.raw"
KERNEL = ROOT / "kernel.elf"
VIDEO = Path("/opt/cursor/artifacts/steve-jobs-macintosh-walkthrough.mp4")
WIDTH = 640
HEIGHT = 480

# Spoken in a deliberate, presentational cadence (Jobs-inspired, not impersonation).
NARRATION = [
    (
        "intro",
        "Today... I want to show you something extraordinary. "
        "A computer. For the rest of us.",
    ),
    (
        "boot",
        "When it wakes, you do not face a blank command line. "
        "You face a desktop. Icons. Windows. A mouse.",
    ),
    (
        "harvester",
        "This is Harvester — your home for documents and the system itself. "
        "Simple. Quiet. Intentional.",
    ),
    (
        "notes",
        "And here — Notes. Double click? No. One click. "
        "Script opens, ready for your words.",
    ),
    (
        "typing",
        "You type. The machine listens. What you see is what you get.",
    ),
    (
        "menus",
        "Pull down File. Save. Your work persists. "
        "Pull down View. Acknowledgment. The system introduces itself.",
    ),
    (
        "drag",
        "And of course — you move windows. Freely. Overlapping. "
        "Just as you would papers on a desk.",
    ),
    (
        "close",
        "Close what you do not need. The desktop remains. "
        "This is personal computing.",
    ),
    (
        "outro",
        "One more thing. It is still early. You will find rough edges. "
        "That is how great products begin.",
    ),
]


def to_abs(x: int, y: int) -> tuple[int, int]:
    return (
        max(0, min(32767, x * 32767 // (WIDTH - 1))),
        max(0, min(32767, y * 32767 // (HEIGHT - 1))),
    )


def wav_duration(path: Path) -> float:
    with wave.open(str(path), "rb") as handle:
        return handle.getnframes() / float(handle.getframerate())


def synthesize_narration() -> list[tuple[str, Path, float]]:
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    clips: list[tuple[str, Path, float]] = []
    for key, text in NARRATION:
        path = AUDIO_DIR / f"{key}.wav"
        subprocess.run(
            [
                "espeak-ng",
                "-v",
                "en-us",
                "-s",
                "135",
                "-p",
                "35",
                "-a",
                "140",
                "-w",
                str(path),
                text,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        clips.append((key, path, wav_duration(path)))
    return clips


class QemuDemo:
    def __init__(self) -> None:
        DISK.parent.mkdir(parents=True, exist_ok=True)
        DISK.write_bytes(b"\0" * (1024 * 1024))
        FRAMES.mkdir(parents=True, exist_ok=True)
        for old in FRAMES.glob("frame_*.ppm"):
            old.unlink()
        self.frame_index = 0
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
        import time

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
            self.pause(0.28)

    def hold_for(self, seconds: float, fps: float = 2.5) -> None:
        frames = max(1, int(seconds * fps))
        self.capture(frames)

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

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int = 14) -> None:
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
        self.pause(0.12)
        for step in range(1, steps + 1):
            x = x0 + (x1 - x0) * step // steps
            y = y0 + (y1 - y0) * step // steps
            self.move(x, y)
            if step % 2 == 0:
                self.capture(1)
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
        self.pause(0.3)

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
                keys = [{"type": "qcode", "data": character.lower()}]
                if character.isupper():
                    keys = [
                        {"type": "qcode", "data": "shift"},
                        {"type": "qcode", "data": character.lower()},
                    ]
                self._qmp({"execute": "send-key", "arguments": {"keys": keys}})
            elif character in qcodes:
                self._qmp(
                    {
                        "execute": "send-key",
                        "arguments": {
                            "keys": [{"type": "qcode", "data": qcodes[character]}]
                        },
                    }
                )
            else:
                continue
            self.pause(0.07)
            if character in " .\n":
                self.capture(1)

    def close(self) -> None:
        self._qmp({"execute": "quit"})
        self.proc.wait(timeout=10)


def make_title_card(path: Path, lines: list[str]) -> None:
    """Write a simple PPM title card without external image libs."""
    width, height = WIDTH, HEIGHT
    bg = (12, 12, 14)
    fg = (232, 228, 220)
    accent = (183, 156, 118)
    pixels = [bg] * (width * height)

    def plot(x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < width and 0 <= y < height:
            pixels[y * width + x] = color

    # Minimal 5x7 bitmap for A-Z 0-9 space . ,
    glyphs = {
        "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
        "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
        "C": ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
        "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
        "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
        "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
        "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
        "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
        "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
        "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
        "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
        "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
        "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
        "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
        "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
        "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
        "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
        "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
        "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
        "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
        "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
        "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
        "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
        "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
        "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
        "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
        "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
        "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
        "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
        "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
        "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
        "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
        "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
        "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
        "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
        "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
        " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
        ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
        ",": ["00000", "00000", "00000", "00000", "01100", "00100", "01000"],
        "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
        "'": ["00100", "00100", "00010", "00000", "00000", "00000", "00000"],
        "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
        ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
        "/": ["00001", "00010", "00100", "01000", "10000", "00000", "00000"],
    }

    def draw_text(text: str, cx: int, cy: int, scale: int, color: tuple[int, int, int]) -> None:
        glyph_w = 6 * scale
        total = len(text) * glyph_w
        x0 = cx - total // 2
        for index, ch in enumerate(text.upper()):
            rows = glyphs.get(ch, glyphs[" "])
            for row, bits in enumerate(rows):
                for col, bit in enumerate(bits):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                plot(
                                    x0 + index * glyph_w + col * scale + dx,
                                    cy + row * scale + dy,
                                    color,
                                )

    # Accent rule
    for x in range(120, 520):
        for y in range(88, 91):
            plot(x, y, accent)

    draw_text(lines[0], width // 2, 120, 3, fg)
    for i, line in enumerate(lines[1:]):
        draw_text(line, width // 2, 200 + i * 36, 2, fg if i == 0 else (176, 173, 166))

    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        handle.write(bytes(c for pixel in pixels for c in pixel))


FPS = 2.5


def frames_for(seconds: float) -> int:
    return max(1, int(round(seconds * FPS)))


def build_video(clips: list[tuple[str, Path, float]]) -> None:
    VIDEO.parent.mkdir(parents=True, exist_ok=True)
    concat_audio = AUDIO_DIR / "full_narration.wav"
    list_file = AUDIO_DIR / "list.txt"
    list_file.write_text("".join(f"file '{path}'\n" for _, path, _ in clips))
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-f",
            "concat",
            "-safe",
            "0",
            "-i",
            str(list_file),
            "-c",
            "copy",
            str(concat_audio),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    total_audio = sum(duration for _, _, duration in clips)
    frame_count = len(list(FRAMES.glob("frame_*.ppm")))
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-framerate",
            str(FPS),
            "-i",
            str(FRAMES / "frame_%04d.ppm"),
            "-i",
            str(concat_audio),
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-c:a",
            "aac",
            "-shortest",
            "-movflags",
            "+faststart",
            str(VIDEO),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    print(f"frames={frame_count} fps={FPS} audio={total_audio:.1f}s")
    print(VIDEO)


def seed_title_cards(seconds: float) -> int:
    make_title_card(
        FRAMES / "frame_0000.ppm",
        ["DIGITAL CAVIAR", "A computer for the rest of us", "System Software 1.0"],
    )
    make_title_card(
        FRAMES / "frame_0001.ppm",
        ["JANUARY 1984", "Cupertino, California", "Macintosh introduction"],
    )
    total = frames_for(seconds)
    half = max(1, total // 2)
    for index in range(total):
        src = FRAMES / ("frame_0000.ppm" if index < half else "frame_0001.ppm")
        dst = FRAMES / f"frame_{index:04d}.ppm"
        if index >= 2:
            dst.write_bytes(src.read_bytes())
    return total


def main() -> int:
    if not KERNEL.exists():
        print("Build the kernel first: make")
        return 1

    clips = synthesize_narration()
    durations = {key: duration for key, _, duration in clips}

    demo = QemuDemo()
    demo.frame_index = seed_title_cards(durations["intro"])
    try:
        demo.pause(2.0)

        # boot — desktop reveal
        demo.hold_for(durations["boot"], fps=FPS)

        # harvester — point around the home window
        demo.move(180, 180)
        demo.capture(2)
        demo.move(140, 230)
        demo.hold_for(max(0.8, durations["harvester"] - 1.0), fps=FPS)

        # notes — open Script from NOTES
        demo.move(560, 410)
        demo.capture(2)
        demo.click(560, 410)
        demo.hold_for(max(0.8, durations["notes"] - 1.2), fps=FPS)

        # typing
        before = demo.frame_index
        demo.type_text("Hello. Insanely great.\n")
        typed = demo.frame_index - before
        remain = frames_for(durations["typing"]) - typed
        if remain > 0:
            demo.capture(remain)

        # menus — File/Save then View/Acknowledgment, paced to narration
        menu_budget = frames_for(durations["menus"])
        start = demo.frame_index
        demo.click(170, 12)
        demo.capture(2)
        demo.click(202, 35)  # SAVE
        demo.capture(3)
        demo.click(225, 12)
        demo.capture(2)
        demo.click(281, 41)  # ACKNOWLEDGMENT
        demo.capture(4)
        demo.click(402, 318)  # DISMISS
        used = demo.frame_index - start
        if menu_budget > used:
            demo.capture(menu_budget - used)

        # drag
        drag_budget = frames_for(durations["drag"])
        start = demo.frame_index
        demo.drag(300, 120, 420, 180)
        used = demo.frame_index - start
        if drag_budget > used:
            demo.capture(drag_budget - used)

        # close via File → Close
        close_budget = frames_for(durations["close"])
        start = demo.frame_index
        demo.click(170, 12)
        demo.capture(2)
        demo.click(202, 63)
        used = demo.frame_index - start
        if close_budget > used:
            demo.capture(close_budget - used)

        # outro — show a rough edge, then recover
        outro_budget = frames_for(durations["outro"])
        start = demo.frame_index
        demo.click(50, 80)  # dead SYSTEM icon
        demo.capture(3)
        demo.click(560, 410)  # NOTES still works
        demo.capture(3)
        used = demo.frame_index - start
        if outro_budget > used:
            demo.capture(outro_budget - used)
    finally:
        demo.close()

    build_video(clips)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
