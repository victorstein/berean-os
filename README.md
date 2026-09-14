# bereanOS

bereanOS is study firmware for the **Xteink X4 Pro**. It turns the device into a dedicated reader
for the Bible and for Jehovah's Witnesses study publications: read, navigate by book, chapter and
verse, download the week's meeting publications, and tag passages for later.

It is a hard fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), a
general-purpose e-reader firmware. The EPUB engine, the renderer, the HAL and the web server come
from there. Everything that is not JW study has been removed, and what remains is being reshaped
around one job. [SCOPE.md](./SCOPE.md) says what is in and out; [ROADMAP.md](./ROADMAP.md) says in
what order the rest lands.

bereanOS is **not affiliated with, endorsed by, or connected to Xteink, Watch Tower Bible and Tract
Society, or jw.org.** It downloads publications from the same public endpoints a browser uses, and
redistributes none of them.

## Hardware

One board, one binary.

| | |
|---|---|
| MCU | ESP32-S3, dual core, 8 MB PSRAM, 16 MB flash |
| Display | 800x480 e-ink (SSD1677 or UC8179, by production batch) |
| Touch | GT911 capacitive |
| Buttons | Left, Right, Power, plus a capacitive Home key on the touch controller |
| Storage | microSD |

There is no physical Back button and no physical Confirm button. Back and menu are gestures; Home
is the capacitive key. The [User Guide](./USER_GUIDE.md) has the full input model.

Hardware facts, confirmed on the bench, are in
[`freeink-sdk/docs/xteink-x4pro-support.md`](./freeink-sdk/docs/xteink-x4pro-support.md).

## What it does today

- **EPUB reading.** Reflowable text, embedded images, hyphenation, footnotes, bookmarks,
  go-to-percent, auto page turn, screenshots.
- **Bible navigation.** Book to chapter to verse drill-down for NWT-shaped publications, instead of
  a flat table of contents. The status bar shows the chapter you are in.
- **Meeting publications.** The current week's *Watchtower* study edition and *Life and Ministry
  Meeting Workbook*, resolved from that week's meetings page and downloaded over Wi-Fi to the SD card.
- **Tagged passages.** Select a passage, attach one or more tags, and browse what you tagged.
- **Wi-Fi file transfer.** A browser UI for uploading, downloading and organising files on the SD
  card, plus WebDAV. See [docs/webserver.md](./docs/webserver.md).
- **Over-the-air updates** from this repository's releases.
- **UI translations**, inherited from CrossPoint.

## What it is becoming

The design target is four sections — **Biblia**, **Reuniones**, **Buscar** and
**Etiquetas y ajustes** — behind a launcher home screen, a passage address that survives a
publication being re-downloaded, and a catalog search that can fetch any publication from jw.org.
That is specified in
[docs/superpowers/specs/2026-09-13-berean-os-design.md](./docs/superpowers/specs/2026-09-13-berean-os-design.md)
and phased in [ROADMAP.md](./ROADMAP.md). The screens you see today are still CrossPoint's; they are
replaced in Phase 2.

## Installing

### Over the air (the normal path)

**Settings -> System -> Check for updates.** The device fetches the latest release of this
repository, refuses an image whose board tag does not match, and flashes into the spare OTA slot. A
cable is only needed for a first install or a development build.

### From the SD card

Put a `firmware.bin` on the card — over USB, or through the file transfer web UI — then
**Settings -> System -> SD firmware update**. The image is validated before it is written.

Uploading over the web server with `curl` needs the `Expect: 100-continue` header suppressed, or the
transfer stalls: the ESP32 web server never answers it.

```bash
curl -H "Expect:" -F "file=@firmware.bin" "http://<device-ip>/upload?path=/"
```

### Over USB

```bash
pip install esptool
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0x10000 firmware-x4pro.bin
```

Releases attach `firmware-x4pro.bin` along with `bootloader.bin` and `partitions.bin`; a first
install on a blank device needs all three, at `0x0`, `0x8000` and `0x10000`. Do not pass a baud
rate — the X4 Pro's native USB-JTAG bridge has no line settings, and forcing one corrupts large
transfers.

## Documentation

- [User Guide](./USER_GUIDE.md) — controls, screens, settings
- [Project scope](./SCOPE.md) — what this firmware will and will not do
- [Roadmap](./ROADMAP.md) — the phases, in order
- [Web server usage](./docs/webserver.md) and [endpoints](./docs/webserver-endpoints.md)
- [Contributing docs](./docs/contributing/README.md) — architecture, build, debugging
- [Touch and UI development](./docs/contributing/touch-and-ui.md) — how to build a screen on the
  FreeInkUI activity bases
- [File formats](./docs/file-formats.md) — the binary cache layouts

## Development

### Prerequisites

- [pioarduino](https://github.com/pioarduino/pioarduino), or VS Code with the pioarduino plugin
- Python 3.8+
- `clang-format` 21
- A USB-C cable that carries data

### Setup

```bash
git clone --recursive https://github.com/victorstein/berean-os
cd berean-os

# if cloned without --recursive:
git submodule update --init --recursive
```

Nix users can enter the development shell with `nix develop -f nix` or `nix-shell nix`.

### Build, flash, monitor

```bash
pio run                  # x4pro is the default env
pio run -t upload
python3 scripts/debugging_monitor.py   # colour-coded log and a live heap graph
```

`pio device monitor` does not work on this device: the USB-JTAG serial bridge has no line settings,
so the monitor dies setting a baud rate. Use the script above, or read the port directly with
`cat /dev/cu.usbmodemXXXX > serial.log`.

### Before opening a PR

```bash
./bin/clang-format-fix   # whole tree, which is what CI checks
pio check
pio run -e x4pro
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The host suite under `test/` builds the pure logic — parsers, scanners, the highlight store, the
address model — with no Arduino and no SD card. Anything that can be tested there should be.

## Internals

The firmware caches aggressively to the SD card: parsed metadata and laid-out pages are written once
and reused, so a page turn is a read rather than a re-parse.

```text
.crosspoint/
├── epub_<hash>/         # one directory per book
│   ├── progress.bin     # reading position
│   ├── cover.bmp
│   ├── book.bin         # title, author, spine, TOC
│   ├── css_rules.cache
│   ├── img_*            # rendered image cache
│   └── sections/*.bin   # per-chapter layout cache
├── highlights/          # tagged passages, per book
├── settings.json
├── state.json
└── recent.json
```

The directory is still named `.crosspoint` because renaming it would orphan every cached book and
every reading position on every card already in use. Phase 1 owns that migration, alongside the move
to the new `/.berean/` stores.

Deleting `.crosspoint/` clears all cached metadata and forces a full regeneration on next open. It
also deletes reading positions and tagged passages, so copy it somewhere first. For the binary
layouts, see [docs/file-formats.md](./docs/file-formats.md).

## Credits

bereanOS exists because of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
and its contributors, who built the reader this is carved out of. CrossPoint was in turn inspired by
[diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader).

Licensed under the terms in [LICENSE](./LICENSE).
