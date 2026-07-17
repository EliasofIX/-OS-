# Digital Caviar [OS]

An experimental bare-metal ARM64 operating system that boots directly into a
640×480 graphical desktop under QEMU's `virt` machine. See `README.md` for the
product overview and usage.

## Cursor Cloud specific instructions

This repo is a single freestanding C/assembly kernel — there is no package
manager or language runtime. The toolchain is entirely system packages
(`clang`, `lld`, `qemu-system-arm`, `seabios`), which the update script keeps
installed. `clang` cross-compiles to `aarch64-none-elf`; the host arch is x86_64.

Standard commands (already documented in `README.md` / `Makefile`):

- Build: `make` (produces `kernel.elf`; treat build warnings as errors via `-Werror`).
- Run: `make run` (opens QEMU with `-serial mon:stdio`; leave QEMU with `Ctrl+A` then `X`).
- Clean: `make clean`.
- Demo video: `make demo-video` (drives the desktop over QMP, writes
  `/opt/cursor/artifacts/digital-caviar-demo.mp4`; requires `python3` + `ffmpeg`).

Non-obvious caveats:

- There is no lint or automated test suite. `-Wall -Wextra -Werror` during
  `make` is the only static check. The `make demo-video` scripted run is the
  effective end-to-end test.
- QEMU has no framebuffer window in a headless VM. For automated/headless
  verification, run with `-display none` and either `-serial stdio` (to read the
  boot diagnostics) or drive it via QMP like `scripts/record_demo.py` and capture
  frames with `screendump`.
- `make run` is interactive and blocks on stdio. For a non-blocking boot check,
  wrap the QEMU command in `timeout` with `-display none -serial stdio`.
- `document.raw` is a generated 1 MB raw disk that persists Script text between
  launches; it is gitignored. `make demo-video` uses its own `demo/document.raw`.
- Requires the SeaBIOS ramfb option ROM (`/usr/share/seabios/vgabios-ramfb.bin`),
  pulled in by the `seabios` package, for the `-device ramfb` display.
