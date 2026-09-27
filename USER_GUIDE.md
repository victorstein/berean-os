# bereanOS User Guide

This guide covers the device as the firmware behaves today: the controls, the screens, and every
setting. The product is being reshaped around four sections — Biblia, Reuniones, Buscar, Etiquetas y
ajustes — and the screens below are replaced when that lands. See [ROADMAP.md](./ROADMAP.md).

- [1. The device](#1-the-device)
- [2. Controls](#2-controls)
- [3. Power and startup](#3-power-and-startup)
- [4. The launcher](#4-the-launcher)
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

There is **no Back button and no Confirm button.** Back is a gesture or a held **Left**; Confirm is
a tap, a held **Right**, the Home key, or the Power button when you configure it that way.

## 2. Controls

| Input | What it does |
|---|---|
| Tap | activates whatever you touched |
| Left / Right | previous / next in a list; previous / next page while reading |
| Hold **Left** just under a second, then release | **Back** |
| Hold **Right** just under a second, then release | **Confirm** |
| Swipe right from the left edge | **Back** (on the reading surface, only to return from a link; see below) |
| Swipe down from the top edge | opens the frontlight panel |
| Swipe up from the bottom edge | Home, on devices without a Home key |
| **Home**, short press | the launcher; inside some screens it confirms instead (see below) |
| **Home**, long press | runs the **Long-press Menu** function while reading |
| **Power**, short press | configurable: ignore, sleep, page turn, refresh, footnotes, or Confirm |
| **Power**, long press | power off |

Left and Right act when you release them, not when you press them: a quick press moves or turns a
page, and a press held for just under a second becomes Back or Confirm instead.

The left-edge Back swipe works in every screen except the reading surface. There it only returns
you from a followed link (see [5. Reading](#5-reading)), so that in swipe page-turn mode a right
swipe can turn back a page.

**Home is context-dependent.** Normally it takes you to the launcher. Two screens repurpose it
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

On a first boot you land on the launcher. After that the device reopens the book you were reading.

The device sleeps after the inactivity timeout set in **Settings -> System -> Time to sleep**. A
download or a firmware update in progress does not count as activity, so leave the screen awake, or
raise the timeout, while one is running.

## 4. The launcher

The launcher is the device's home. Under the **bereanOS** header it shows four tiles:

- **Bible** — opens your Bible and shows how many chapters you have read. If no Bible has been found
  on the card, the tile reads **Not on the card · tap to download**, and tapping it offers to
  download the New World Translation in your **Publication language** (see
  [Finding the Bible](#finding-the-bible)).
- **Meetings** — the week's meeting publications (see
  [10. Meeting publications](#10-meeting-publications)).
- **Publications** — every publication on the card, with a search of the jw.org catalog to download
  more.
- **Settings**

When there is a book to go back to, a **Continue Reading** tile resumes it.

Tap a tile to open it, or move the selection with **Left** / **Right** and confirm.

### Finding the Bible

The launcher looks for the Bible in three places, in order:

1. A Bible downloaded on the device, or one you have opened before.
2. A file named the way jw.org names it, such as `nwt_S.epub` or `nwt_E.epub`, in the download
   folder or at the root of the card.
3. Your recent books.

A Bible you copied onto the card yourself is remembered the first time you open it, so after that
the tile finds it whatever it is called and wherever it is.

With no Bible found, tapping the tile asks **Download the Bible?** and gives its approximate size.
**Download** connects to Wi-Fi and fetches it, then returns you to the launcher; **Choose a file**
opens the file browser (see [12. Browsing files](#12-browsing-files)) so you can open a Bible that is
already on the card.

## 5. Reading

| Action | Input |
|---|---|
| Next page | tap the right third, or press **Right** |
| Previous page | tap the left third, or press **Left** |
| Reader menu | tap the centre third, or hold **Right** and release |
| Start a passage selection | long-press a word outside the centre third (section 8) |
| Leave the book | **Home**, hold **Left** and release, or **Reader menu -> Go home** |

**Cross-references.** Following a link inside a publication remembers where you came from. Back —
the left-edge swipe or a held **Left** — returns to that position rather than leaving the book. The
return stack holds three positions.

**Auto page turn** advances pages on a timer; enable it from the reader menu.

## 6. The reader menu

Tap the centre third of the page. The menu lists, depending on the publication:

- **Select chapter** — the table of contents, or the Bible drill-down (section 7)
- **Search verses** — in a Bible only
- **Footnotes** — the footnotes on the current page
- **Bookmarks** — jump to or delete a saved position, once there is one
- **Highlights** — browse what you have marked in this publication
- **Toggle bookmark** — drop or remove a bookmark at the current page
- **Highlight passage** — start a passage selection (section 8)
- **Text settings** — font, size, spacing, margins, with a live preview
- **Night mode** — invert the page
- **Frontlight** — the light panel
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

A **highlight** marks a passage. A **tag** is a label you attach to it. Tags are global: a tag you
create in one publication is offered in every other.

### Marking a passage

1. Open the reader menu and choose **Highlight passage**. (Or set **Long-press Menu** to
   **Highlight** and hold the Home key, or long-press a word outside the centre third to start the
   selection there.)
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
the filter list retires it: it stops being offered in the tag picker, and it is removed from this
publication's highlights. A highlight left with no tag shows as **Unlabeled**; no highlight is
deleted.

## 9. Bookmarks and footnotes

**Bookmarks** are saved positions. Add one from **Reader menu -> Toggle bookmark**, or by holding the
Home key when **Long-press Menu** is set to **Bookmark**. **Reader menu -> Bookmarks** lists them;
activating a row jumps there, and a long press deletes after a confirmation.

**Footnotes** appear in the menu when the current page has any. Selecting one opens the note and
remembers where you were; Back returns. Setting **Short power button click** to **Footnotes** opens
the same list from the Power button, and jumps straight there when there is only one note on the page.

## 10. Meeting publications

**Settings -> System -> File transfer -> Meeting publications** downloads the current week's *Watchtower* study
edition and *Life and Ministry Meeting Workbook* as EPUBs onto the SD card.

The device reads its clock, works out the ISO week from the local date (the clock's UTC offset setting
applies), fetches that week's meetings page, reads the
issue numbers out of the publication links, resolves the download URLs, and writes the files. Either
publication may be missing for a given week, which is a normal outcome and not an error.

The **Meetings** tile on the launcher opens the week view:

- A header with the week's dates, such as *Week of 21–27 September*.
- A strip of the seven days, Monday first. Today is shown inverted, and a dot marks each day set in
  **Settings -> System -> Midweek meeting day** and **Weekend meeting day**.
- One card per publication, with its cover, title and issue, and a progress bar once you have
  started reading it. Tap a card to open the publication, or to download it when it is not on the
  card yet. The Memorial week has only a *Watchtower*, so it shows one card.
- **Download again**, which re-checks the week and fetches both publications.

When the week is not known yet, the screen looks it up once on opening.

Files already on the card with a matching checksum are not downloaded again. Where they land is set
by the download folder, which is editable from the web settings page; blank means the card root.

**Set the clock first.** The week is derived from it, so a wrong clock fetches the wrong week. The
clock lives under **Settings -> Reader -> Customise status bar**, which is also where **Sync now**
and the UTC offset are; those rows appear only on a device with a real-time clock.

## 11. File transfer and the web interface

**Settings -> System -> File Transfer**, then pick a mode:

| Mode | Use when |
|---|---|
| **Join network** | the device should join your Wi-Fi |
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

The file browser walks the SD card. It opens from **Choose a file** in the Bible download offer,
which the **Bible** tile shows when no Bible has been found on the card. The current path is shown at
the top, directories appear in brackets, and file extensions are shown.

- **Left** / **Right** move the selection.
- Tap a row to open a folder or a book. A `.bmp` opens in the image viewer.
- Long-press a row to delete it, after a confirmation. Renaming and moving are web-interface
  operations, not device ones.

Hidden files and folders — anything starting with `.` — are not shown.

## 13. Settings

The **Settings** tile on the launcher, four tabs.

### Display

- **Sleep screen** — Dark, Light, Custom, Cover, Cover + Custom, Quick resume, Transparent, None, or
  Study. See section 14.
- **Sleep screen cover mode** — Fit or Crop, when a cover is shown.
- **Sleep screen cover filter** — None (grayscale), Contrast, or Inverted.
- **Quick resume on timeout** — use the quick-resume sleep screen when the device sleeps on its own.
- **Hide battery %** — Never, In reader, or Always. The icon always stays.
- **Refresh frequency** — how often the panel does a full refresh while reading, to clear ghosting:
  every 1, 5, 10, 15 or 30 pages.
- **UI theme** — Classic or Lyra. Tap to switch between them.
- **Restore light on wake** — bring the frontlight back at the brightness it had before sleep.

### Reader

- **Text settings** — with a live preview:
  - **Font family** — Noto Serif, Noto Sans, or any family installed on the card.
  - **Font size**, **line spacing**, **screen margin**, **paragraph alignment**.
  - **Embedded style** — honour the publication's own HTML and CSS.
  - **Focus reading** — bold the first part of each word.
  - **Hyphenation**.
  - **Extra paragraph spacing** — space between paragraphs instead of a first-line indent.
  - **Text anti-aliasing** — smoother edges, slightly slower page turns.
- **Manage fonts** — browse and download SD-card font families over Wi-Fi.
- **Images** — display, show a placeholder, or suppress.
- **Night mode** — invert the reading surface.
- **Customise status bar** — what the reading status bar shows: chapter page count, book percentage,
  progress bar style and thickness, title, battery, and the clock. On a device with a real-time clock
  this is also where the clock format, the UTC offset, and **Sync now** live.

### Controls

- **Side button layout** — Prev/Next, Next/Prev, or disabled while reading.
- **Touch reader controls** — Off, Tap, Swipe, or Inverted tap.
- **Tap for reader menu** — whether a centre-third tap opens the menu.
- **Long-press Menu** — what holding the Home key does while reading: Bookmark, Reader menu,
  Highlight, or Disabled.
- **Short power button click** — Ignore, Sleep, Page turn, Refresh, Footnotes, or Confirm.
- **Quick return from footnotes** — a short Power press acts as Back while in a footnote. Shown only
  when **Short power button click** is set to **Footnotes**.

### System

- **Publication language** — Spanish or English; the language the Bible, the meeting publications
  and the catalog are downloaded in.
- **Look up meetings when WiFi connects** — on by default.
- **Midweek meeting day** and **Weekend meeting day** — the days the meetings week view marks.
  **Not set** by default.
- **Time to sleep** — inactivity before the device sleeps.
- **Wi-Fi networks** — saved networks.
- **File transfer** — see section 11.
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
| **Study** | one of your marked passages, with the date when the clock is set, its reference, its first tag, and your Bible reading progress |

**Study** picks a different passage each time the device sleeps, favouring ones it has not shown
recently. With no marked passages it falls back to the default screen.

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

Everything is on the SD card, in two hidden directories.

`.crosspoint/` holds settings, reading positions and caches:

| | |
|---|---|
| `epub_<hash>/progress.bin` | reading position for one publication |
| `epub_<hash>/sections/*.bin` | cached page layout |
| `epub_<hash>/book.bin` | title, author, spine, table of contents |
| `bookmarks/` | bookmarks, one file per publication |
| `settings.json`, `state.json`, `recent.json` | settings, session state, recent list |
| `wifi.json` | saved Wi-Fi networks |

`.berean/` holds your study data:

| | |
|---|---|
| `passages/` | highlights and the tags on them, one file per publication |
| `tags.json` | your tags, shared by every publication |
| `completion/` | which Bible chapters you have read |
| `pubkeys.json` | which publication each downloaded file is, and which file is your Bible |
| `meeting-weeks.json` | the meeting publications for each week looked up |
| `search/` | the Bible search index |
| `units/` | per-publication indexes of verses and paragraphs |

The search index and the unit indexes can be rebuilt: search offers to prepare its index again, and
a unit index is built again when its publication is opened.

The `.crosspoint/` hash is derived from the file path, so **moving or renaming a publication loses
its reading position, its bookmarks and its cached layout**, including through the web interface.
Highlights are kept by publication instead of by file. The Bible's follow it wherever it is. A
publication downloaded on the device finds its highlights again once it is downloaded again; one you
copied onto the card yourself finds them only at its original path.

**Older highlights.** Highlights made before this study store existed live in
`.crosspoint/highlights/`. On boot the device copies any it has not copied yet into `.berean/`,
showing **Preparing your study data** while it works. The originals are left where they are.

Deleting `.crosspoint/` clears settings, reading positions, bookmarks and caches; deleting `.berean/`
clears your highlights, tags and Bible reading progress. Back them up before you do.

## 18. Troubleshooting

**A screen is corrupt or ghosted.** Set **Short power button click** to **Refresh** and press Power,
or lower **Refresh frequency**.

**A publication renders wrongly after a firmware update.** The layout cache format may have changed.
**Reader menu -> Delete book cache**, or **Settings -> System -> Clear reading cache**.

**The device will not boot.** Press and release **Reset**, then hold the Home key and **Power** to
come up on the launcher instead of resuming a book. If that fails, a corrupt settings file is the
usual cause: delete `.crosspoint/settings.json` and `.crosspoint/state.json` from the card.

**Crash reports.** After a crash the firmware writes a report to the root of the SD card. Attach it
to any bug report.

**Serial logs.** Only a development build writes a log over serial; the build that **Check for
updates** installs does not. Build one with `pio run -e x4pro` and install it over USB or with **SD
firmware update** (section 16). Then connect the device over USB and run:

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
