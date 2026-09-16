# bereanOS User Guide

This guide covers the device as the firmware behaves today: the controls, the screens, and every
setting. The product is being reshaped around four sections — Biblia, Reuniones, Buscar, Etiquetas y
ajustes — and the screens below are replaced when that lands. See [ROADMAP.md](./ROADMAP.md).

- [1. The device](#1-the-device)
- [2. Controls](#2-controls)
- [3. Power and startup](#3-power-and-startup)
- [4. Home screen](#4-home-screen)
- [5. Reading](#5-reading)
- [6. The reader menu](#6-the-reader-menu)
- [7. Bible navigation](#7-bible-navigation)
- [8. Highlights and tags](#8-highlights-and-tags)
- [9. Bookmarks and footnotes](#9-bookmarks-and-footnotes)
- [10. Meeting publications](#10-meeting-publications)
- [11. File transfer and the web interface](#11-file-transfer-and-the-web-interface)
- [12. Browsing files](#12-browsing-files)
- [13. Settings](#13-settings)
- [14. The sleep screen](#14-the-sleep-screen)
- [15. Custom fonts](#15-custom-fonts)
- [16. Firmware updates](#16-firmware-updates)
- [17. Where your data lives](#17-where-your-data-lives)
- [18. Troubleshooting](#18-troubleshooting)

## 1. The device

The Xteink X4 Pro has an 800x480 e-ink panel, a capacitive touchscreen, and a warm/cold frontlight.
Physically there are three buttons and one capacitive key:

| | |
|---|---|
| **Left** and **Right** | on the side of the device |
| **Power** | on the side, below them |
| **Home** | a capacitive key below the screen, read by the touch controller |
| **Reset** | a recessed pinhole |

There is **no Back button and no Confirm button.** Back is a gesture; Confirm is a tap, the Home
key, or the Power button when you configure it that way.

## 2. Controls

| Input | What it does |
|---|---|
| Tap | activates whatever you touched |
| Left / Right | previous / next in a list; previous / next page while reading |
| Long-press Left / Right | scroll a full page in a list; skip a chapter while reading (configurable) |
| Swipe right from the left edge | **Back** |
| Swipe down from the top edge | opens the frontlight panel |
| Swipe up from the bottom edge | Home, on devices without a Home key |
| **Home**, short press | Home screen; inside some screens it confirms instead (see below) |
| **Home**, long press | runs the **Long-press Menu** function while reading |
| **Power**, short press | configurable: ignore, sleep, page turn, refresh, footnotes, or Confirm |
| **Power**, long press | power off |

The left-edge Back swipe routes through GPIO-independent code and is checked before anything else in
every screen, so it always gets you out.

**Home is context-dependent.** Normally it takes you to the Home screen. Two screens repurpose it
because they have no other way to confirm: passage selection uses a Home tap to set each end of the
selection, and the tag picker uses it to finish. Both are described below.

### While reading

The reading surface is split into three vertical zones:

| Zone | Tap |
|---|---|
| Left third | previous page |
| Centre third | opens the reader menu |
| Right third | next page |

The centre tap is the practical way into the reader menu on this device, because the top-edge swipe
belongs to the frontlight panel. If you prefer, set **Settings -> Controls -> Long-press Menu** to
**Reader menu** and hold the Home key instead.

Touch reading controls can be set to tap, swipe, inverted tap, or off entirely
(**Settings -> Controls -> Touch reader controls**). With touch off, the reading surface ignores the
screen completely, so a stray brush cannot turn a page.

## 3. Power and startup

Hold **Power** for about half a second to turn the device on or off. A short press does whatever
**Settings -> Controls -> Short power button click** says; the default is to ignore it.

To reboot, press and release **Reset**, then press and hold **Power** for a few seconds.

On a first boot you land on the Home screen. After that the device reopens the book you were reading.

The device sleeps after the inactivity timeout set in **Settings -> System -> Time to sleep**. A
download or a firmware update in progress does not count as activity, so leave the screen awake, or
raise the timeout, while one is running.

## 4. Home screen

The Home screen shows the book you were last reading and four entries:

- **Browse files** — the SD card, folders and books
- **Recent books** — recently opened, newest first
- **File transfer** — Wi-Fi modes, including meeting publication downloads
- **Settings**

Selecting the cover resumes reading.

## 5. Reading

| Action | Input |
|---|---|
| Next page | tap the right third, or press **Right** |
| Previous page | tap the left third, or press **Left** |
| Next / previous chapter | hold **Right** / **Left** briefly, then release |
| Reader menu | tap the centre third |
| Back out of the book | swipe right from the left edge |

Long-press chapter skip can be turned off, or swapped for page scrolling, in
**Settings -> Controls -> Long-press behaviour**.

**Cross-references.** Following a link inside a publication remembers where you came from. Pressing
Back returns to that position rather than leaving the book. The return stack holds three positions.

**Auto page turn** advances pages on a timer; enable it from the reader menu.

## 6. The reader menu

Tap the centre third of the page. The menu lists, depending on the publication:

- **Select chapter** — the table of contents, or the Bible drill-down (section 7)
- **Footnotes** — the footnotes on the current page
- **Bookmarks** — jump to or delete a saved position
- **Highlights** — browse what you have marked in this publication
- **Toggle bookmark** — drop or remove a bookmark at the current page
- **Highlight passage** — start a passage selection (section 8)
- **Text settings** — font, size, spacing, margins, with a live preview
- **Night mode** — invert the page
- **Frontlight** — the light panel
- **Orientation** — rotate without leaving the book
- **Auto turn** — pages per minute
- **Go to %** — jump by percentage
- **Take screenshot** — writes a BMP to `screenshots/`
- **Go home**
- **Delete book cache** — forces a re-index of this publication on next open

Back closes the menu and returns to the page.

## 7. Bible navigation

In a Bible, **Select chapter** opens a three-level drill-down instead of the flat table of contents:

1. **Book** — a scrolling list of the 66 books.
2. **Chapter** — a paged grid of numbers. Tap one to list its verses, or confirm it to open the
   chapter.
3. **Verse** — a paged grid. Tap a verse to open the page containing it.

Number grids rather than lists, because Psalm 119 has 176 verses and a list would cost a dozen page
turns to cross.

While you are in a Bible, the status bar shows the chapter number alongside the book name.

## 8. Highlights and tags

A **highlight** marks a passage. A **tag** is a label you attach to it. Tags currently live inside
each publication's highlight file; global tags that span publications arrive in Phase 1.

### Marking a passage

1. Open the reader menu and choose **Highlight passage**. (Or set **Long-press Menu** to
   **Highlight** and hold the Home key.)
2. Tap the first word of the passage, or move the cursor with **Left** / **Right** and tap **Home**
   to confirm it.
3. Do the same for the last word.
4. Choose what to do with the selection:
   - **Highlight** saves it with no tags.
   - **Tag** opens the tag picker; tick any number of tags, then tap **Home** to apply them.
   - **Cancel** discards the selection.

Swiping Back at any point cancels the whole selection, including from the final chooser.

### Browsing what you marked

**Reader menu -> Highlights** lists this publication's highlights, newest first, with the passage
text. Activating a row jumps to it. The filter row at the top narrows the list to a single tag.

Long-pressing a row offers to delete the highlight, or to change its tags. Long-pressing a tag in
the filter list deletes that tag from the publication's palette.

## 9. Bookmarks and footnotes

**Bookmarks** are saved positions. Add one from **Reader menu -> Toggle bookmark**, or by holding the
Home key when **Long-press Menu** is set to **Bookmark**. **Reader menu -> Bookmarks** lists them;
activating a row jumps there, and a long press deletes after a confirmation.

**Footnotes** appear in the menu when the current page has any. Selecting one opens the note and
remembers where you were; Back returns. Setting **Short power button click** to **Footnotes** opens
the same list from the Power button, and jumps straight there when there is only one note on the page.

## 10. Meeting publications

**Home -> File transfer -> Meeting publications** downloads the current week's *Watchtower* study
edition and *Life and Ministry Meeting Workbook* as EPUBs onto the SD card.

The device reads its clock, works out the ISO week, fetches that week's meetings page, reads the
issue numbers out of the publication links, resolves the download URLs, and writes the files. Either
publication may be missing for a given week, which is a normal outcome and not an error.

Files already on the card with a matching checksum are not downloaded again. Where they land is set
by the download folder, which is editable from the web settings page; blank means the card root.

**Set the clock first.** The week is derived from it, so a wrong clock fetches the wrong week. The
clock lives under **Settings -> Reader -> Customise status bar**, which is also where **Sync now**
and the UTC offset are; those rows appear only on a device with a real-time clock.

## 11. File transfer and the web interface

**Home -> File transfer**, then pick a mode:

| Mode | Use when |
|---|---|
| **Join network** | the device should join your Wi-Fi |
| **Calibre wireless** | receiving books from Calibre's device plugin |
| **Meeting publications** | see section 10 |
| **Create hotspot** | there is no trusted network; the device makes its own |

In Join network mode the device tries the last network it used, then other saved networks by signal
strength, then shows the scan list. Once connected it displays the SSID, a QR code, the IP URL, and
an mDNS URL.

The web interface can upload, download, rename, move and delete files, create folders, edit most
settings, manage saved Wi-Fi networks, and install SD-card fonts. It also speaks WebDAV, so you can
mount the card as a network drive.

**There is no authentication.** Anyone on the same network can use it while it is running. Use it on
networks you trust, or in hotspot mode, and leave the screen when you are done.

Details are in [docs/webserver.md](./docs/webserver.md); the raw endpoints are in
[docs/webserver-endpoints.md](./docs/webserver-endpoints.md).

## 12. Browsing files

**Home -> Browse files** walks the SD card. The current path is shown at the top, directories appear
in brackets, and file extensions are shown.

- **Left** / **Right** move the selection; hold either to move a full page.
- Tap a row to open a folder or a book. A `.bmp` opens in the image viewer.
- Long-press a row to delete it, after a confirmation. Renaming and moving are web-interface
  operations, not device ones.

Hidden files and folders — anything starting with `.` — are shown only when
**Settings -> System -> Show hidden files** is on.

## 13. Settings

**Home -> Settings**, four tabs.

### Display

- **Sleep screen** — Dark, Light, Custom, Cover, Cover + Custom, Quick resume, Transparent, or None.
  See section 14.
- **Sleep screen cover mode** — Fit or Crop, when a cover is shown.
- **Sleep screen cover filter** — None (grayscale), Contrast, or Inverted.
- **Quick resume on timeout** — use the quick-resume sleep screen when the device sleeps on its own.
- **Hide battery %** — Never, In reader, or Always. The icon always stays.
- **Refresh frequency** — how often the panel does a full refresh while reading, to clear ghosting:
  every 1, 5, 10, 15 or 30 pages.
- **UI theme** — Classic, Lyra, Lyra Extended, or RoundedRaff.
- **Sunlight fading fix** — a software workaround for panels that fade in direct sunlight.
- **Restore light on wake** — bring the frontlight back at the brightness it had before sleep.

### Reader

- **Text settings** — font family, size, line spacing, margins and alignment, with a live preview.
- **Manage fonts** — browse and download SD-card font families over Wi-Fi.
- **Font family** — Noto Serif, Noto Sans, or any family installed on the card.
- **Font size**, **line spacing**, **screen margin**, **paragraph alignment**.
- **Embedded style** — honour the publication's own HTML and CSS.
- **Focus reading** — bold the first part of each word.
- **Hyphenation**.
- **Extra paragraph spacing** — space between paragraphs instead of a first-line indent.
- **Text anti-aliasing** — smoother edges, slightly slower page turns.
- **Images** — display, show a placeholder, or suppress.
- **Night mode** — invert the reading surface.
- **Orientation** — Portrait, Landscape CW, Inverted, or Landscape CCW.
- **Customise status bar** — what the reading status bar shows: chapter page count, book percentage,
  progress bar style and thickness, title, battery, and the clock. On a device with a real-time clock
  this is also where the clock format, the UTC offset, and **Sync now** live.

### Controls

- **Side button layout** — Prev/Next, Next/Prev, or disabled while reading.
- **Touch reader controls** — Off, Tap, Swipe, or Inverted tap.
- **Tap for reader menu** — whether a centre-third tap opens the menu.
- **Buttons follow orientation** — swap Left and Right when the screen is rotated.
- **Long-press behaviour** — what holding a page button does: nothing, chapter skip, or orientation.
- **Long-press Menu** — what holding the Home key does while reading: Bookmark, Reader menu,
  Highlight, or Disabled.
- **Short power button click** — Ignore, Sleep, Page turn, Refresh, Footnotes, or Confirm.
- **Quick return from footnotes** — a short Power press acts as Back while in a footnote.
- **Back to file browser** — a short Back from a book returns to the file browser instead of Home.

### System

- **Time to sleep** — inactivity before the device sleeps.
- **Show hidden files**.
- **Remove read books from recents**.
- **Move finished books to a Read folder**.
- **Wi-Fi networks** — saved networks.
- **Clear reading cache** — drop the SD cache and force a re-index.
- **Check for updates** — over-the-air firmware update.
- **SD firmware update** — flash a `firmware.bin` from the card.
- **Language** — the UI language.

## 14. The sleep screen

| Mode | What is shown |
|---|---|
| **Dark** | the default logo screen |
| **Light** | the same, on white |
| **Custom** | an image from the SD card |
| **Cover** | the cover of the open publication |
| **Cover + Custom** | the cover while reading, a custom image otherwise |
| **Quick resume** | the last page read, so waking returns to it without reloading the book |
| **Transparent** | an overlay drawn over whatever is on screen |
| **None** | blank |

**Custom images:** create a `.sleep` directory at the root of the card and put any number of `.bmp`
files in it — one is picked at random each time. A single `sleep.bmp` at the root takes priority.
Use uncompressed 24-bit BMPs at 480x800.

**Transparent overlays:** the same, with `.sleep-overlay` and `sleep-overlay.bmp` / `.png`. White
pixels leave the screen underneath unchanged. For per-pixel transparency use a PNG with an alpha
channel, or a 32-bit BGRA BMP.

## 15. Custom fonts

Additional reading fonts load from the SD card as `.cpfont` files, including scripts the built-in
families do not cover. Three ways to install them:

1. **Settings -> Reader -> Manage fonts**, and download over Wi-Fi.
2. The **Fonts** page of the web interface, while file transfer is running.
3. Copy the files to `/.fonts/YourFont/` or `/fonts/YourFont/` on the card.

Installed families appear in **Settings -> Reader -> Font family**. Full details are in
[docs/sd-card-fonts.md](./docs/sd-card-fonts.md).

## 16. Firmware updates

**Settings -> System -> Check for updates** downloads the latest bereanOS release over Wi-Fi and
flashes it into the spare OTA partition. An image built for a different board is refused. This is
the normal way to update.

**Settings -> System -> SD firmware update** flashes a validated `firmware.bin` from the card, which
you can put there over USB or through the web interface.

Both write to the partition the device is not running from, so a failed update leaves the working
firmware in place.

## 17. Where your data lives

Everything is on the SD card, under `.crosspoint/`:

| | |
|---|---|
| `epub_<hash>/progress.bin` | reading position for one publication |
| `epub_<hash>/sections/*.bin` | cached page layout |
| `epub_<hash>/book.bin` | title, author, spine, table of contents |
| `highlights/` | highlights and their tags |
| `settings.json`, `state.json`, `recent.json` | settings, session state, recent list |

The directory keeps its inherited name so that upgrading to bereanOS does not orphan a card full of
cached books and reading positions. Phase 1 migrates it.

The hash is derived from the file path, so **moving or renaming a publication loses its reading
position and its highlights.** Copy them back, or move files through the web interface, which re-keys
the cache.

Deleting `.crosspoint/` clears everything, including highlights. Back it up before you do.

## 18. Troubleshooting

**A screen is corrupt or ghosted.** Set **Short power button click** to **Refresh** and press Power,
or lower **Refresh frequency**.

**A publication renders wrongly after a firmware update.** The layout cache format may have changed.
**Reader menu -> Delete book cache**, or **Settings -> System -> Clear reading cache**.

**The device will not boot.** Press and release **Reset**, then hold the Home key and **Power** to
come up on the Home screen instead of resuming a book. If that fails, a corrupt settings file is the
usual cause: delete `.crosspoint/settings.json` and `.crosspoint/state.json` from the card.

**Crash reports.** After a crash the firmware writes a report to the root of the SD card. Attach it
to any bug report.

**Serial logs.** Connect the device over USB and run:

```bash
python3 scripts/debugging_monitor.py
```

It auto-detects the port, colour-codes the log by subsystem, and graphs free heap. Pass a port
explicitly if the guess is wrong (`python3 scripts/debugging_monitor.py /dev/cu.usbmodem1101`), and
`--filter MEM` or `--suppress "[SD]"` to narrow the output.

`pio device monitor` does not work on this device — the USB-JTAG bridge has no line settings and the
monitor fails setting a baud rate. Either use the script above or read the port directly:

```bash
cat /dev/cu.usbmodem1101 > serial.log
```

Release builds log at a lower level than development builds, so a reproduction with the serial log is
worth far more from a build flashed over USB than from an OTA image.
