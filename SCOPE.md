# Scope

bereanOS is a study device, not a general e-reader and not a tablet. It exists to make the Bible and
the JW study publications fast to reach, fast to read, and trustworthy to mark up on hardware with
one panel, three buttons and a slow refresh.

Every decision below follows from that. This document is the gate: if a proposal is not in section 2,
it needs an argument against section 3, not enthusiasm.

## 1. The device

One board: the Xteink X4 Pro. ESP32-S3, 8 MB PSRAM, 16 MB flash, an 800x480 one-bit e-ink panel with
no grayscale, GT911 touch, and Left / Right / Power plus a capacitive Home key.

That is the whole target. Portability to other ESP32 e-readers is not a goal; the fork gave that up
deliberately, and code that only exists to keep another board buildable is dead weight.

## 2. In scope

- **The Bible as the centre of the device.** Book, chapter and verse navigation, cross-references,
  and returning to where you jumped from.
- **The two weekly meeting publications**, downloaded automatically.
- **Browsing and downloading any publication from the jw.org catalog.**
- **Tagging passages, and managing tags globally.** A tag lives at the device level, not inside one
  publication, and "show me everything tagged X" is a first-class query.
- **Reading quality** on this panel: typography, hyphenation, pagination, refresh behaviour,
  legibility.
- **Settings** for the choices that genuinely differ between readers.
- **Memory, flash and code quality.** A refactor that reduces resource use or removes a class of bug
  is in scope with no user-visible feature attached.

## 3. Out of scope

Each of these was considered and rejected. The reason matters more than the verdict, because it is
what a future proposal has to argue against.

- **Notes.** Typed notes need a keyboard this device does not have and a synchronisation story it
  does not want. Excluding them means the only text ever typed is a tag name and a Wi-Fi password,
  which the existing soft keyboard already handles.
- **JW Library interop.** The device is standalone and is the source of truth for its own study
  data. Tracking a third-party schema as it moves would put every mark a user makes at the mercy of
  someone else's release.
- **On-device full-text search.** Search means the publication catalog, not publication content.
  Indexing the content of a 100 MB publication on this hardware costs minutes of SD I/O and a format
  nobody can rebuild if it corrupts.
- **A general e-reader.** The OPDS browser, KOReader sync, the dictionary, and the TXT and XTC
  readers were removed in the fork and are not coming back. If you want those, CrossPoint still
  exists and is better at them.
- **Interactive apps.** No games, calculators, notepads, timers or mini-apps.
- **Active connectivity.** No RSS, no news, no browser, no background polling. Wi-Fi comes up for a
  download or an update, and goes away again.
- **PDF rendering.** Fixed-layout pages mean panning and zooming, which is a poor reading experience
  on e-ink at any price.
- **Portability to other boards.** See section 1.

## 4. Deferred, not rejected

- **BLE keyboard.** Wanted, but a NimBLE stack plus controller is a flash cost nothing has budgeted
  yet. It needs a measured budget before it needs a design.
- **Drag-to-select.** This unit's panel is one-bit with no windowed update, so the only drag feedback
  available is full-frame inversion — several hundred milliseconds behind the finger. Long-press to
  anchor plus a tap to finish pays two refreshes instead of one per finger sample. Revisit if
  windowed update lands.

## 5. Constraints that decide the borderline cases

**PSRAM is a licence to budget differently, not to stop budgeting.** The S3's PSRAM is on an external
SPI bus: roughly an order of magnitude slower than internal SRAM, unusable from an ISR, unusable
while the flash cache is suspended, and DMA-constrained.

| Belongs in PSRAM | Belongs in internal SRAM |
|---|---|
| The catalog index while search is open | The framebuffer |
| Unit index pages being built or queried | Selection geometry and the render hot path |
| Download and inflate buffers | ISR state, anything `IRAM_ATTR` touches |

Internal SRAM stays the ~380 KB-class resource CrossPoint treated it as.

**Study data is the only thing on this device that cannot be re-downloaded.** Every store this
project adds writes atomically, checks an explicit serialised-byte budget before writing rather than
truncating, refuses a format version it does not understand rather than reinterpreting it, and
streams anything that can exceed ~40 KB instead of reading it whole. A feature that cannot meet that
bar does not ship.

**Every screen costs a full panel refresh.** A flow that adds a screen to a common path is more
expensive than it looks, and an interaction that repaints per finger sample is not available.

**A new setting is not free.** It is a field to persist, migrate, validate, translate and render,
plus the combinations every future change has to keep working. Add one when readers genuinely differ;
otherwise pick a good default.

## 6. Proposing something

Say what reading or study problem it solves, why an existing screen or setting cannot, and what it
costs in RAM, flash and refreshes. "Don't know" is an acceptable answer to the cost question; not
asking it is not.
