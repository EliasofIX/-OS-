# Digital Caviar [OS]

An experimental ARM64 operating system exploring the interaction model of the
original Macintosh System Software — including the Write / Paint / clipboard
experience Steve Jobs and the Mac team showcased at the Boston Computer Society
in January 1984 — through the Digital Caviar design language.

Version 1.1 boots directly into a 640×480 graphical desktop with:

- Harvester (Finder-like launcher), Script (MacWrite-class editor), and Paint;
- Chicago / Geneva / London bitmap fonts with bold and underline styles;
- 12 / 18 point sizing, text selection, and WYSIWYG-ish document rendering;
- Paint tools (pencil, eraser, line, rect, oval, select) and fill patterns;
- a shared clipboard that pastes pictures into Script documents;
- overlapping draggable windows, striped title bars, and global menus
  (File, Edit, Font, Style, Size, View);
- Bayer 8×8 ordered-dither shadows and a restrained film-grain desktop; and
- crash-resistant two-slot persistence for the Script document text.

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

Record the BCS-parity narrated walkthrough (Paint → Copy → Script fonts → Paste)
to `/opt/cursor/artifacts/bcs-macintosh-walkthrough.mp4`:

```sh
make bcs-walkthrough
```

## Using the desktop

- Click desktop icons or Harvester rows to open **Script** or **Paint**.
- Drag a title bar to move a window; click the close box to dismiss it.
- In **Script**: type, drag to select, then use **Font / Style / Size**.
- In **Paint**: pick a tool, draw, use **SELECT**, then **Edit → Copy**.
- In **Script**: **Edit → Paste** to embed the picture in the document.
- **File → Save** persists Script text; **View → Acknowledgment** shows About.

The implementation is intentionally a focused single-user desktop rather than
a Unix-compatible general-purpose system. UART remains available for kernel
diagnostics.
