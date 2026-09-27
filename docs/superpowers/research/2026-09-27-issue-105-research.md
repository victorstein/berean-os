# Issue #105 — research

Issue #105 asks us to remove CrossPoint's in-browser EPUB optimiser and the bundled jszip from the
File Transfer page, check that uploads still work (the dev firmware side-load included), and report
the flash saving. This note records how the page and the server behave today. Every claim was read
or run in this worktree at `e41880e3`.

## Files that own the behaviour

| Concern | Location |
|---|---|
| The page: CSS, markup and all JS in one file | `src/network/html/FilesPage.html` (217,924 B, 6,036 lines) |
| The jszip asset | `src/network/html/js/jszip.min.js` (97,630 B) |
| HTML/JS → gzip → `constexpr char[] PROGMEM` headers | `scripts/build_html.py:49-88`. It walks all of `src/` for `*.html` / `*.js`, minifies only `.html` (`:56-60`), gzips at level 9 (`:64`), and writes `<name>Html` / `<name>Js` arrays (`:68-86`) |
| Build hook | `platformio.ini:131-133`, `pre:scripts/build_html.py` |
| Header includes | `src/network/CrossPointWebServer.cpp:26` (`FilesPageHtml`) and `:30` (`html/js/jszip_minJs.generated.h`) |
| jszip route | `CrossPointWebServer.cpp:142` registers `GET /js/jszip.min.js` → `handleJszip()` (`:367-371`), declared at `CrossPointWebServer.h:95` |
| Page route | `CrossPointWebServer.cpp:141` `GET /files` → `handleFileList()` → `sendHtmlContent(..., FilesPageHtml, sizeof(FilesPageHtml))` (`:487-489`, helper at `:357-360`) |
| Upload endpoints | `POST /upload` at `CrossPointWebServer.cpp:150` (`handleUpload` `:645`, `handleUploadPost` `:798`); WebSocket upload, protocol `START:<filename>:<size>:<path>`, at `:1471-1539` |
| Endpoint doc | `docs/webserver-endpoints.md:24` lists `GET /js/jszip.min.js` as "JavaScript asset used by the file manager" |

`grep -rln "jszip\|JszipMin\|FilesPageHtml\|handleJszip" src lib docs scripts test platformio.ini`
returns only `CrossPointWebServer.{h,cpp}`, `FilesPage.html`, `jszip.min.js`,
`docs/webserver-endpoints.md` and `scripts/build_html.py` (a comment at `:56` naming jszip as the
example of a pre-minified JS file). No host test and no other page references either one. The
other pages (`HomePage`, `SettingsPage`, `FontsPage`) carry their own inline CSS, so no CSS class is
shared across pages.

The root `CLAUDE.md` says the HTML sources live in `data/html/`. They do not: the tree has no
`data/html/`, and `build_html.py:5` sets `SRC_DIR = "src"`.

## The issue's "xtc appears 42 times" is a false signal

```
$ grep -o 'xtc' FilesPage.html | wc -l           ->  0
$ grep -o -i 'xtc' FilesPage.html | wc -l        -> 42
$ grep -o 'textContent' FilesPage.html | wc -l   -> 42
$ grep -o -iw 'xtc' FilesPage.html | wc -l       ->  0
```

All 42 hits are `textContent` matched case-insensitively. The page has no XTC code. The size
argument still holds, because the optimiser itself is large (below). The spec should not plan any
"remove XTC" work. (`XTCache` in the server's protected-path list, `docs/webserver-endpoints.md:84,106`,
is server-side and outside this issue.)

## Page structure

| Lines | Content |
|---|---|
| 7 | `<script src="/js/jszip.min.js"></script>`, the only jszip load |
| 8-1501 | `<style>` |
| 1502-1842 | body markup |
| 1843-6034 | one `<script>` block |

### What must survive: list, upload, download, rename, delete (plus move, new folder, image preview)

- `hydrate()` `:1936`, which builds the file table from `/api/files`, the breadcrumbs, and overlay-click
  handlers. It opens with `fetchVersion()` (`:1938`), an optimiser hook (see below).
- Download links `:2092`, image preview `:2088-2110`, and the modal `:1829-1842`.
- The upload modal markup `:1553-1765`. Only the drop zone and file input (`:1561-1564`), the
  Upload/Cancel buttons (`:1752-1753`) and the progress bar (`:1754-1757`) belong to plain upload.
- Drag-and-drop listener `:3143-3218`, which routes a drop through `validateFile()`.
- Upload core: `fetchExistingUploadNames` `:3966`, `reserveAvailableUploadFilename` `:3943`
  (collision suffixing), `uploadFileWebSocket` `:5319`, `uploadFileHTTP` `:5428` (fallback),
  `uploadFile` `:5471`, the failed-upload banner and retry `:5756-5830`, cancel
  (`handleCancelUploadModal` `:2132`, `restoreAfterCancel` `:2145`).
- Folder, delete, rename and move modals and their JS: `:3025-3120`, `:5833-6033`.

### What goes: the optimiser

- CSS for the convert options, advanced settings, toggle switches, quality presets, rotation, device
  and overlap buttons, the image picker and legend, `picker-mode` / `picker-columns`, the log section,
  `.upload-btn.optimize` and `.convert-warning`. A class-by-line map (`grep -n` for each class) puts
  every one of these classes' uses inside the style block or the upload modal's optimiser markup
  `:1565-1750`, the log section `:1758-1764`, or optimiser JS. `.toggle-switch`, `.setting-*` and
  `.advanced-setting-row` have no user outside the upload modal's settings panel.
- Markup `:1565-1750` (convert options, advanced settings, image picker, sync warning,
  `startConversionBtn`) and the conversion log `:1758-1764`.
- JS: `updateBatchModeUI` / `toggleConvertOptions` / `toggleAdvancedOptions` / quality, handedness
  and overlap setters `:2207-2330`; the image picker `:2327-2965`; `clearImagePicker` `:3124`;
  converter constants, device profiles and upload-settings persistence `:3283-3390`; picker state and
  the logging system `:3392-3810`; the EPUB fix-up and convert pipeline `:3811-5310`
  (`convertEpubFile` `:4957`).

### Coupling points that are not self-contained blocks

Each of these mixes optimiser and kept behaviour, so it cannot be deleted by line range:

1. **`uploadFile()` `:5471-5754`** threads conversion through the kept upload loop: the
   `convertEnabled` read, batch logging (`useBatchLog`, `startBatchLog`, `finalizeBatchLog`), the
   `needsConversion` branch at `:5672-5709`, the 50%/100% progress split in `onProgress`, and the
   batch-log calls in `onComplete` / `onError`.
2. **`validateFile()` `:3220-3280`** shows or hides the convert options, opens the image picker, and
   calls `updateBatchModeUI`. The file-input state and `uploadBtn.disabled` handling stay.
3. **`openUploadModal()` `:2112` and `closeUploadModal()` `:2161`** reset picker, log and advanced
   state and call `restoreUploadSettingsFromStorage` / `applyUploadSettings`. `closeUploadModal`
   also resets the kept file input, progress bar and upload button.
4. **The `DOMContentLoaded` handler `:2970-3023`** sets up quality-slider state and *also* registers
   the delegated `.image-preview-link` click handler (`:2972-2977`), which is kept behaviour.
5. **`hydrate()` → `fetchVersion()` `:1938` / `:3415`** fetches `/api/status` only to fill
   `crosspointVersion` (used by the log export) and `DETECTED_DEVICE` (used by the device profile).
   `/api/status` itself stays: `HomePage.html:142` uses it, and it is filled at
   `CrossPointWebServer.cpp:407-409`.
6. **`restoreAfterCancel` `:2145`**: only a comment mentions "Optimize & Upload".

### A kept-looking feature that depends on jszip: "Rename from Book Metadata"

The toggle sits inside the convert-options panel (`:1582-1593`), which is shown only when an EPUB is
selected. When it is on, `uploadNextFile` calls `maybeRenameEbookFile` (`:5584-5591`, `:3924`) →
`getMetadataFilenameForEpub` (`:3896`) → `JSZip.loadAsync` (`:3898`), which reads the OPF to name
the file `Title - Author.epub`. It came from upstream CrossPoint PR #2534
(`git log -- FilesPage.html`: `35a45f9c`). It is independent of conversion, but it cannot survive
jszip's removal unless something else parses the ZIP. Its setting is off by default
(`DEFAULT_UPLOAD_SETTINGS.renameFromMetadata: false`, `:3323-3332`). The issue's list ("list,
upload, download, rename and delete") does not include it, and "rename" there means `POST /rename`.
The spec has to decide this one explicitly: drop it with jszip (my reading of the issue) or keep a
jszip-free OPF reader.

The upload-settings persistence (`localStorage` key `crosspoint.files.uploadSettings.v1`, `:3322`)
stores only optimiser settings and the rename toggle, so nothing is left for it to remember.

## Uploads, including the dev firmware side-load

- **The side-load is server-side and does not touch the page.** The documented path
  (`CLAUDE.md`, "Flashing") is `curl -H "Expect:" -X POST -F file=@firmware.bin
  "http://<ip>/upload?path=/"`, followed by *Settings → SD firmware update*.
  `SdFirmwareUpdateActivity::launchPicker` (`src/activities/settings/SdFirmwareUpdateActivity.cpp:25-26`)
  reuses the file browser and restricts it to `.bin`. None of this reads `FilesPage.html`.
- **A `.bin` uploaded through the page already bypasses the optimiser.** `validateFile` shows the
  convert options only when an `.epub` is selected (`:3228-3230`), and `needsConversion` requires
  `isEpub` (`:5577-5578`). So a firmware upload from the browser takes the plain path today: WS
  first, HTTP fallback.
- The WS and HTTP upload functions do not reference any optimiser symbol. They take `file`,
  `currentPath` and the progress callbacks.

## Nothing validates this HTML

No host test, CI step or lint parses `FilesPage.html`. The build only gzips it
(`build_html.py`). A JS `ReferenceError` left behind by a partial strip, such as a call to a deleted
function in `closeUploadModal`, would ship green. Commit `0d9285eb` ("refactor: drop the OPDS
browser") names the same risk. Installed tools that can close the gap on the host:

```
$ node --version                                   -> v24.16.0
$ /Volumes/stein/.platformio/penv/bin/pio --version -> PlatformIO Core, version 6.1.19
$ python3 --version                                -> Python 3.14.7
```

For example, `node --check` on the extracted `<script>` body, plus a grep that every
`getElementById('<id>')` names an id still in the markup, and every `onclick="fn()"` names a
function still defined.

## Toolchain

- Platform: `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/...`
  (`platformio.ini:15`); framework `arduinoespressif32@3.3.7` (installed during the baseline build
  below).
- Build through the batch lock: `pio-locked.sh run -e x4pro`.

## Flash baseline

`pio-locked.sh run -e x4pro` at `e41880e3` (exit 0):

```
Generated: src/network/html/FilesPageHtml.generated.h
  Original: 217405 bytes      <- characters, not bytes; wc -c gives 217,924 because of UTF-8 emoji
  Minified: 213177 bytes (98.1%)
  Compressed: 49528 bytes (22.8%)
Generated: src/network/html/js/jszip_minJs.generated.h
  Original: 97630 bytes
  Minified: 97630 bytes (100.0%)
  Compressed: 28379 bytes (29.1%)
RAM:   [==        ]  19.9% (used 65132 bytes from 327680 bytes)
Flash: [========  ]  84.8% (used 5557906 bytes from 6553600 bytes)
$ stat -f %z .pio/build/x4pro/firmware.bin  -> 5558416
```

The two arrays put 77,907 B of gzip in flash (`constexpr char[] PROGMEM`, `build_html.py:78`),
so removing jszip saves 28,379 B outright, plus whatever `FilesPageHtml` shrinks by. The build also
prints a `-Wnonnull` warning at `src/activities/network/CrossPointWebServerActivity.cpp:204`. It
predates this issue and is not in the files it touches.

## Nearest existing example

`0d9285eb` "refactor: drop the OPDS browser" is the closest. It removed a CrossPoint subsystem
including its web-server endpoints and web-UI section. Two lessons from its message apply here:

1. Check for CSS classes that the removed section shares with kept UI before deleting them. There
   it was `.opds-server` styling the WiFi editor. Here, the class map above found no such sharing
   outside the upload modal.
2. Verify by grep for orphans, because nothing validates the HTML.

Its shape also fits: a route and handler deleted from `CrossPointWebServer.{h,cpp}`, the endpoint
removed from the docs, and the verification listed in the commit.

## Scope and tier

The change stays inside `src/network` (the page, the jszip asset, two server routes) plus
`docs/webserver-endpoints.md` and a comment in `scripts/build_html.py`. It has no on-disk format,
no migration, no shared append file (`test/CMakeLists.txt`, translations, `src/main.cpp`) and no
contract with another surface. `/api/status` and `/upload` are unchanged. The `standard` tier holds.
The only decision is the "Rename from Book Metadata" question above, which belongs in the spec.
