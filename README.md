# Digital Caviar [OS]

An experimental ARM64 operating system exploring the interaction model of the
original Macintosh System Software through the Digital Caviar design language.

The current `0.1 Foundation` milestone boots directly into a 640×480 graphical
composition with:

- intentional negative space and asymmetric anchors;
- opaque light/dark masses instead of simulated glass;
- one deliberate break in the layout grid;
- Bayer 8×8 ordered-dither shadows;
- a restrained, deterministic film-grain texture; and
- a serial diagnostic channel for bring-up.

This is a graphical foundation, not yet a desktop environment. Input, movable
windows, persistent files, and applications are subsequent milestones.

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
The kernel expects QEMU's `virt` machine and its `ramfb` device.

To leave QEMU while using `-serial mon:stdio`, press `Ctrl+A`, then `X`.

## Direction

The intended path is:

1. graphics primitives and reproducible QEMU boot;
2. pointer/keyboard input and an event queue;
3. overlapping windows, menus, and calm critically damped motion;
4. opaque controls and dialogs;
5. persistent storage;
6. Harvester, Script, and a small cooperative application model.

UART remains available throughout for kernel diagnostics.
