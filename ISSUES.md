# Issues found during graphical walkthrough

Captured while navigating Digital Caviar OS (Macintosh System Software–inspired
desktop) via QEMU tablet/keyboard input and a narrated Steve Jobs–style demo.

Evidence frames: `demo/probe_frames/`.
Walkthrough video: `/opt/cursor/artifacts/steve-jobs-macintosh-walkthrough.mp4`.

## Functional / interaction

1. **Dead desktop SYSTEM icon** — The SYSTEM folder is drawn on the desktop but
   has no hit target. Only the SCRIPT icon, NOTES icon, and Harvester’s NOTES
   row open Script.
2. **Decorative Harvester SYSTEM row** — Inside Harvester, SYSTEM looks like a
   Finder item but does nothing; NOTES is the only actionable row.
3. **Harvester cannot be dismissed as the last window** — Clicking Harvester’s
   close box when it is the sole window immediately forces it visible again, so
   the close control is a no-op.
4. **Harvester can be lost for the session** — Closing Harvester while Script is
   open hides it permanently; nothing on the desktop reopens Harvester.
5. **Asymmetric close lifecycle** — Closing the last Script window forcibly
   resurrects Harvester even if the user had closed it earlier.
6. **File → Close is context-silent** — Close only affects Script when Script is
   frontmost. With Harvester front, the menu item remains but does nothing.
7. **Mid-word text wrap in Script** — `draw_document` wraps on pixel width, so
   words such as `INSANELY` split across lines with no hyphenation
   (`IN` / `SANELY`).

## Naming / Macintosh fidelity

8. **NOTES vs Script naming drift** — Desktop and Harvester label the launcher
   **NOTES**, but the application window is **SCRIPT**.
9. **Dirty-document title marker** — An unsaved Script window uses the title
   `SCRIPT -`, which reads like a truncated string rather than a Macintosh-style
   dirty bullet/diamond.
10. **Menu brand flip** — The leftmost menu title switches between
    `DIGITAL CAVIAR` and `SCRIPT` depending on the front app (classic Mac did
    this, but here the brand mark disappears entirely while editing).

## Tooling

11. **`record_demo.py` drag was broken** — The original `drag()` only moved the
    pointer without holding the mouse button. Fixed in this branch; the Jobs
    walkthrough also uses a correct press-move-release drag.

## Observed during the Jobs walkthrough (not defects)

- Single-click opens documents (no double-click required).
- Overlapping windows, File → Save status, View → Acknowledgment, and real
  title-bar dragging work when exercised with a held mouse button.
