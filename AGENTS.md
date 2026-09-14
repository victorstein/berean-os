# bereanOS

A study firmware for the **Xteink X4 Pro**, hard-forked from `crosspoint-x4pro` on
2026-09-13. Design: `docs/superpowers/specs/2026-09-13-berean-os-design.md`.
Workflow: `docs/contributing/development-workflow.md`.

The Bible at the centre, the two weekly meeting publications, catalog browsing,
and tagged passages with global tags. No notes, no JW Library interop, no
on-device full-text search.

---

## Read this before the CrossPoint guide below

Everything after the separator is inherited from CrossPoint and is being stripped
in Phase 0. Where it conflicts with this section, **this section wins.** The
inherited text is kept because most of it — HAL rules, memory safety, SdFat
threading, cache versioning — is still correct and hard-won.

### One device, and it is not a C3

The inherited guide opens with "380KB RAM is the hard ceiling" for the ESP32-C3.
**This project targets only the X4 Pro: ESP32-S3, dual core, 8 MB PSRAM, 16 MB
flash** (`platformio.ini:239,250`). Multi-board support is out of scope.

That is a licence to use PSRAM deliberately, not a licence to stop budgeting. S3
PSRAM is on an external SPI bus: roughly an order of magnitude slower than internal
SRAM, unusable from an ISR, unusable while the flash cache is suspended, and
DMA-constrained.

| Lives in PSRAM | Lives in internal SRAM |
|---|---|
| Catalog index while Buscar is open (~217 KB) | Framebuffer (48 KB) |
| Unit index pages being built or queried | Selection geometry, render hot path |
| Download and inflate buffers | ISR state, anything `IRAM_ATTR` touches |

Internal SRAM is still the same ~380 KB-class resource the inherited guide
disciplines you about. Every rule below about stack size, heap fragmentation,
`constexpr`, and string policy still applies to it.

### The input hardware, as confirmed on the bench

`freeink-sdk/docs/xteink-x4pro-support.md`, section "Input — digital buttons +
capacitive Home":

- **Left** nav button — GPIO0 (also a boot-strap pin; fine unless held at reset)
- **Right** nav button — GPIO7
- **Power** — GPIO3
- **Home** — a capacitive key bit on the GT911 (`0x814E & 0x10`), **not a GPIO**
- GT911 capacitive touchscreen

**There is no Back button and no Confirm button.** The inherited "Logical Button
Mapping" section models four remappable front buttons plus two side buttons; most
of that describes hardware this device does not have.

`MappedInputManager` cannot simply be deleted — it is in the `Activity` base-class
constructor (`src/activities/Activity.h:22,28-29`), spans 418 references across 121
files, and *implements* this device's Back via a left-edge swipe
(`src/MappedInputManager.cpp:266,301`). It may only be removed in the same change
that lands its replacement.

### Storage discipline — the rule that protects the only irreplaceable data

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

### Evidence rules

The inherited "Anti-Hallucination" rule stands and is sharpened: **a claim about
this codebase needs a file and a line, and the line must have been read.** The two
claims most likely to be wrong and most expensive to discover late are "this
already exists" and "this is already tested."

Specifically do not repeat these, all of which were asserted and disproven during
design:

- `sdkconfig.x4pro`'s `CONFIG_BT_NIMBLE_*` lines do **not** mean BLE is compiled
  in. Zero NimBLE objects link; `lib_deps` has no BLE library. The stack's flash
  cost is entirely unpaid.
- `data-pnum` is **not** the addressable unit in JW publications. It is the printed
  paragraph number on a `<span class="parNum">`. The structural address is
  `data-pid` on the block element.
- `VerseAnchors` does **not** generalise to other markers. It hardcodes the `id`
  attribute and a `chapter%u_verse%u` grammar (`VerseAnchors.cpp:30-41`).

### Scope

Out, deliberately: notes, `.jwlibrary` interop, on-device full-text search, and
everything CrossPoint carries that is not JW study (OPDS, KOSync, dictionary, TXT
and XTC readers, other board targets).

The inherited `SCOPE.md` describes CrossPoint's product, not this one. It is kept
for the reasoning, not the rules.

---

# CrossPoint Reader Development Guide

Project: Open-source e-reader firmware for Xteink X4 (ESP32-C3)
Mission: Provide a lightweight, high-performance reading experience focused on EPUB rendering on constrained hardware.

## AI Agent Identity and Cognitive Rules

* Role: Senior Embedded Systems Engineer (ESP-IDF/Arduino-ESP32 specialized).
* Primary Constraint: 380KB RAM is the hard ceiling. Stability is non-negotiable.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of libraries or ESP-IDF functions. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the freeink-sdk source or the FreeInk SDK docs (https://freeink.org/llms.txt for an LLM-readable index) first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage).
* Resource Justification: You must justify any new heap allocation (new, malloc, std::vector) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or checking a specific cache file).

---

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection

```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

**Detection Required**: Run `uname -s` at session start to determine platform

### Platform-Specific Behaviors

- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use `find`+`xargs`)
- **Linux/WSL**: Full bash, Unix paths, native glob support

**Cross-Platform Code Formatting**:

```bash
./bin/clang-format-fix -g   # while working: only Git-modified files
./bin/clang-format-fix      # before committing: the whole tree, as CI does
```

**Run the unsuffixed form before you commit.** CI runs `./bin/clang-format-fix` over the entire
tree (`.github/workflows/ci.yml`), while `-g` only reaches files Git currently reports as
modified. A file you create and commit is no longer "modified", so `-g` silently skips it and the
CI format job fails on work that looked clean locally.

Never invoke or probe `clang-format` directly. The repository wrapper is the only sanctioned entry point.

---

## Platform and Hardware Constraints

### Hardware Specs

* MCUs: ESP32-C3 (single-core RISC-V @ 160MHz) and ESP32-S3 (`sticky`, dual-core Xtensa LX7)
* RAM: ~380KB usable on ESP32-C3 (VERY LIMITED - primary project constraint)
  * **NO PSRAM on C3**.
  * **Single Buffer Mode**: Only ONE 48KB framebuffer (not double-buffered)
* Flash: 16MB (Instruction storage and static data)
* Display: 800x480 E-Ink (Slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD Card (Used for books and aggressive caching)

### The Resource Protocol

1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Large constant data (UI strings, lookup tables) MUST be marked static const to stay in Flash (Instruction Bus), freeing DRAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This moves computation to compile time, enables dead-branch elimination, and guarantees flash placement. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SD Persistence Throttling: Settings, state, credentials, and other `PersistableStore` JSON files live on SD under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Guard redundant writes and debounce progress saves to avoid serialization, SD I/O, and `storageMutex` cost.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

---

## Project Architecture

### Build System: PlatformIO

**PlatformIO is BOTH a VS Code extension AND a CLI tool**:

1. **VS Code Extension** (Recommended):
   
   * Extension ID: `platformio.platformio-ide` (see `.vscode/extensions.json`)
   
   * Provides: Toolbar buttons, IntelliSense, integrated build/upload/monitor
   
   * Configuration: `.vscode/c_cpp_properties.json`, `.vscode/tasks.json`
   
   * Usage: Click Build (✓), Upload (→), or Monitor (🔌) buttons

2. **CLI Tool** (`pio` command):
   
   * **Installation**: Python package (typically `pip install platformio`)
   
   * **Windows Location**: `C:\Users\<user>\AppData\Local\Programs\Python\Python3xx\Scripts\pio.exe`
   
   * **Verify**: `which pio` (Git Bash) or `where.exe pio` (cmd)
   
   * **Usage**: `pio run`, `pio run -t upload`, etc.

**Configuration Files**:

* `platformio.ini`: Main build configuration (committed to git)
* `platformio.local.ini`: Local overrides (gitignored, create if needed)
* `partitions.csv`: ESP32 flash partition layout

### Build Environment

* **Standard**: C++20 (`-std=c++2a`). No Exceptions, No RTTI.
* **Logging**: ALWAYS use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` from `Logging.h`. Raw Serial output is deprecated.
* **Environments** (in `platformio.ini`):
  * `default`: Development (LOG_LEVEL=2, serial enabled)
  * `gh_release`: Production (LOG_LEVEL=0)
  * `gh_release_rc`: Release candidate (LOG_LEVEL=1)
  * `slim`: Minimal build (no serial logging)

### Critical Build Flags

These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1   // Avoid zlib name conflicts
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**DESTRUCTOR_CLOSES_FILE implications**:

- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:

- Only ONE framebuffer exists (not double-buffered)
- Grayscale rendering requires temporary buffer allocation (`renderer.storeBwBuffer()`)
- Must call `renderer.restoreBwBuffer()` to free temporary buffers
- See [lib/GfxRenderer/GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp) for malloc usage

### Directory Structure

* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalDisplay, HalGPIO, HalStorage)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based binary cache for EPUB metadata and pre-rendered layout sections

### Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class    | Wraps SDK Class | Purpose               | Singleton Macro |
| ------------ | --------------- | --------------------- | --------------- |
| `HalDisplay` | `EInkDisplay`   | E-ink display control | *(none)*        |
| `HalGPIO`    | `InputManager`  | Button input handling | *(none)*        |
| `HalStorage` | `SDCardManager` | SD card file I/O      | `Storage`       |

**Location**: [lib/hal/](lib/hal/)

**Why HAL?**

- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:

```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see DESTRUCTOR_CLOSES_FILE above).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:

- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method call (read, write, seek, close) takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

---

## Coding Standards

### Naming Conventions

* Classes: PascalCase (e.g., EpubReaderActivity)
* Methods/Variables: camelCase (e.g., renderPage())
* Constants: UPPER_SNAKE_CASE (e.g., MAX_BUFFER_SIZE)
* Private Members: memberVariable (no prefix)
* File Names: Match Class names (e.g., EpubReaderActivity.cpp)

### Header Guards

* Use #pragma once for all header files.

### Comment Style

* Keep comments short and write them for the merged state, as if the code had always worked this way.
* Remove before/after narration, investigation measurements, and rationale that belongs in the commit message.
* Keep only non-obvious mechanism, field/parameter meaning, or the reason a special case exists.

### Memory Safety and RAII

* Smart Pointers: Prefer std::unique_ptr. 
* RAII: Use destructors for cleanup. Call `vTaskDelete()` explicitly for deterministic task release. Do NOT call `file.close()` on local `FsFile` variables — `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit (see Critical Build Flags).

### ESP32-C3 Platform Pitfalls

#### `std::string_view` and Null Termination

`string_view` is *not* null-terminated. Passing `.data()` to any C-style API (`drawText`, `snprintf`, `strcmp`, SdFat file paths) is undefined behaviour when the view is a substring or a view of a non-null-terminated buffer.

**Rule**: `string_view` is safe only when passing to C++ APIs that accept `string_view`. For any C API boundary, convert explicitly:

```cpp
// WRONG - undefined behaviour if view is a substring:
renderer.drawText(font, x, y, myView.data(), true);

// CORRECT - guaranteed null-terminated:
renderer.drawText(font, x, y, std::string(myView).c_str(), true);

// CORRECT - for short strings, use a stack buffer:
char buf[64];
snprintf(buf, sizeof(buf), "%.*s", (int)myView.size(), myView.data());
```

#### `IRAM_ATTR` and Flash Cache Safety

All code runs from flash via the instruction cache. During internal-flash operations such as OTA writes or NVS updates, the cache is briefly suspended. Any code that can execute during this window — ISRs in particular — must reside in IRAM or it will crash silently.

```cpp
// ISR handler: must be in IRAM
void IRAM_ATTR gpioISR() { ... }

// Data accessed from IRAM_ATTR code: must be in DRAM, never a flash const
static DRAM_ATTR uint32_t isrEventFlags = 0;
```

**Rules**:

- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` (a flash-resident `static const` will fault)
- Normal task code does **not** need `IRAM_ATTR`

#### ISR vs Task Shared State

`xSemaphoreTake()` (mutex) **cannot** be called from ISR context — it will crash. Use the correct primitive for each communication direction:

| Direction                       | Correct primitive                                  |
| ------------------------------- | -------------------------------------------------- |
| ISR → task (data)               | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()`     |
| ISR → task (signal)             | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task                     | `xSemaphoreTake()` / mutex                         |
| Simple flag (single writer ISR) | `volatile bool` + `portENTER_CRITICAL_ISR()`       |

#### RISC-V Alignment

ESP32-C3 faults on unaligned multi-byte loads. Never cast a `uint8_t*` buffer to a wider pointer type and dereference it directly. Use `memcpy` for any unaligned read:

```cpp
// WRONG — faults if buf is not 4-byte aligned:
uint32_t val = *reinterpret_cast<const uint32_t*>(buf);

// CORRECT:
uint32_t val;
memcpy(&val, buf, sizeof(val));
```

This applies to all cache deserialization code and any raw buffer-to-struct casting. `__attribute__((packed))` structs have the same hazard when accessed via member reference.

#### Template and `std::function` Bloat

Each template instantiation generates a separate binary copy. `std::function<void()>` adds ~2–4 KB per unique signature and heap-allocates its closure. Avoid both in library code and any path called from the render loop:

```cpp
// Avoid — heap-allocating, large binary footprint:
std::function<void()> callback;

// Prefer — zero overhead:
void (*callback)() = nullptr;

// For member function + context (common activity callback pattern):
struct Callback { void* ctx; void (*fn)(void*); };
```

When a template is necessary, limit instantiations: use explicit template instantiation in a `.cpp` file to prevent the compiler from generating duplicates across translation units.

---

### Error Handling Philosophy

**Source**: [src/main.cpp:132-143](src/main.cpp), [lib/GfxRenderer/GfxRenderer.cpp:10](lib/GfxRenderer/GfxRenderer.cpp)

**Pattern Hierarchy**:

1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states (framebuffer missing)
4. **ESP.restart()**: Only for recovery (OTA complete)

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

### Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:

```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Cover image buffers: [HomeActivity.cpp:166](src/activities/home/HomeActivity.cpp)
- Bitmap rendering: [GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp)

### Heap Allocation with `new`: Always Use `makeUniqueNoThrow`

**CRITICAL**: With `-fno-exceptions`, bare `new` on OOM calls `abort()` — it does NOT return `nullptr`. Always use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, which wraps `new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and automatically frees on scope exit.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

// Pass to C APIs via .get(); unique_ptr frees automatically on return
someApi(buf.get(), size);
```

**`new (std::nothrow)` directly is acceptable** when the object must be passed to a C API that takes ownership and calls `delete` itself:

```cpp
auto* obj = new (std::nothrow) MyClass(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }
sdkApiThatTakesOwnership(obj);  // SDK calls delete
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **NEVER use bare `new`** — always `makeUniqueNoThrow` or `new (std::nothrow)`
- **ALWAYS `LOG_ERR` before returning false** on OOM
- **Use `.get()`** to pass the raw pointer to C-style APIs; ownership stays with the `unique_ptr`
- **`new (std::nothrow)` directly only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)

---

## UI and Orientation Guidelines

### Orientation-Aware Logic

* No Hardcoding: Never assume 800 or 480. Use renderer.getScreenWidth() and renderer.getScreenHeight().
* Viewable Area: Use renderer.getOrientedViewableTRBL() to stay within physical bezel margins.

### Logical Button Mapping

**Source**: [src/MappedInputManager.cpp:20-55](src/MappedInputManager.cpp)

Constraint: Physical button positions are fixed on hardware, but their logical functions change based on user settings and screen orientation.

**Button Categories**:

1. **Physical Fixed** (Up/Down side buttons):
   
   - `Button::Up` → Always `HalGPIO::BTN_UP`
   
   - `Button::Down` → Always `HalGPIO::BTN_DOWN`

2. **User Remappable** (Front buttons):
   
   - `Button::Back` → Maps to `SETTINGS.frontButtonBack` (hardware index)
   
   - `Button::Confirm` → Maps to `SETTINGS.frontButtonConfirm`
   
   - `Button::Left` → Maps to `SETTINGS.frontButtonLeft`
   
   - `Button::Right` → Maps to `SETTINGS.frontButtonRight`

3. **Reader-Specific** (Page navigation with optional swap):
   
   - `Button::PageBack` → Uses side button (swappable via `SETTINGS.sideButtonLayout`)
   
   - `Button::PageForward` → Uses side button (swappable)

**Implementation**:

- Activities use **logical buttons** (e.g., `Button::Confirm`)
- `MappedInputManager` translates to **physical hardware buttons**
- User can remap front buttons in settings
- Orientation changes handled separately by renderer coordinate transforms

**Rule**: Always use `MappedInputManager::Button::*` enums, never raw `HalGPIO::BTN_*` indices (except in ButtonRemapActivity).

### UITheme (The GUI Macro)

* Rule: All UI rendering must go through the GUI macro (UITheme). 
* Do not hardcode fonts, colors, or positioning. This ensures orientation-aware layout consistency.

---

## Common Patterns

### Singleton Access

**Available Singletons**:

```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                   // Current theme
#define Storage HalStorage::getInstance()            // SD card I/O
#define I18N I18n::getInstance()                     // Internationalization
```

### Activity Lifecycle and Memory Management

**Source**: [src/main.cpp:132-143](src/main.cpp)

**CRITICAL**: Activities are **heap-allocated** and **deleted on exit**.

```cpp
// main.cpp navigation pattern
void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;  // Activity deleted here!
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;  // Heap-allocated activity
  currentActivity->onEnter();
}
```

**Memory Implications**:

- Activity navigation = `delete` old activity + `new` create next activity
- Any memory allocated in `onEnter()` MUST be freed in `onExit()`
- FreeRTOS tasks MUST be deleted in `onExit()` before activity destruction
- Member `FsFile` handles MUST be closed in `onExit()` (local `FsFile` variables auto-close via destructor)

**Activity Pattern**:

```cpp
void onEnter()  { Activity::onEnter(); /* alloc: buffer, tasks */ render(); }
void loop()     { mappedInput.update(); /* handle input */ }
void onExit()   { /* free: vTaskDelete, free buffer, close member FsFiles */ Activity::onExit(); }
```

**Critical**: Free resources in reverse order. Delete tasks BEFORE activity destruction.

### FreeRTOS Task Guidelines

**Source**: [src/activities/util/KeyboardEntryActivity.cpp:45-50](src/activities/util/KeyboardEntryActivity.cpp)

**Pattern**: See Activity Lifecycle above. `xTaskCreate(&taskTrampoline, "Name", stackSize, this, 1, &handle)`

**Stack Sizing** (in BYTES, not words):

- **2048**: Simple rendering (most activities)
- **4096**: Network, EPUB parsing
- Monitor: `uxTaskGetStackHighWaterMark()` if crashes

**Rules**: Always `vTaskDelete()` in `onExit()` before destruction. Use mutex if shared state.

### Global Font Loading

**Source**: [src/main.cpp:40-115](src/main.cpp)

**All fonts are loaded as global static objects** at firmware startup:

- Noto Serif: 12, 14, 16, 18pt (4 styles each: regular, bold, italic, bold-italic)
- Noto Sans: 12, 14, 16, 18pt (4 styles each)
- Ubuntu UI fonts: 10, 12pt (2 styles)

**Total**: ~80+ global `EpdFont` and `EpdFontFamily` objects

**Compilation Flag**:

```cpp
#ifndef OMIT_FONTS
  // Most fonts loaded here
#endif
```

**Implications**:

- Fonts stored in **Flash** (marked as `static const` in `lib/EpdFont/builtinFonts/`)
- Font rendering data cached in **DRAM** when first used
- `OMIT_FONTS` can reduce binary size for minimal builds
- Font IDs defined in [src/fontIds.h](src/fontIds.h)

**Usage**:

```cpp
#include "fontIds.h"

renderer.insertFont(FONT_UI_MEDIUM, ui12FontFamily);
renderer.drawText(FONT_UI_MEDIUM, x, y, "Hello", true);
```

---

## Testing and Debugging

### Build Commands

**Via CLI**:

```bash
# Build firmware (default environment)
pio run

# Build and upload to device
pio run -t upload

# Build specific environment
pio run -e gh_release

# Clean build artifacts
pio run -t clean
```

**Via VS Code**:

* Use PlatformIO toolbar: Build (✓), Upload (→), Clean (🗑️)
* Or Command Palette: `PlatformIO: Build`, `PlatformIO: Upload`, etc.

### Monitoring and Debugging

```bash
# Enhanced monitor with color/logging (recommended)
python3 scripts/debugging_monitor.py

# Standard PlatformIO monitor
pio device monitor
```

**Via VS Code**: Click Monitor (🔌) button in PlatformIO toolbar

### Code Quality

```bash
# Static analysis (cppcheck)
pio check

# Format only Git-modified C/C++ files, on every host
./bin/clang-format-fix -g

# Format the whole tree -- what CI checks. Run this before committing.
./bin/clang-format-fix
```

Do not run raw `clang-format` or probe it with `command -v`; use the wrapper even for diagnostics.

### Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   
   - Monitor heap usage throughout activity lifecycle
   
   - Check if large allocations (>10KB) occur before crash
   
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   
   - Occurs during deep recursion or large local variables
   
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   
   - Move large buffers to heap with malloc

3. **Use-After-Free**:
   
   - Activity deleted but task still running
   
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   
   - Delete `.crosspoint/` directory on SD card
   
   - Forces clean re-parse of all EPUBs
   
   - Check file format versions in [docs/file-formats.md](docs/file-formats.md)

5. **Watchdog Timeout**:
   
   - Loop/task blocked for >5 seconds
   
   - Add `vTaskDelay(1)` in tight loops
   
   - Check for blocking I/O operations

**Verification Steps**:

1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Git Workflow and Repository Awareness

### Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:

- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):

```bash
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Check working tree status
git status --short
```

**Example Output** (forked repository):

```text
origin      https://github.com/<your-username>/crosspoint-reader.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

### Git Operation Rules

1. Integration branches and PR comparisons target `main`, not `master` or the remote's symbolic HEAD. release-please only watches `main`, so a PR targeting anything else releases nothing.
2. Never push to any remote or open/close a PR without explicit user approval. Complete local work and any requested local commit, then stop.
3. If the user explicitly approves a push, inspect remotes again and use `fork` for the feature branch unless the user specifies otherwise.
4. Never add Claude, Codex, or assistant self-attribution as a commit co-author or generated-by trailer.
5. When a change supersedes or adapts another person's PR, verify the original human author from Git/GitHub and add that person as `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

**For feature/fix branches**:

```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:

- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:

```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

### When to Commit

**DO commit when**:

- User explicitly requests: "commit these changes"
- Feature is complete and tested on device
- Bug fix is verified working
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:

- Changes are untested on actual hardware
- Build fails or has warnings
- Experimenting or debugging in progress
- User hasn't explicitly requested commit
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain, ASK before committing.**

---

## Generated Files and Build Artifacts

### Files Generated by Build Scripts

**NEVER manually edit these files** - they are regenerated automatically:

1. **HTML Headers** (generated by `scripts/build_html.py`):
   
   - `src/network/html/*.generated.h`
   
   - **Source**: HTML templates in `data/html/` directory
   
   - **Triggered**: During PlatformIO `pre:` build step
   
   - **To modify**: Edit source HTML in `data/html/`, not generated headers

2. **I18n Headers** (generated by `scripts/gen_i18n.py`):
   
   - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`, `lib/I18n/I18nStrings.cpp`
   
   - **Source**: YAML translation files in `lib/I18n/translations/` (one per language)
   
   - **To modify**: Edit source YAML files, then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
   
   - **Commit**: Source YAML files only. All three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are in `.gitignore` and regenerated at build time.

3. **Build Artifacts** (in `.gitignore`):
   
   - `.pio/` - PlatformIO build output
   
   - `build/` - Compiled binaries
   
   - `*.generated.h` - Any auto-generated headers
   
   - `compile_commands.json` - LSP/IDE metadata

### Modifying Generated Content Workflow

**To change HTML pages**:

1. Edit source: `data/html/<pagename>.html`
2. Build: `pio run` (auto-triggers `scripts/build_html.py`)
3. Generated headers update: `src/network/html/<pagename>Html.generated.h`
4. **Commit ONLY** source HTML, NOT generated `.generated.h` files

**To add/modify translations (i18n)**:

1. Edit or add YAML file: `lib/I18n/translations/<language>.yaml`
   
   - Each file must contain: `_language_name`, `_language_code`, `_order`, and `STR_*` keys
   
   - English (`english.yaml`) is the reference; missing keys in other languages fall back to English
2. Run generator: `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
3. Generated files update: `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`
4. **Commit** source YAML files only. All three generated files are in `.gitignore` and regenerated at build time.

**To use translated strings in code**:

```cpp
#include <I18n.h>
// Use tr() macro with StrId enum (defined in generated I18nKeys.h)
renderer.drawText(FONT_UI, x, y, tr(STR_LOADING), true);
```

**To add custom fonts**:

1. Place source fonts in `lib/EpdFont/fontsrc/` (gitignored)
2. Run conversion script (see `lib/EpdFont/README`)
3. Update global font objects in `src/main.cpp:40-115`
4. Add font ID constant to `src/fontIds.h`

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:

- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:

```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:

1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:

- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):

1. ✅ **Build**: Build once after the last code edit with the relevant `pio run` target. Do not clean by default, repeat a target that already passed, or rebuild after formatting/comment-only/documentation-only changes.
2. ✅ **Quality**: `pio check` when relevant + `./bin/clang-format-fix` (full tree, matching CI; `-g` alone misses newly added files once they are committed)
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Heap**: `ESP.getFreeHeap()` > 50KB, no leaks
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow      | File                                        | Purpose                |
| ------------- | ------------------------------------------- | ---------------------- |
| Build Check   | `.github/workflows/ci.yml`                  | Verifies code compiles |
| Format Check  | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Release Publish | `.github/workflows/release-publish.yml`   | Builds and attaches release firmware; dispatched by release-please |
| RC Build      | `.github/workflows/release_candidate.yml`   | Release candidates     |

### Releases and flashing

**Releases are automated — never push a tag by hand.** `release-please` (managed by stein-infra,
not by files in this repo) watches conventional commits on `main`, opens a `chore(main): release
X.Y.Z` PR, self-merges it, cuts a `vX.Y.Z` tag and creates the release. That fires
`release-publish.yml`, which builds all four boards and attaches the binaries. A manually pushed tag
is filtered out and does nothing.

Consequences worth holding onto:

- **The PR title is the release input.** A non-conventional title contributes nothing to the
  changelog and may skip the version bump.
- **`[crosspoint] version` in `platformio.ini` is bumped for you**, via release-please `extra-files`
  and the block-form `x-release-please-start-version` markers. Do not edit it by hand, and do not
  convert those markers to the inline form — `scripts/git_branch.py` reads that key with a parser
  that keeps inline comments, which would put the comment inside `CROSSPOINT_VERSION`.
- **Do not hand-create** `release-please-config.json`, `.release-please-manifest.json`,
  `version.txt` or `.github/workflows/release-please.yml`. They are pushed by stein-infra's tofu.

**Flashing: OTA is the default path.** The device updates itself from this repo's releases over
WiFi (Settings → check for updates). Reach for a cable only when you need a **dev** build — OTA
installs `x4pro-gh_release`, which has `LOG_LEVEL=0` and no serial logging.

Cable-free side-load of a dev build, when you do need logs: `POST /upload?path=/` the `firmware.bin`
to the device web server, then *Settings → SD firmware update*. Suppress `Expect: 100-continue`
(`curl -H "Expect:"`) or the transfer hangs — the ESP32 web server never answers it.

**Rules**:

- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Format check fails → Run `./bin/clang-format-fix` (no `-g`; CI checks the whole tree)
- Build check fails → Fix compile errors

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Plain read**: `cat /dev/cu.usbmodemXXXXX > serial.log` — background it and tail the file
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

**`pio device monitor` does not work on the X4 Pro.** Its native USB-JTAG/serial bridge
(`303A:1001`) has no line settings, so the monitor dies setting a baud rate:
`termios.error: (19, 'Operation not supported by device')`. Never pass a baud rate to this
transport in any tool — `esptool` corrupts large transfers the same way.

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`

---

## Cache Management and Invalidation

### Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`

**Hash**: `std::hash<std::string>{}(filepath)` → Moving/renaming file = new hash = lost progress

### Cache Invalidation Rules

**Cache is automatically invalidated when**:

1. **File format version changes** (see `docs/file-formats.md`)
   
   - `book.bin` version number incremented
   
   - `section.bin` version number incremented
2. **Render settings change**:
   
   - Font family or size (`SETTINGS.fontFamily`, `SETTINGS.fontSize`)
   
   - Line spacing (`SETTINGS.lineSpacing`)
   
   - Paragraph spacing (`SETTINGS.extraParagraphSpacing`)
   
   - Screen margins (`SETTINGS.screenMargin`)
3. **Viewport dimensions change**:
   
   - Screen orientation change
   
   - Display resolution change
4. **Book file modified**:
   
   - Moved, renamed, or content changed (new hash)

**Manual Cache Clear** (safe operations):

```bash
# Delete ALL caches (forces full regeneration)
rm -rf /path/to/sd/.crosspoint/

# Delete specific book cache
rm -rf /path/to/sd/.crosspoint/epub_<hash>/

# Keep progress, delete only rendered sections
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/
```

**When to Clear Cache**:

- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

### Cache File Format Versioning

**Source**: `lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`

**Current Versions** (as of docs/file-formats.md):

- `book.bin`: **Version 7** (metadata structure)
- `section.bin`: **Version 25** (layout structure)

**Version Increment Rules**:

1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in `docs/file-formats.md`

**Example** (incrementing section format version):

```cpp
// lib/Epub/Epub/Section.cpp
static constexpr uint8_t SECTION_FILE_VERSION = 26;  // Was 25, now 26

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```

---

Philosophy: We are building a dedicated e-reader, not a Swiss Army knife. If a feature adds RAM pressure without significantly improving the reading experience, it is Out of Scope.
