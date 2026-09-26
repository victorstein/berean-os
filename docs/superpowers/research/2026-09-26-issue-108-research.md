# Issue #108: fallible bare `new`. Research

Branch `fix/108-nothrow-fallible-allocations`, based on `2f303f6f` (release 1.16.0).
Each claim below cites a `file:line` or a command whose output was read in this session.

## 1. Why a bare `new` aborts on this target

- The firmware is built with `-fno-exceptions`, and `-fexceptions` is unflagged
  (`platformio.ini:72`, `platformio.ini:74-76`).
- The prebuilt Arduino/IDF libs were compiled **with** C++ exceptions:
  `framework-arduinoespressif32-libs/esp32s3/sdkconfig:1023` has
  `CONFIG_COMPILER_CXX_EXCEPTIONS=y`, with `EMG_POOL_SIZE=0` on line 1024.
  So the throwing `operator new` does throw `std::bad_alloc` on failure. No frame in a
  `-fno-exceptions` TU can catch it, so it reaches `std::terminate` and then `abort()`.
  That matches what `CLAUDE.md` and `lib/Memory/Memory.h:9-10` describe. A null check
  after a bare `new` is dead code.
- `makeUniqueNoThrow` (`lib/Memory/Memory.h:23-34`) wraps `new (std::nothrow)`. The
  array form value-initialises (`Elem[count]()`), which matches the `new T[n]()` sites
  it would replace.

## 2. Installed toolchain

Command: `~/.platformio/penv/bin/pio --version` and `pio pkg list -e x4pro`.

| Package | Version |
|---|---|
| PlatformIO Core | 6.1.19 |
| platform espressif32 (pioarduino) | 55.3.37 |
| framework-arduinoespressif32 | 3.3.7 |
| framework-arduinoespressif32-libs | 5.5.0+sha.87912cd291 |
| framework-espidf | 3.50502.0 (IDF v5.5.2) |
| toolchain-xtensa-esp-elf | 14.2.0+20251107 |
| tool-cppcheck | 2.11.0 |

Host tests use CMake, C++20 and GoogleTest (`test/CMakeLists.txt:4-5`). They do **not**
pass `-fno-exceptions` (`test/CMakeLists.txt:42-46`), so on the host a failed
throwing `new` throws. It does not abort. Host OOM behaviour therefore differs from the
device's.

## 3. Inventory of bare `new` in `lib/` and `src/`

Command:
`grep -rnE "(=|return|\() *new [A-Za-z_:]" --include='*.cpp' --include='*.h' lib src | grep -v nothrow`.
A broader sweep for `\bnew\b` found nothing more outside comments and the vendored
`expat.h`. The issue's list is **incomplete**. Sites marked † are not in the issue.

### Image paths (sized from image content)

| Site | What | Size driver | Current failure channel |
|---|---|---|---|
| `lib/GfxRenderer/BitmapHelpers.h:27-29` | `Atkinson1BitDitherer` ctor, 3× `int16_t[width+4]` | image width | none. The ctor cannot fail |
| `BitmapHelpers.h:108-110` | `AtkinsonDitherer` ctor, same | image width | none |
| `BitmapHelpers.h:209-210` | `FloydSteinbergDitherer` ctor, 2× `int16_t[width+2]` | image width | none |
| `lib/PngToBmpConverter/PngToBmpConverter.cpp:632,635,637` | raw `Ditherer*` locals | `outWidth` | `bool` return. Manual `delete` at `:660-662` and `:817-819` |
| `PngToBmpConverter.cpp:648-649` | `rowAccum`/`rowCount` `new T[outWidth]()` | `outWidth` | same. Manual `delete[]` |
| `lib/GfxRenderer/Bitmap.cpp:171,173` | ditherer members in `parseHeaders` | BMP width | returns `BmpReaderError`. `OomRowBuffer` exists (`Bitmap.h:59`) |
| † `lib/Epub/Epub/converters/PngToFramebufferConverter.cpp:53` | `new HalFile()` in the PNGdec open callback | fixed | callback already returns `nullptr` on open failure (`:54-56`) |
| † `lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp:56` | same for JPEGDEC | fixed | same |
| † `lib/Epub/Epub/converters/ImageDecoderFactory.cpp:28,33` | decoder singletons | fixed | factory already returns `nullptr` for "no decoder" (`:37-38`) |

Width bounds: `Bitmap.cpp:125-129` and `PngToBmpConverter.cpp:451-456` both cap width at
2048. At the cap a ditherer row is `(2048+4)*2 = 4104` bytes. The Atkinson ditherers
therefore take about 12.3 KB in 3 allocations.

**The ditherer constructors are the core of the image problem.** Even the gold-standard
call site, `makeUniqueNoThrow<AtkinsonDitherer>(outWidth)`
(`lib/JpegToBmpConverter/JpegToBmpConverter.cpp:676-694`), only makes the *object*
allocation nothrow. The object's constructor then makes three throwing
`new int16_t[]` calls. Those abort, and `makeUniqueNoThrow` cannot catch them. Fixing
the call sites alone does not fix the JPEG path. Nothing on the ditherer classes
reports whether their rows were allocated.

Dead member: `Bitmap::errorCurRow`/`errorNextRow` (`Bitmap.h:97-98`) are only ever
`delete[]`d (`Bitmap.cpp:17-18`). Nothing allocates them.

### EPUB engine

| Site | What | Failure channel today |
|---|---|---|
| `lib/Epub/Epub.cpp:362,364` | `BookMetadataCache`, `CssParser` in `Epub::load` | `load()` returns `bool`, and callers check it (e.g. `EpubReaderActivity.cpp:195`, `SleepActivity.cpp:825`). Two callers ignore it (`RecentBooksStore.cpp:119`, `HomeActivity.cpp:65`) |
| `Epub.cpp:383,495` | reload `BookMetadataCache` after a CSS rebuild or build | same `bool` |
| `lib/Epub/Epub/Page.cpp:79` | `PageImage::deserialize` | returns `unique_ptr`. Caller null-checks (`Page.cpp:198-201`) |
| `Page.cpp:172` | `Page::deserialize` | returns `unique_ptr`. Callers null-check (`Section.cpp:737-741`, `:775-778`) |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:234` | page break in `flushPendingAnchor` | `void` |
| `ChapterHtmlSlimParser.cpp:325` | `new ParsedText` in `startNewTextBlock` | `void`. `currentTextBlock->` is dereferenced 35 times in the file |
| `ChapterHtmlSlimParser.cpp:774,782` | image page break | `void` callback. A null check follows each one (`:775-778`, `:783-786`), but it is **dead code** after a throwing `new` |
| † `ChapterHtmlSlimParser.cpp:1667,1676` | `addLineToPage` | `void`. Dereferences `currentPage` right after (`:1686`, `:1693`) |
| † `ChapterHtmlSlimParser.cpp:1704` | `makePages` | `void` |

Parser failure propagation today: `parseStep()` returns `ParseStatus::Error` only for
expat or file errors (`ChapterHtmlSlimParser.cpp:1588-1603`). `parseAndBuildPages` then
aborts (`:1651-1653`). No member flag lets a callback say "allocation failed, stop".
Stopping from a callback has a precedent in another library: `XML_StopParser(parser,
XML_FALSE)` in `lib/ProgressMapper/ChapterXPathResolver.cpp:292,497`.

### Firmware (`src/`)

| Site | What | Failure channel today |
|---|---|---|
| `src/activities/reader/EpubReaderActivity.cpp:1054` | `new Section(...)` inside `renderBook()` | `renderBook` already has an error exit: `section.reset(); showBuildError(); return;` (`:1084-1089`, lambda at `:1013`) |
| `src/network/CrossPointWebServer.cpp:112` | `WebServer` | `begin()` is `void`. Success is `running = true` (`:205`). An early `return` before that is already a failure (`:100-103`). The activity checks `isRunning()` (`CrossPointWebServerActivity.cpp:265-275`) |
| `CrossPointWebServer.cpp:191` | `WebSocketsServer` | same |
| † `CrossPointWebServer.cpp:184` | `new WebDAVHandler()`, ownership passes to `WebServer::addHandler` | same. This is the "C API takes ownership" case `CLAUDE.md` allows with a comment |
| `src/activities/network/CrossPointWebServerActivity.cpp:236` | raw global `DNSServer*` (`:35`). Freed by `stopDnsServer()` (`:38-44`) | `void` |
| `src/util/QrUtils.cpp:36` | `std::make_unique<uint8_t[]>(qrcode_getBufferSize(version))`, version ≤ 40 by payload length (`:28-32`) | the code checks `res == 0` from `qrcode_initText` and has no OOM path |

### Adjacent throwing allocations the issue does not name

`std::make_shared` also uses the throwing `operator new`. The parser already says so in
a comment (`ChapterHtmlSlimParser.cpp:806-808`). The tree has three calls:
`ChapterHtmlSlimParser.cpp:1693` (`PageLine`, once per laid-out line) and
`lib/Epub/Epub/ParsedText.cpp:1560,1589` (`TextBlock`). None of them is input-sized.
Each is a per-line allocation of a fixed-size object on the same parse path.

## 4. Triage of `std::make_unique`

`grep -rn "std::make_unique" --include='*.cpp' --include='*.h' lib src` returns 71
lines: 70 calls plus one comment in `Memory.h:9`. For comparison, 64
`makeUniqueNoThrow<` calls exist outside `Memory.h`. The issue's count of 73 is not
reproduced here.

| Group | Count | Sites | Note |
|---|---|---|---|
| `HalFile::Impl` | 4 | `lib/hal/HalStorage.cpp:86,104,120,170` | fixed-size wrapper around an `FsFile`. Surface is `hal`, not `epub` |
| `UITheme` | 4 | `src/components/UITheme.cpp:34-49` | fixed-size, one per theme switch |
| Activities | 61 | `ActivityManager.cpp`, `*Activity.cpp` | fixed-size objects, not input-sized. `main.cpp:555` is the recovery path |
| QR buffer | 1 | `src/util/QrUtils.cpp:36` | input-sized, and listed in the issue |

Only `QrUtils.cpp:36` is sized from input. Whether `startActivityForResult`/
`replaceActivity` tolerate a null `unique_ptr` was **not** checked here. The spec
phase must check it before converting any activity site.

## 5. Existing patterns to mirror

1. **Gold standard, same code family:** `lib/JpegToBmpConverter/JpegToBmpConverter.cpp:666-694`.
   It uses `makeUniqueNoThrow<uint32_t[]>(outWidth)` and
   `makeUniqueNoThrow<AtkinsonDitherer>(outWidth)`, follows each with
   `LOG_ERR("JPG", "OOM: <what>")`, and returns `false`. Commit `8377ac9e`, which
   introduced `Memory.h`, converted this file and said "Other files can be converted
   later". `PngToBmpConverter` is the file it left unconverted.
2. **Same file, parser pages:** `ChapterHtmlSlimParser.cpp:340-345` and `:367-372`
   `currentPage.reset(new (std::nothrow) Page()); if (!currentPage) { LOG_ERR(...); return; }`.
   `:806-814` has the `shared_ptr` form and a comment explaining why.
3. **Deserialize returning null:** `Page.cpp:47-51`
   (`new (std::nothrow) PageLine`, `LOG_ERR("PGE", ...)`, `return nullptr`) and
   `ImageBlock.cpp:443`.
4. **Firmware, network:** `CrossPointWebServerActivity.cpp:257-262`
   (`makeUniqueNoThrow<CrossPointWebServer>()`, `LOG_ERR("WEBACT", "OOM: ...")`), then
   the existing `isRunning()` failure branch.
5. **Firmware, owned by a C API:** `src/network/FirmwareFlasher.cpp:157,307`
   (`std::unique_ptr<uint8_t[]>(new (std::nothrow) ...)`).
6. **An earlier OOM-abort fix:** commit `5776911a` (FontDecompressor). It replaced a
   throwing `vector::resize` with a checked `malloc`, logged the failure, and returned
   `nullptr` to the render caller.

## 6. Host-test reachability

- `BitmapHelpers.h` is header-only and includes only `<cstdint>` and `<cstring>`
  (`BitmapHelpers.h:1-4`). A host test can include it directly. No test does so today:
  `grep -rl "BitmapHelpers\|Ditherer\|makeUniqueNoThrow" test` returns nothing.
- `test/CMakeLists.txt` compiles no `Page.cpp`, `ChapterHtmlSlimParser.cpp`,
  `Bitmap.cpp`, `PngToBmpConverter.cpp` or `Epub.cpp`. The nearest harness is
  `test/pagination/CMakeLists.txt`. It links `ParsedText`/`TextBlock` against a
  link-time `GfxRendererFake` and against `test/stubs` for `HalStorage`/`Logging`.
  The `HalStorage` stub is a no-op (see the `host-tests-halstorage-stub-is-noop`
  project memory).
- A host test cannot inject an allocation failure without a replaceable
  `operator new(size_t, std::nothrow_t)` or an allocator seam. Nothing in `test/`
  provides either. This is a design decision for the spec phase. The alternative is to
  make "allocation failed" a queryable state (e.g. a ditherer `isValid()` for a width
  that cannot be allocated) that a host test can assert on.

## 7. Open questions for the spec

1. Ditherers: add a validity check and have every caller (JPEG, PNG, Bitmap) check it,
   or move the rows into one `makeUniqueNoThrow<int16_t[]>` block and expose failure?
   No existing class in `GfxRenderer` sets a precedent for a fallible constructor.
2. The parser: how an allocation failure inside an expat callback should stop the parse.
   Options are an OOM flag checked by `parseStep` (which returns `ParseStatus::Error`)
   or `XML_StopParser`.
3. Scope across surfaces. `src/network` and `src/activities` belong to other agents'
   surfaces (the `net-dev` and `ui-dev` guides). The batch context routes the whole
   issue here and says to mirror existing call sites.
4. Whether the 61 fixed-size activity `std::make_unique` calls stay as they are. Section
   4 argues they should, since they are not input-sized.
