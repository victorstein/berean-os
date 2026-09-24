# bereanOS

A study firmware for the **Xteink X4 Pro**, hard-forked from `crosspoint-x4pro` on
2026-09-13. Design: `docs/superpowers/specs/2026-09-13-berean-os-design.md`.
Workflow: `docs/contributing/development-workflow.md`.

The Bible at the centre, the two weekly meeting publications, catalog browsing,
and tagged passages with global tags. No notes, no JW Library interop, no
on-device full-text search.

---

## The device

One board, one target: **ESP32-S3, dual core, 8 MB PSRAM, 16 MB flash**
(`platformio.ini:162,167` — `esp32-s3-devkitc1-n16r8` plus `-DBOARD_HAS_PSRAM`). Multi-board support is out of scope. Any rule you
remember that starts "on the C3" or "380 KB is the hard ceiling" was written for a
different device and does not apply here.

| | |
|---|---|
| MCU | ESP32-S3, dual core Xtensa LX7 |
| RAM | ~380 KB-class internal SRAM, plus 8 MB PSRAM |
| Flash | 16 MB; `app0`/`app1` 6,553,600 B each (`partitions.csv:4-5`) |
| Display | 800×480 e-ink, SSD1677 or UC8179 by production batch; 1-bit B/W plus a 4-level grayscale pass for anti-aliasing and images; slow full refresh |
| Framebuffer | 48,000 bytes (800 × 480 ÷ 8), **single buffer** |
| Storage | SD card — books, caches, and all persisted study data |
| Orientation | **Portrait only.** A fixed Left/Right button mapping and a device that turns over cannot both be true. |

### PSRAM is a licence to budget deliberately, not to stop budgeting

S3 PSRAM sits on an external SPI bus: roughly an order of magnitude slower than
internal SRAM, unusable from an ISR, unusable while the flash cache is suspended,
and DMA-constrained.

| Lives in PSRAM | Lives in internal SRAM |
|---|---|
| Catalog index while Buscar is open (~217 KB) | Framebuffer (48 KB) |
| Unit index pages being built or queried | Selection geometry, render hot path |
| Download and inflate buffers | ISR state, anything `IRAM_ATTR` touches |

Internal SRAM is still a ~380 KB-class resource. Every rule below about stack
size, heap fragmentation, `constexpr`, and string policy is about internal SRAM
and still applies in full.

### Input hardware, as confirmed on the bench

`freeink-sdk/docs/xteink-x4pro-support.md`, section "Input — digital buttons +
capacitive Home":

- **Left** nav button — GPIO0 (also a boot-strap pin; fine unless held at reset)
- **Right** nav button — GPIO7
- **Power** — GPIO3
- **Home** — a capacitive key bit on the GT911 (`0x814E & 0x10`), **not a GPIO**,
  reached through `BoardConfig::hasHomeKey()` → `HalGPIO.cpp:166`
- GT911 capacitive touchscreen

**There is no Back button and no Confirm button.** Anything that assumes four
remappable front buttons plus two side buttons describes hardware this device does
not have.

In the reader, touch is the primary control: the screen is three vertical tap
zones — outer thirds page, centre third opens the menu
(`src/activities/reader/ReaderUtils.h:129`, `isTouchMenuTap`).

`MappedInputManager` cannot simply be deleted — it is in the `Activity` base-class
constructor (`src/activities/Activity.h:22,28-29`), spans 418 references across 121
files, and *implements* this device's Back via a left-edge swipe
(`src/MappedInputManager.cpp:266,301`). It may only be removed in the same change
that lands its replacement.

---

## Agent rules

* **Role**: Senior Embedded Systems Engineer (ESP-IDF / Arduino-ESP32).
* **Primary constraint**: internal SRAM and flash headroom are both finite.
  Stability is non-negotiable.
* **Evidence**: a claim about this codebase needs a file and a line, and the line
  must have been read. The two claims most likely to be wrong and most expensive to
  discover late are "this already exists" and "this is already tested."
* **Anti-hallucination**: do not assume a library or ESP-IDF function exists. If
  you are unsure of an API's availability on the ESP32-S3 target, check the
  `freeink-sdk` source or the FreeInk SDK docs (https://freeink.org/llms.txt for an
  LLM-readable index) first.
* **No unfounded claims**: never assert a performance or memory gain without the
  mechanism (PSRAM vs SRAM, flash vs DRAM, fewer heap operations).
* **Resource justification**: justify any new heap allocation, or explain why a
  stack or static alternative was rejected.
* **Verification**: after a fix, say how the user verifies it — heap via serial, a
  specific cache file, a host test.

### Do not repeat these

All three were asserted and disproven during design:

- `sdkconfig.x4pro`'s `CONFIG_BT_NIMBLE_*` lines do **not** mean BLE is compiled
  in. Zero NimBLE objects link; `lib_deps` has no BLE library. The stack's flash
  cost is entirely unpaid.
- `data-pnum` is **not** the addressable unit in JW publications. It is the printed
  paragraph number on a `<span class="parNum">`. The structural address is
  `data-pid` on the block element.
- `VerseAnchors` does **not** generalise to other markers. It hardcodes the `id`
  attribute and a `chapter%u_verse%u` grammar (`VerseAnchors.cpp:30-41`).

---

## Scope

In: the Bible as the centre of the device, the two weekly meeting publications,
browsing and downloading from the jw.org catalog, tagging passages with global
tags, and settings.

Out, deliberately: notes, `.jwlibrary` interop, on-device full-text search, and the
general-reader subsystems the fork removed.

`SCOPE.md` describes CrossPoint's product, not this one. It is kept for the
reasoning, not the rules.

**Philosophy**: this is a dedicated study device, not a Swiss Army knife. A feature
that adds RAM pressure without improving study is out of scope.

---

## Development environment

Detect the host platform once per session — it decides which shell idioms work:

```bash
uname -s     # MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash,
  limited glob (use `find` + `xargs`)
- **Linux / WSL / macOS**: full bash, Unix paths, native glob

### Formatting

```bash
./bin/clang-format-fix -g   # while working: only Git-modified files
./bin/clang-format-fix      # before committing: the whole tree, as CI does
```

**Run the unsuffixed form before you commit.** CI runs `./bin/clang-format-fix`
over the entire tree (`.github/workflows/ci.yml`), while `-g` only reaches files
Git currently reports as modified. A file you create and commit is no longer
"modified", so `-g` silently skips it and the CI format job fails on work that
looked clean locally.

Never invoke or probe `clang-format` directly. The repository wrapper is the only
sanctioned entry point.

---

## Build system

PlatformIO, as a CLI (`pio`) and as the VS Code extension
(`platformio.platformio-ide`, see `.vscode/extensions.json`).

**Configuration files**:

* `platformio.ini` — main build configuration (committed)
* `platformio.local.ini` — local overrides (gitignored; see below)
* `partitions.csv` — ESP32 flash partition layout

### Environments

Two, both X4 Pro:

* `x4pro` — development, the `default_envs` (`platformio.ini:2`). Serial logging on.
* `x4pro-gh_release` — production, `LOG_LEVEL=0`, no serial logging. This is what
  OTA installs.

**Standard**: C++20 (`-std=c++2a`). No exceptions, no RTTI.
**Logging**: always `LOG_INF` / `LOG_DBG` / `LOG_ERR` from `Logging.h`. Raw
`Serial` output is deprecated.

### Critical build flags

These flags in `platformio.ini` fundamentally affect firmware behaviour:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1   // Avoid zlib name conflicts
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**`DESTRUCTOR_CLOSES_FILE` implications**:

- SdFat's `FsBaseFile` destructor calls `close()` when the object goes out of scope.
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the
  destructor handles it.
- Explicit `close()` is still required in three cases:
  1. **Close before delete** — before `Storage.remove()` on the same path.
  2. **Close before reopen** — before reopening the same `FsFile` variable (write
     then read, or rewrite the same path).
  3. **Member variables** — `FsFile` members outlive any single function scope, so
     close at the intended release point (usually `onExit()`).

**`SINGLE_BUFFER_MODE` implications**:

- Only one framebuffer exists; it is not double-buffered.
- Grayscale rendering needs a temporary buffer (`renderer.storeBwBuffer()`), and
  `renderer.restoreBwBuffer()` must be called to free it.
- See `lib/GfxRenderer/GfxRenderer.cpp` for the allocation pattern.

---

## Project architecture

### Directory structure

* `lib/` — internal libraries: `Epub` (engine), `GfxRenderer`, `I18n`, `Memory`,
  `Serialization`, `Utf8`, the parsers and codecs
  * `lib/hal/` — Hardware Abstraction Layer (`HalDisplay`, `HalGPIO`, `HalStorage`)
  * `lib/I18n/` — translations in `translations/*.yaml`, generated string tables
* `src/` — firmware: `activities/` (UI, `onEnter`/`loop`/`onExit`), `components/`
  (`UITheme`), `network/`, `platform/`, the settings and state singletons
* `freeink-sdk/` — low-level SDK (`EInkDisplay`, `InputManager`, `BatteryMonitor`,
  `SDCardManager`)
* `test/` — host test suites (no Arduino, no `HalStorage`), built with CMake
* `/.crosspoint/` on the SD card — binary cache for EPUB metadata and pre-rendered
  layout sections

### Hardware Abstraction Layer

**Always use HAL classes, never the SDK classes directly.**

| HAL Class    | Wraps SDK Class | Purpose               | Singleton Macro |
| ------------ | --------------- | --------------------- | --------------- |
| `HalDisplay` | `EInkDisplay`   | E-ink display control | *(none)*        |
| `HalGPIO`    | `InputManager`  | Button input handling | *(none)*        |
| `HalStorage` | `SDCardManager` | SD card file I/O      | `Storage`       |

The HAL gives consistent per-module error logging, hides SDK implementation
details, and centralises resource management. Location: `lib/hal/`.

```cpp
#include <HalStorage.h>

HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file.
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit.
}
```

Use `HalFile` (the mutex-wrapping handle), not a raw SdFat `FsFile` or an Arduino
`File`.

### SdFat is not thread-safe; all SD access MUST go through HalStorage

- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronised `m_spiActive`
  bool. Two tasks calling SdFat concurrently can confuse that state machine and end
  with one task calling `SPIClass::endTransaction()` against a paramLock the *other*
  task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert
  (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat
  issue #518.
- `HalStorage` serialises everything via `storageMutex`. Downstream code uses
  `HalFile` (declared in `<HalStorage.h>`); every method call — read, write, seek,
  close — takes the mutex, and `HalFile`'s destructor takes it before letting the
  underlying `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / a raw
  `FsFile` directly. That bypasses the mutex.

---

## Storage discipline — the rule that protects the only irreplaceable data

`SDCardManager::readFile` (`freeink-sdk/.../SDCardManager.cpp:202`) hard-caps reads
at `constexpr size_t maxSize = 50000` and returns a **silently truncated** string.
`PersistableStore::saveToFile` uses the **non-atomic** `writeDocToFile`
(`lib/Serialization/PersistableStore.cpp:11`); `writeDocToFileAtomic` sits beside
it and must be opted into.

Left alone, that chain is: a store grows past ~45 KB, saves fine because nothing
checks, reads back truncated mid-token, fails to parse, initialises empty, and the
next save overwrites the real file with `{}`. The repo already carries the scar —
`src/util/HighlightFile.h:40-42`, `SAVE_BYTE_BUDGET = 45000`.

**Every store this project introduces must:**

1. Write through `writeDocToFileAtomic`. Never `writeDocToFile`.
2. Check an explicit serialised-byte budget *before* writing. Refuse and report;
   never truncate.
3. Stream, not `Storage.readFile`, if it can exceed ~40 KB.
4. Carry a format version a future build **refuses** rather than reinterprets.
5. Name its owning task and hold `storageMutex` on write. This firmware has a web
   server and background downloads, so it has more concurrent writers than the
   model it replaces.

Settings, state, credentials and other `PersistableStore` JSON files live on SD
under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Study data goes
under `/.berean/`. Guard redundant writes and debounce progress saves — every one
costs serialisation, SD I/O, and `storageMutex` contention.

---

## Coding standards

### Naming

* Classes: PascalCase (`EpubReaderActivity`)
* Methods and variables: camelCase (`renderPage()`)
* Constants: UPPER_SNAKE_CASE (`MAX_BUFFER_SIZE`)
* Private members: `memberVariable`, no prefix
* File names match class names (`EpubReaderActivity.cpp`)
* `#pragma once` for all headers

### Comments

* Keep comments short and write them for the merged state, as if the code had
  always worked this way.
* Remove before/after narration, investigation measurements, and rationale that
  belongs in the commit message.
* Keep only non-obvious mechanism, field or parameter meaning, or the reason a
  special case exists.

### The resource protocol

1. **Stack safety**: keep local function variables under 256 bytes. Task stacks
   here are 2–4 KB; use `std::unique_ptr` or a static pool for anything larger.
2. **Heap fragmentation**: avoid repeated `new`/`delete` in loops. Allocate once in
   `onEnter()` and reuse.
3. **Flash persistence**: large constant data (UI strings, lookup tables) must be
   `static const` at minimum so it stays in flash rather than DRAM.
4. **String policy**: no `std::string` or Arduino `String` in hot paths. Use
   `std::string_view` for read-only access and `snprintf` into a fixed `char[]` for
   construction.
5. **UI strings**: all user-facing text goes through the `tr()` macro (e.g.
   `tr(STR_LOADING)`) for i18n. Never hardcode UI text. Logging messages
   (`LOG_DBG` / `LOG_ERR`) may be hardcoded; user-facing text may not.
6. **`constexpr` first**: compile-time constants and lookup tables must be
   `constexpr`, not merely `static const`. That moves computation to compile time,
   enables dead-branch elimination, and guarantees flash placement. Use
   `static constexpr` for class-level constants.
7. **`std::vector` pre-allocation**: always `.reserve(N)` before a `push_back()`
   loop. Each growth allocates a new block (2×), copies every element, then frees
   the old one — three heap operations that fragment DRAM. When the final size is
   unknown, estimate conservatively.

### Memory safety and RAII

* Prefer `std::unique_ptr`. Use destructors for cleanup, and call `vTaskDelete()`
  explicitly for deterministic task release.
* Do **not** call `file.close()` on local `FsFile` variables —
  `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit.

#### Always use `makeUniqueNoThrow`

With `-fno-exceptions`, a bare `new` that fails calls `abort()` — it does **not**
return `nullptr`. `makeUniqueNoThrow` (`lib/Memory/Memory.h`) wraps
`new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and frees
automatically on scope exit. It is also preferable to `malloc`, which is nothrow
but needs a manual `free` on every return path — a common source of leaks.

```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

someApi(buf.get(), size);   // unique_ptr keeps ownership and frees on return
```

`malloc` or `new (std::nothrow)` directly are acceptable **only** when a C API
takes ownership of the buffer and frees or deletes it itself. Document why in a
comment:

```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));
if (!buffer) { LOG_ERR("MODULE", "OOM: %d bytes", bufferSize); return false; }
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free()
```

**Rules**:

- Prefer `makeUniqueNoThrow`; automatic cleanup eliminates leak risk on error paths.
- **Never write a bare `new`** for any fallible allocation.
- Always null-check an allocation and `LOG_ERR` before returning false.
- Use `.get()` to pass the raw pointer to C-style APIs; ownership stays with the
  `unique_ptr`.

### Platform pitfalls

#### `std::string_view` and null termination

`string_view` is *not* null-terminated. Passing `.data()` to any C-style API
(`drawText`, `snprintf`, `strcmp`, SdFat file paths) is undefined behaviour when the
view is a substring or a view of a non-null-terminated buffer.

`string_view` is safe only when passed to a C++ API that accepts `string_view`. At
any C boundary, convert explicitly:

```cpp
// WRONG — undefined behaviour if the view is a substring:
renderer.drawText(font, x, y, myView.data(), true);

// CORRECT — guaranteed null-terminated:
renderer.drawText(font, x, y, std::string(myView).c_str(), true);

// CORRECT — for short strings, a stack buffer:
char buf[64];
snprintf(buf, sizeof(buf), "%.*s", (int)myView.size(), myView.data());
```

#### `IRAM_ATTR` and flash cache safety

All code runs from flash via the instruction cache. During internal-flash
operations such as OTA writes or NVS updates the cache is briefly suspended. Any
code that can execute in that window — ISRs in particular — must live in IRAM or it
crashes silently.

```cpp
void IRAM_ATTR gpioISR() { ... }            // ISR handler: must be in IRAM
static DRAM_ATTR uint32_t isrEventFlags = 0; // data an ISR reads: must be in DRAM
```

- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` — a flash-resident `static const` will
  fault
- Normal task code does **not** need `IRAM_ATTR`

#### ISR vs task shared state

`xSemaphoreTake()` cannot be called from ISR context — it will crash. Use the right
primitive per direction:

| Direction                       | Correct primitive                                  |
| ------------------------------- | -------------------------------------------------- |
| ISR → task (data)               | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()`     |
| ISR → task (signal)             | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task                     | `xSemaphoreTake()` / mutex                         |
| Simple flag (single writer ISR) | `volatile bool` + `portENTER_CRITICAL_ISR()`       |

#### Template and `std::function` bloat

Each template instantiation generates a separate binary copy. `std::function<void()>`
adds ~2–4 KB per unique signature and heap-allocates its closure. Avoid both in
library code and on any path called from the render loop:

```cpp
std::function<void()> callback;      // avoid — heap-allocating, large footprint
void (*callback)() = nullptr;        // prefer — zero overhead

// For member function + context (the common activity callback pattern):
struct Callback { void* ctx; void (*fn)(void*); };
```

When a template is necessary, use explicit instantiation in a `.cpp` to stop the
compiler duplicating it across translation units.

### Error handling

1. **`LOG_ERR` + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **`LOG_ERR` + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **`assert(false)`**: only for fatal impossible states (framebuffer missing)
4. **`ESP.restart()`**: only for recovery (OTA complete)

No exceptions, no `abort()`, always log before an error return.

A failed network fetch leaves the previous data in place and says so. A failed
write must never leave a partial file — hence atomic writes — and must report to
the UI rather than fail silently.

---

## UI

* **No hardcoded dimensions**: never assume 800 or 480. Use
  `renderer.getScreenWidth()` and `renderer.getScreenHeight()`.
* **Viewable area**: use `renderer.getOrientedViewableTRBL()` to stay inside the
  physical bezel margins.
* **All rendering goes through the `GUI` macro (`UITheme`).** Do not hardcode
  fonts, colours, or positions.

### Singletons

```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                  // Current theme
#define Storage HalStorage::getInstance()           // SD card I/O
#define I18N I18n::getInstance()                    // Internationalization
```

---

## Common patterns

### Activity lifecycle and memory management

Activities are **heap-allocated and deleted on exit** (`src/main.cpp`):

```cpp
void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;      // Activity deleted here
    currentActivity = nullptr;
  }
}
```

- Navigation = `delete` the old activity, construct the next one.
- Anything allocated in `onEnter()` MUST be freed in `onExit()`.
- FreeRTOS tasks MUST be deleted in `onExit()`, before the activity is destroyed.
- Member `FsFile` handles MUST be closed in `onExit()`; local ones auto-close.

```cpp
void onEnter() { Activity::onEnter(); /* alloc: buffer, tasks */ render(); }
void loop()    { mappedInput.update(); /* handle input */ }
void onExit()  { /* vTaskDelete, free buffers, close member FsFiles */ Activity::onExit(); }
```

Free resources in reverse order. Delete tasks **before** activity destruction —
a task still running against a deleted activity is the classic use-after-free here.

### FreeRTOS tasks

`xTaskCreate(&taskTrampoline, "Name", stackSize, this, 1, &handle)`.

Stack sizes are in **bytes**, not words:

- **2048** — simple rendering (most activities)
- **4096** — network, EPUB parsing
- Monitor with `uxTaskGetStackHighWaterMark()` when a crash looks stack-shaped.

Always `vTaskDelete()` in `onExit()`. Use a mutex for any shared state.

### Global font loading

All fonts are loaded as global static objects at startup (`src/main.cpp`): Noto
Serif and Noto Sans at 12/14/16/18pt in four styles each, plus Ubuntu UI at
10/12pt — ~80 global `EpdFont` and `EpdFontFamily` objects, guarded by
`#ifndef OMIT_FONTS`.

Font data is `static const` in `lib/EpdFont/builtinFonts/` and therefore in flash;
rendering data is cached in DRAM on first use. Font IDs live in `src/fontIds.h`.

```cpp
#include "fontIds.h"

renderer.insertFont(FONT_UI_MEDIUM, ui12FontFamily);
renderer.drawText(FONT_UI_MEDIUM, x, y, "Hello", true);
```

---

## Testing and debugging

### Build commands

```bash
pio run                      # build the default env (x4pro)
pio run -t upload            # build and upload
pio run -e x4pro-gh_release  # build the release env
pio run -t clean             # clean build artifacts
pio check                    # static analysis (cppcheck)
```

### Testing checklist

**What you can verify:**

1. **Build** — build once after the last code edit, with the relevant `pio run`
   target. Do not clean by default, repeat a target that already passed, or rebuild
   after formatting, comment-only or documentation-only changes.
2. **Quality** — `pio check` when relevant, plus `./bin/clang-format-fix` over the
   full tree (matching CI; `-g` alone misses newly added files once committed).
3. **Format** — conventional commit messages, and no `.gitignore`-excluded files
   staged (`*.generated.h`, `.pio/`, `platformio.local.ini`).
4. **CI** — fix GitHub Actions failures before requesting review.

**What only the human tester can verify — flag these for the user:**

5. **Device** — test on hardware.
6. **Heap** — `ESP.getFreeHeap()` above ~50 KB, no leaks across an activity cycle.
7. **Cache** — if a format version moved, delete `/.crosspoint/` and verify the
   re-parse.

### Debugging crashes

1. **Out of memory** (most common) — `LOG_DBG("MEM", "Free heap: %d", ESP.getFreeHeap());`
   around the suspect operation. Watch for allocations over 10 KB, and confirm
   buffers are freed in `onExit()`.
2. **Stack overflow** — `LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));`
   Deep recursion or a large local. Raise the task stack (2048 → 4096) or move the
   buffer to the heap.
3. **Use-after-free** — activity deleted while its task still runs. `vTaskDelete()`
   in `onExit()`, and null pointers after freeing.
4. **Corrupt cache** — delete `/.crosspoint/` on the SD card to force a clean
   re-parse; check the format versions in `docs/file-formats.md`.
5. **Watchdog timeout** — a loop or task blocked over 5 s. Add `vTaskDelay(1)` in
   tight loops; look for blocking I/O.

Verification: read the serial stack trace, compare `ESP.getFreeHeap()` before and
after, check task deletion with `vTaskList()`, and test with `LOG_LEVEL=2`.

---

## Serial monitoring and live debugging

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (colour-coded, recommended)
2. **Plain read**: `cat /dev/cu.usbmodemXXXXX > serial.log` — background it and tail
   the file
3. **VS Code**: the Monitor button in the PlatformIO toolbar

**`pio device monitor` does not work on the X4 Pro.** Its native USB-JTAG/serial
bridge (`303A:1001`) has no line settings, so the monitor dies setting a baud rate:
`termios.error: (19, 'Operation not supported by device')`. Never pass a baud rate
to this transport in any tool — `esptool` corrupts large transfers the same way.

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` every 5 s in the loop.
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` — under 512 bytes, raise the stack.
**Flush**: `logSerial.flush();` to force output before a crash.

Port detection — Windows: `mode`. Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or
`dmesg | grep tty`.

---

## Cache management and invalidation

**Location**: `/.crosspoint/` on the SD card.
**Structure**: `/.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`
**Hash**: `std::hash<std::string>{}(filepath)` — moving or renaming a book gives a
new hash and loses its progress. (The study data under `/.berean/` deliberately does
not work this way; it keys on publication identity.)

Cache is invalidated automatically when:

1. **A file format version changes** — `book.bin` or `section.bin`.
2. **Render settings change** — `SETTINGS.fontFamily`, `fontSize`, `lineSpacing`,
   `extraParagraphSpacing`, `screenMargin`.
3. **Viewport dimensions change**.
4. **The book file is modified** — moved, renamed, or re-content-ed (new hash).

Manual clears:

```bash
rm -rf /path/to/sd/.crosspoint/                     # all caches
rm -rf /path/to/sd/.crosspoint/epub_<hash>/         # one book
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/ # keep progress, drop layout
```

Clear the cache after EPUB parsing errors, corrupt rendering, or any change to
`lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`, or the render
settings in `CrossPointSettings`.

### Format versioning

The constants are the source of truth; `docs/file-formats.md` documents the layout
and lags behind them.

- `BOOK_CACHE_VERSION` — `lib/Epub/Epub/BookMetadataCache.cpp:14`
- `SECTION_FILE_VERSION` — `lib/Epub/Epub/Section.cpp:48`

**Increment the version *before* changing a binary structure.** A mismatch
invalidates and regenerates the cache; a changed structure under an unchanged
version deserialises garbage. Document the change in `docs/file-formats.md`.

```cpp
// lib/Epub/Epub/Section.cpp
constexpr uint8_t SECTION_FILE_VERSION = 43;  // bumped with the struct change

struct PageLine {
  // ... existing fields ...
  uint16_t newField;
};
```

The same discipline applies to every store this firmware writes: a format version a
future build **refuses** rather than reinterprets.

---

## Generated files and build artifacts

**Never hand-edit these** — they are regenerated on every build:

1. **HTML headers** — `src/network/html/*.generated.h`, produced by
   `scripts/build_html.py` from `data/html/` during the PlatformIO `pre:` step.
   Edit the source HTML.
2. **I18n headers** — `lib/I18n/I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`,
   produced by `scripts/gen_i18n.py` from `lib/I18n/translations/*.yaml`. Edit the
   YAML, then run
   `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`.
3. **Build artifacts** — `.pio/`, `build/`, `*.generated.h`,
   `compile_commands.json`. All gitignored.

**Commit source only.** All three generated i18n files are in `.gitignore` and are
regenerated at build time; the same goes for the `.generated.h` HTML headers.

Each translation YAML must contain `_language_name`, `_language_code`, `_order` and
its `STR_*` keys. English (`english.yaml`) is the reference; missing keys elsewhere
fall back to it.

```cpp
#include <I18n.h>
renderer.drawText(FONT_UI, x, y, tr(STR_LOADING), true);
```

**To add a custom font**: put the source in `lib/EpdFont/fontsrc/` (gitignored), run
the conversion script (`lib/EpdFont/README`), add the global font object in
`src/main.cpp`, and add its ID to `src/fontIds.h`.

---

## Local development configuration

`platformio.local.ini` holds personal settings that must **never** be committed:
serial ports, personal debug flags, local paths.

```ini
# platformio.local.ini (gitignored)
[env:x4pro]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1
```

`platformio.ini` is the committed, shared configuration; `platformio.local.ini`
extends it. Use `${base.build_flags}` to extend rather than replace the base flags,
and keep serial ports and credentials out of `platformio.ini`.

---

## Git workflow

### Repository context

Verify before any git operation — remotes differ between clones:

```bash
git branch --show-current
git remote -v
git status --short
```

This repo's own remotes are `origin` (`git@github.com:victorstein/berean-os.git`)
and `crosspoint`, a local path remote pointing at the fork parent. A `gh` command
here needs `--repo victorstein/berean-os` if the CLI resolves a different default.

### Rules

1. Integration branches and PR comparisons target `main`, never `master` or the
   remote's symbolic HEAD. release-please only watches `main`, so a PR targeting
   anything else releases nothing.
2. Never push to any remote, or open or close a PR, without explicit user approval.
   Complete local work and any requested local commit, then stop.
3. Never add Claude, Codex, or assistant self-attribution as a commit co-author or
   generated-by trailer.
4. When a change supersedes or adapts another person's PR, verify the original human
   author from Git or GitHub and add them as `Co-Authored-By`. Skip bot authors.

### Branch naming

```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Refactoring
docs/<topic>                      # Documentation
```

### Commit messages

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`.

```text
feat: add real-time SD download progress bar

Implements progress tracking for publication downloads using the UITheme
progress bar, with heap-safe updates.

Verified on device with a 5 MB download.
```

### When to commit

**Do** when the user explicitly asks, when a feature is complete and tested on
device, when a bug fix is verified, or when a refactor preserves behaviour and
`pio run` succeeds.

**Do not** when the change is untested on hardware, when the build fails or warns,
while still experimenting, when the user has not asked, or when
`.gitignore`-excluded files would be staged — run `git status` and cross-check
against `.gitignore` first.

If uncertain, **ask before committing.**

---

## CI and releases

| Workflow | File | Purpose |
| --- | --- | --- |
| Build check | `.github/workflows/ci.yml` | Compiles `x4pro`; runs the format check |
| Format check | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| PR title lint | `.github/workflows/pr-title-lint.yml` | Enforces conventional PR titles |
| Release | `.github/workflows/release-please.yml` | Opens and merges the release PR |
| Release publish | `.github/workflows/release-publish.yml` | Builds `x4pro-gh_release`, attaches `firmware-x4pro.bin` |

**Releases are automated — never push a tag by hand.** release-please (managed by
stein-infra, not by files in this repo) watches conventional commits on `main`,
opens a `chore(main): release X.Y.Z` PR, self-merges it, cuts a `vX.Y.Z` tag and
creates the release. That fires `release-publish.yml`. A manually pushed tag is
filtered out and does nothing.

Consequences worth holding onto:

- **The PR title is the release input.** PRs are squash-merged, so a
  non-conventional title contributes nothing to the changelog and may skip the
  version bump entirely.
- **`[berean] version` in `platformio.ini` is bumped for you**, via release-please
  `extra-files` and the block-form `x-release-please-start-version` markers. Do not
  edit it by hand, and do not convert those markers to the inline form —
  `scripts/git_branch.py` reads that key with a parser that keeps inline comments,
  which would put the comment inside `BEREAN_VERSION`.
- **Do not hand-create** `release-please-config.json`,
  `.release-please-manifest.json`, `version.txt` or
  `.github/workflows/release-please.yml`. They are pushed by stein-infra's tofu.
- **`release-publish.yml` is gated on the repository name**
  (`release-publish.yml:106`). If that guard names the wrong repo, the release is
  cut with no firmware asset, the workflow goes green, and OTA finds a release whose
  asset does not exist.

**Fix CI failures before requesting review.** A format failure means
`./bin/clang-format-fix` over the whole tree, not `-g`.

### Flashing

**OTA is the default path.** The device updates itself from this repo's releases
over WiFi (Settings → check for updates). Reach for a cable only when you need a
**dev** build — OTA installs `x4pro-gh_release`, which has `LOG_LEVEL=0` and no
serial logging.

Cable-free side-load of a dev build, when you do need logs: `POST /upload?path=/`
the `firmware.bin` to the device web server, then *Settings → SD firmware update*.
Suppress `Expect: 100-continue` (`curl -H "Expect:"`) or the transfer hangs — the
ESP32 web server never answers it.
