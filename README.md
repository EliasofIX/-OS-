# Digital Caviar [OS]

An experimental ARM64 operating system exploring the interaction model of the
original Macintosh System Software through the Digital Caviar design language.

Version 1.0 boots directly into a 640×480 graphical desktop with:

- intentional negative space and asymmetric anchors;
- opaque light/dark masses instead of simulated glass;
- one deliberate break in the layout grid;
- Bayer 8×8 ordered-dither shadows;
- a restrained, deterministic film-grain texture; and
- a serial diagnostic channel for bring-up;
- virtio tablet and keyboard input;
- overlapping, draggable application windows and global menus;
- Harvester, Script, and Acknowledgment experiences; and
- crash-resistant two-slot persistence for the Script document.

## Requirements

- Clang with the AArch64 bare-metal target
- LLD
- `qemu-system-aarch64` with the SeaBIOS ramfb option ROM

On Debian or Ubuntu:

```sh
sudo apt-get install clang lld qemu-system-arm seabios
```

## Build and run

```sh
make
make run
```

QEMU opens a display window and keeps diagnostics on the launching terminal.
The kernel targets QEMU's ARM64 `virt` machine. The generated `document.raw`
disk retains Script text between launches.

To leave QEMU while using `-serial mon:stdio`, press `Ctrl+A`, then `X`.

## Demo video

Record a scripted demonstration to `/opt/cursor/artifacts/digital-caviar-demo.mp4`:

```sh
make demo-video
```

The capture walks through boot, opening Script, typing, saving, the acknowledgment dialog, window drag, and the desktop Notes icon.

Record a narrated Steve Jobs–style walkthrough to
`/opt/cursor/artifacts/steve-jobs-macintosh-walkthrough.mp4`:

```sh
make walkthrough
```

Probe the desktop for interaction issues and write `ISSUES.md`:

```sh
make probe-issues
```

## Using the desktop

- Click and drag a title bar to move its window.
- Click a window to bring its application forward.
- Open **Notes** in Harvester to launch Script.
- Type directly in Script.
- Choose **File → Save** to persist the document.
- Choose **View → Acknowledgment** for system information.

The implementation is intentionally a focused single-user desktop rather than
a Unix-compatible general-purpose system. UART remains available for kernel
diagnostics.
