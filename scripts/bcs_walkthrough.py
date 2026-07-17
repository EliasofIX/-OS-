#!/usr/bin/env python3
"""BCS-parity walkthrough: Paint, Script fonts/styles, and clipboard paste."""

from __future__ import annotations

import json
import subprocess
import time
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "demo" / "bcs_frames"
AUDIO_DIR = ROOT / "demo" / "bcs_narration"
DISK = ROOT / "demo" / "bcs_walkthrough.raw"
KERNEL = ROOT / "kernel.elf"
VIDEO = Path("/opt/cursor/artifacts/bcs-macintosh-walkthrough.mp4")
WIDTH = 640
HEIGHT = 480
FPS = 2.5

NARRATION = [
    (
        "intro",
        "Tonight, Macintosh. A computer for the rest of us. "
        "Not a command line. A desktop. Windows. A mouse.",
    ),
    (
        "paint",
        "First, pictures. Open Paint. Choose a tool. Draw. "
        "Erase. Stretch a rectangle. Just point and click.",
    ),
    (
        "copy",
        "Now the magic. Select what you drew. Edit. Copy. "
        "The picture lives on the clipboard.",
    ),
    (
        "write",
        "Open Script, our word processor. Type. Then change the font. "
        "Chicago. Geneva. London. Make it bold. Make it large.",
    ),
    (
        "paste",
        "And here is the moment. Edit. Paste. "
        "The picture appears inside the document. "
        "Words and images. Together.",
    ),
    (
        "outro",
        "Write. Paint. Cut and paste between them. "
        "Multiple fonts. Insanely great.",
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


def synthesize() -> dict[str, float]:
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    durations: dict[str, float] = {}
    for key, text in NARRATION:
        path = AUDIO_DIR / f"{key}.wav"
        subprocess.run(
            [
                "espeak-ng",
                "-v",
                "en-us",
                "-s",
                "138",
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
        durations[key] = wav_duration(path)
    return durations


def frames_for(seconds: float) -> int:
    return max(1, int(round(seconds * FPS)))


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
            self.pause(0.25)

    def hold_for(self, seconds: float) -> None:
        self.capture(frames_for(seconds))

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
        self.pause(0.05)

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int = 18) -> None:
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
            if step % 3 == 0:
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
        self.pause(0.35)

    def type_text(self, text: str) -> None:
        qcodes = {" ": "spc", "\n": "ret", ".": "dot", ",": "comma", "-": "minus"}
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
            self.pause(0.06)
            if character in " .\n":
                self.capture(1)

    def menu_item(self, menu_x: int, item: int) -> None:
        self.click(menu_x, 12)
        self.capture(1)
        self.click(menu_x + 20, 24 + item * 28 + 14)
        self.capture(2)

    def close(self) -> None:
        self._qmp({"execute": "quit"})
        self.proc.wait(timeout=10)


def make_title_card(path: Path, lines: list[str]) -> None:
    width, height = WIDTH, HEIGHT
    bg = (12, 12, 14)
    fg = (232, 228, 220)
    accent = (183, 156, 118)
    pixels = [bg] * (width * height)

    def plot(x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < width and 0 <= y < height:
            pixels[y * width + x] = color

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
        "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    }

    def draw_text(text: str, cx: int, cy: int, scale: int,
                  color: tuple[int, int, int]) -> None:
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

    for x in range(120, 520):
        for y in range(88, 91):
            plot(x, y, accent)
    draw_text(lines[0], width // 2, 120, 3, fg)
    for i, line in enumerate(lines[1:]):
        draw_text(line, width // 2, 200 + i * 36, 2, fg if i == 0 else (176, 173, 166))
    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        handle.write(bytes(c for pixel in pixels for c in pixel))


def build_video(durations: dict[str, float]) -> None:
    VIDEO.parent.mkdir(parents=True, exist_ok=True)
    list_file = AUDIO_DIR / "list.txt"
    list_file.write_text(
        "".join(f"file '{AUDIO_DIR / (key + '.wav')}'\n" for key, _ in NARRATION)
    )
    concat_audio = AUDIO_DIR / "full.wav"
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
    total = sum(durations.values())
    print(f"frames={len(list(FRAMES.glob('frame_*.ppm')))} audio={total:.1f}s")
    print(VIDEO)


def main() -> int:
    if not KERNEL.exists():
        print("Build first: make")
        return 1

    durations = synthesize()
    demo = QemuDemo()

    # Title cards for intro
    total_intro = frames_for(durations["intro"])
    make_title_card(
        FRAMES / "frame_0000.ppm",
        ["DIGITAL CAVIAR", "Write. Paint. Paste.", "BCS demo parity"],
    )
    make_title_card(
        FRAMES / "frame_0001.ppm",
        ["JANUARY 1984", "Boston Computer Society", "The Macintosh idea"],
    )
    half = max(1, total_intro // 2)
    for index in range(total_intro):
        src = FRAMES / ("frame_0000.ppm" if index < half else "frame_0001.ppm")
        dst = FRAMES / f"frame_{index:04d}.ppm"
        if index >= 2:
            dst.write_bytes(src.read_bytes())
    demo.frame_index = total_intro

    try:
        demo.pause(2.0)

        # Paint segment
        start = demo.frame_index
        demo.click(50, 230)  # desktop Paint icon
        demo.capture(3)
        demo.drag(170, 110, 300, 165)  # pencil
        demo.capture(2)
        demo.click(100, 174)  # RECT tool
        demo.capture(1)
        demo.drag(190, 120, 320, 185)
        demo.capture(2)
        used = demo.frame_index - start
        budget = frames_for(durations["paint"])
        if budget > used:
            demo.capture(budget - used)

        # Copy segment
        start = demo.frame_index
        demo.click(100, 230)  # SELECT
        demo.drag(155, 100, 340, 200)
        demo.capture(2)
        demo.menu_item(210, 1)  # Edit → Copy
        used = demo.frame_index - start
        budget = frames_for(durations["copy"])
        if budget > used:
            demo.capture(budget - used)

        # Write / fonts segment
        start = demo.frame_index
        demo.click(50, 150)  # Script
        demo.capture(2)
        demo.type_text("\nHello from Script.\n")
        demo.drag(200, 130, 400, 130)  # select a line
        demo.menu_item(270, 2)  # Font → London
        demo.menu_item(320, 1)  # Style → Bold
        demo.menu_item(370, 1)  # Size → 18
        demo.capture(2)
        demo.type_text("\nLarge London bold.\n")
        used = demo.frame_index - start
        budget = frames_for(durations["write"])
        if budget > used:
            demo.capture(budget - used)

        # Paste segment
        start = demo.frame_index
        demo.menu_item(210, 2)  # Edit → Paste
        demo.capture(4)
        used = demo.frame_index - start
        budget = frames_for(durations["paste"])
        if budget > used:
            demo.capture(budget - used)

        # Outro
        demo.menu_item(430, 0)  # View → Acknowledgment
        demo.capture(3)
        demo.click(402, 318)  # Dismiss
        demo.hold_for(durations["outro"])
    finally:
        demo.close()

    build_video(durations)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
