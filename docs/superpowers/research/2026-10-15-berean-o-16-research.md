# Research: berean-o-16 (T3) — Predicación screens and the reader entry contract

Run `berean-os-20261015-predicaci-n-193x`, task `t3`. Spec:
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` (§3, §4, §6 contract, §8, §9).
Every claim below was read in this worktree at `aed8879a`, which is also the tip of
`origin/batch/20261015-predicaci-n-193x` (`git rev-parse` on both printed `aed8879ac14e…`).

## 1. Toolchain, as installed

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version`; `pio` is **not** on PATH (`which pio` → not found) |
| Platform | pioarduino espressif32 55.03.37 | `platformio.ini:15` |
| ArduinoJson | 7.4.2 | `platformio.ini:155` |
| clang-format | 21.1.8 | `.venv/bin/clang-format --version` (bootstrap has run; `freeink-sdk` submodule at `7dde134`) |
| CMake | 4.4.2 | `cmake --version` |
| Python | 3.14.7 | `python3 --version` |

No `.pio/` or `build/` exists yet in this worktree, so the first `pio run -e x4pro` and the first host
CMake configure will be cold.

Warnings: `scripts/enable_repo_warnings.py:30` adds `-Wall` to this repo's own sources, so `-Wswitch`
is on for `src/`. Host tests build with `-Wall -Wextra -pedantic` (`test/CMakeLists.txt:42-46`).

## 2. What the siblings already landed (read before planning)

**T1 — `lib/Preaching/Preaching/`** (host suite `test/preaching/`, registered at `test/CMakeLists.txt:148`):

- `IntroSection.h`: `QUESTION_COUNT = 20`, `SLOT_COUNT = 21`, `INDEX_SLOT = 0`; `BASENAMES[21]` as
  `constexpr const char*` (slot n = question n); `nextQuestion(int)` → follow-up or 0;
  `stripTocPrefix(std::string_view)` returns a view into its argument, or the whole label.
- `QuestionParser.h`: `Block { BlockKind kind (Heading|Verse|Text); std::string text, reference, href; }`,
  `Question { std::string title; std::vector<Block> blocks; }`. The parser is fed through
  `bool feed(const char*, size_t, bool isFinal)` — the `SpineHtmlStream::ChunkSink` contract — and
  `take(Question&)` moves the result out once, nothing partial. `outOfMemory()` separates OOM from bad
  markup. Bounds: `MAX_BLOCKS = 64`, `MAX_TEXT_BYTES = 8 KiB`. `parseQuestion(const char*, size_t, Question&)`
  is the one-shot form.
- Note the shape: `feed` is a member, but `ChunkSink` is `bool (*)(void* ctx, const char*, size_t, bool)`
  (`src/activities/reader/SpineHtmlStream.h:20`), so T3 needs a one-line trampoline casting `ctx` to
  `QuestionParser*`.

**T2 — `src/study/`** (host tests in `test/storage_io/PreachingPassagesTest.cpp`,
`PreachingTopicsFileTest.cpp`):

- `PreachingTopicsFile` (`src/study/PreachingTopicsFile.h`): `load()` reads `tags.json` **and**
  `/.berean/preaching.json`; `tags()` returns the picked ids; `palette()` returns the loaded
  `study::TagPalette`; `save(picked)` (T5's concern). Loop task only (header comment, `:16-18`).
  So Mis temas gets both the picks and the palette (for `activeIdsByName()` and `name(TagId)`,
  `lib/StudyStore/StudyStore/TagPalette.h:53,61`) from one `load()`.
- `PreachingPassages` (`src/study/PreachingPassages.h`), a namespace, not a class:
  - `Status { Ok, Missing, Unreadable, OutOfMemory }`, `isAvailable(status)`.
  - `countByTag(tags, ArduinoJson::Allocator* json, std::vector<TagCount>& out)` — one count per input
    tag, same order, zero included.
  - `passagesWithTag(tag, const PassageDoc::Allocators&, std::vector<Passage>& out)` —
    `Passage { std::string reference; study::PassageText text; study::Unit start; uint16_t spine; }`,
    sorted spine then Unit (`PreachingPassages.cpp:197-200`).
  - The header requires the firmware to pass `PsramJsonAllocator::json()` / `::passageDoc()`
    (`PreachingPassages.h:17-20`, `src/study/PsramJsonAllocator.h:21-25`), or the parse lands in
    internal SRAM.
  - Both open `/.berean/passages/bible.json` with `Storage.openFileForRead` and return `Missing` when
    it does not exist (`PreachingPassages.cpp:68-71`).

## 3. Files that own the behaviour, and what T3 changes in each

| File | Today | T3 |
|---|---|---|
| `src/activities/preaching/` | does not exist (`ls src/activities`) | new: `PreachingActivity`, `PreachingTopicActivity`, any pure layout helper |
| `src/activities/reader/ReaderEntryIntent.h` | `Kind { None, OpenAt, GoTo, Search, Tags }`, `Route { None, Locate, ChapterGrid, TocList, Search, Highlights }`, `route()` exhaustive over `Kind` (`:14-15,38-52`) | add `Kind::OpenHref`, `char href[64]`, its route, `PreachingOrigin` |
| `src/activities/ActivityManager.{h,cpp}` | `goTo…` wrappers over `replaceActivity` (`ActivityManager.h:72-80`) | add `goToPreaching(const PreachingOrigin& = {})` |
| `lib/I18n/translations/{english,spanish}.yaml` | no `PREACHING` key (`grep -rn PREACHING lib/I18n/translations/` is empty); 478 / 425 `STR_` lines | add every §8 key |
| `test/ui_layout/ReaderEntryIntentTest.cpp` | six tests over `route()` and `openAt` | add `OpenHref` tests |
| `src/activities/reader/EpubReaderActivity.cpp:1104` | `switch (ReaderEntryIntent::route(...))` over `Route` with no `default` | needs a minimal case if T3 adds a `Route` value (see §6) |

`.claude/agents/ui-dev.md` lists the translation YAMLs as report-don't-edit shared files; the brief
overrides that for this run ("You own `lib/I18n/translations/` in this run").

## 4. Control flow today

### Activity replace, push and pop (`src/activities/ActivityManager.cpp`)

- `replaceActivity` (`:184-196`) only sets `pendingActivity` / `PendingAction::Replace` when an activity
  is current; the switch happens in `loop()`.
- `loop()` Replace branch (`:143-150`): `exitActivity(lock)` — `currentActivity->onExit()` then
  `reset()`, so the destructor runs too (`:176-182`) — then, for each stacked activity from the top,
  `onExit()` then `pop_back()` (destructor). **Only after that** does it move in the pending activity and
  call its `onEnter()` (`:157-160`).
- Push (`:151-154`) moves the current activity onto the stack without `onExit`; Pop (`:93-137`) runs
  the popped activity's `onExit`/destructor, then the parent's `resultHandler` with the moved
  `ActivityResult`, then `requestUpdate()`.
- After an `onEnter`, the `while (pendingAction != None)` loop `continue`s (`:162-163`), so an `onEnter`
  that calls `startActivityForResult` is pushed in the same `loop()` pass. That is how
  `goToPreaching(origin)` can rebuild the list and push the topic screen on top.
- Pop with an empty stack goes Home (`:109-113`). The Home key/gesture goes Home from any non-Home
  activity unless `handleHomeGesture()` claims it (`:75-81`).

`startActivityForResult` stores the handler and calls `pushActivity` (`src/activities/Activity.cpp:17-20`);
`finish()` is `popActivity()` (`:24`). `ActivityResultHandler` is a `std::function`
(`src/activities/ActivityResult.h` end) — existing infrastructure every result-returning screen uses.

### The reader's load and the parked Bible — **onExit runs before the reader loads**

1. `goToReader` (`ActivityManager.cpp:208-229`) constructs the reader immediately via
   `ReaderActivity::create` (`ReaderActivity.cpp:25-37`). The constructor only stores the path and
   the intent (`EpubReaderActivity.h:223-227`); it touches no `Epub`.
2. `replaceActivity` defers. On the next `loop()` pass the Replace branch runs the topic screen's
   `onExit` + destructor, then `PreachingActivity`'s `onExit` + destructor (it is on the stack).
3. Only then `ReaderActivity::onEnter` (`ReaderActivity.cpp:43-67`) calls `loadBook()`, which calls
   `ParkedBook::take(bookPath, SETTINGS.embeddedStyle == 0)` (`EpubReaderActivity.cpp:218-219`).

So if `PreachingActivity::onExit` parks the `Epub`, the reader takes it. **Confirmed from the code;
no reopen.** The same order holds in reverse: the reader parks in its **destructor**
(`EpubReaderActivity.cpp:165-188`), which `exitActivity` runs before `goToPreaching`'s new activity
enters, so Predicación's `onEnter` can take the Bible the reader just left.

### `ParkedBook` (`src/activities/reader/ParkedBook.{h,cpp}`, `ParkedBookOf.h`)

- One global slot, loop task only (`ParkedBook.h:9-10`). `Held { shared_ptr<Epub> book; shared_ptr<BibleNavCache> nav; }`
  (`ParkedBookOf.h:18-21`).
- `take(path, skipLoadingCss)` always empties the slot; it hands the book back only if the path and
  file size match and `book->reopenCache(skipLoadingCss)` succeeds (`ParkedBookOf.h:41-65`).
- `park(path, held)` sizes the EPUB and parks nothing (and empties the slot) when it cannot
  (`ParkedBook.cpp:43-51`).
- A `null` `nav` is fine: the reader builds its Go-to cache lazily when absent
  (`EpubReaderActivity.cpp:1012-1014`). So Predicación can park `{epub, nav}` it took, or `{epub, nullptr}`
  for a book it opened itself.
- Opening fresh, the reader's pattern is `makeUniqueNoThrow<Epub>(path, sdpaths::CROSSPOINT_DIR)` then
  `load(true, skipLoadingCss)`, with `GUI.drawPopup(STR_INDEXING)` and a `FrameBufferLoan` when
  `book.bin` is absent (`EpubReaderActivity.cpp:190-215`). `ParkedBook` holds a `shared_ptr`, so the
  `unique_ptr` converts.

### Finding the Bible

`BibleFinder::find(std::string_view exclude = {})` (`src/activities/launcher/BibleFinder.h:22`) returns
`optional<Found{path, by}>`. Its recents guess needs recents **already loaded**
(`BibleFinder.h:18-21`); Home calls `RECENT_BOOKS.loadFromFile()` first
(`LauncherActivity.cpp:106,115`).

### Epub APIs the screens use (`lib/Epub/Epub.h`)

- `resolveFilenamesToSpineIndices(const std::string* filenames, int* out, int count)` (`:93`): one
  forward pass, `-1` where absent (`Epub.cpp:976-990`). It takes `std::string`s, so the 21
  `const char*` basenames need a `std::string[21]` (heap strings, ~21 × 17 B; SSO on the host, and
  libstdc++'s 15-byte SSO means each 16-char basename allocates once on the device).
- `getTocIndexForSpineIndex(int)` (`:81`) is `getSpineItem(spineIndex).tocIndex` (`Epub.cpp:899`);
  `getTocItem(int)` (`:77`) returns `TocEntry { std::string title, href, anchor; uint8_t level; int16_t spineIndex; }`
  (`lib/Epub/Epub/BookMetadataCache.h:32-38`). Each call reads the metadata cache on SD.
- `resolveHrefToSpineIndex(const std::string&)` (`:88`, `Epub.cpp:949-974`): strips the fragment,
  decodes, then walks the **whole** spine on a miss, one `getSpineItem` (SD read) and heap string per
  item. Acceptable for one tap; not something to call per card at layout time.

### `SpineHtmlStream::stream` (`src/activities/reader/SpineHtmlStream.{h,cpp}`)

`stream(shared_ptr<Epub>, spineIndex, GfxRenderer&, ChunkSink, void* ctx, WhenMissing = Inflate)`.
Goes through `Section`'s HTML cache; when missing and `Inflate`, it **takes the `RenderLock`** and
loans the framebuffer while it inflates (`SpineHtmlStream.cpp:28-46`). So it must run on the loop task
and never while the caller holds the render lock (`SpineHtmlStream.h:31-33`). Question files are
1.7–4.7 KB (spec), far under the 32 KB popup threshold (`SpineHtmlStream.h:25`), so no popup shows.

### Reader intent consumption (T7's side; T3 only shapes the struct)

`EpubReaderActivity::onBookLoaded` copies then clears `entryIntent` and switches over
`ReaderEntryIntent::route(intent.kind, isBible())` (`EpubReaderActivity.cpp:1101-1133`). `Locate` uses
`STUDY.locatePlace(unit, spineHint)` and shows `STR_LINK_TARGET_NOT_FOUND` on a miss. Other `Kind`
users: `LauncherActivity.cpp:405,418`, `HomeTargets.h:39` (compare/assign only, no switch), and
`EpubReaderActivity.cpp:435` (`Kind::Tags`). The only `switch` over `Kind` is `route()` itself; the only
`switch` over `Route` is `onBookLoaded`. `navigateToHref(const std::string&, bool savePosition = false)`
exists (`EpubReaderActivity.h:204`, `.cpp:2125`).

`ReaderEntryIntent.h` includes only `<StudyStore/Unit.h>`, `<cstdint>` and `"Place.h"`.
`study::TagId` lives in `TagPalette.h:23`, which includes `<ArduinoJson.h>` (`:3`). The host suite can
compile ArduinoJson (`test/CMakeLists.txt:30` fetches it), but `PreachingOrigin` can avoid the include
by holding the raw `uint16_t`.

### List screens

`UiTabListActivity` (`src/activities/UiTabListActivity.h`) — ring 0 is the tab bar, 1..N the rows;
`tabNavs` hold per-tab selection; subclasses implement `tabCount`, `activeTab`, `tabLabel`,
`onTabAction`, `stepTab`, `handleButtons` (pure), plus `UiListActivity`'s `listCount`, `buildScreen`,
`activateIndex` (`UiListActivity.h:35-42`). Two subclasses exist: `SettingsActivity` and
`TextSettingsActivity`. Back differs between them: Settings' Back from a row returns to the tab bar,
from the tab bar goes Home (`SettingsActivity.cpp:222-229`); Text Settings' Back finishes
(`TextSettingsActivity.cpp:179-180`). The device's Back is the left-edge swipe implemented in
`MappedInputManager::wasBackGesture` (`src/MappedInputManager.cpp:222-223`), which `wasPressed`/`wasReleased(Back)` also answer to (`:257,265`).

## 5. Nearest existing examples

| Need | Model | Why |
|---|---|---|
| Two-tab list screen | `src/activities/settings/SettingsActivity.{h,cpp}` | the richer `UiTabListActivity` subclass: `stepTab`, `handleButtons`, `buildScreen` with `syncTabListViewport` |
| Custom-drawn screen with tap regions | `src/activities/reader/EpubReaderMenuActivity.cpp:396,480-487` | a `UiAppHost` screen registering its own rects with `screen.frame().hit(rect, action, value, fui::InputTouch)` |
| Pure, host-tested measured word wrap | `src/activities/boot_sleep/StudySleepFit.h` + `test/study_sleep_pick/StudySleepFitTest.cpp` | Arduino-free, widths through a `MeasureFn` function pointer, greedy wrap on U+0020 only (as `GfxRenderer::wrappedText`), never cuts a word; flags an over-wide word instead |
| Opening the Bible at a tagged passage | `LauncherActivity::openReader` (`LauncherActivity.cpp:404-420`) | `Kind::OpenAt` with `unit` and `spineHint` set from the passage, then `activityManager.goToReader(biblePath, …, intent)` |
| Opening a fresh `Epub` | `EpubReaderActivity::loadFreshEpub` (`EpubReaderActivity.cpp:190-215`) | popup + framebuffer loan only when uncached |
| A `goTo…` wrapper | `ActivityManager::goToReader` (`ActivityManager.cpp:208-229`) | `makeUniqueNoThrow`, `LOG_ERR` on OOM, `replaceActivity` |
| Host test of an intent | `test/ui_layout/ReaderEntryIntentTest.cpp` (CMake target `test/ui_layout/CMakeLists.txt:85-99`, include dirs `src` and `lib/StudyStore`) | extend in place |

Home's verse card face is Noto Serif 14/12 italic, then UI 10 (`src/activities/launcher/HomeVerse.h:24-26`).
`NOTOSERIF_12_FONT_ID` and `NOTOSERIF_14_FONT_ID` exist (`src/fontIds.h:4-5`).

## 6. Findings that shape the design

1. **onExit-before-load is confirmed** (§4). Predicación's `onExit` parks; the reader's `loadBook`
   takes. No second `Epub`, no reload.
2. **A new `Route` value needs a reader case.** If `route(Kind::OpenHref, true)` returns a new
   `Route::OpenHref`, the switch at `EpubReaderActivity.cpp:1104` has no `default`, and `-Wall`
   (`-Wswitch`) warns about the unhandled enumerator — a new warning the brief forbids. T3 adds the
   minimal case (log and open normally, as `Route::None` does) and T7 replaces it. The brief allows
   that widening.
3. **`stream` takes the render lock itself**, so a question is read on the loop task (in the tap
   handler or `onEnter`), never from `render()`.
4. **The list needs to know which question or tag the topic screen ended on**, because "Para la
   revisita" swaps the question in place. `ResultVariant` (`ActivityResult.h`) has no preaching type;
   adding one edits `ActivityResult.h`, which is outside T3's declared files
   (`src/activities/preaching/`, `ActivityManager.`, `ReaderEntryIntent.h`, translations). An existing
   alternative is a `PageResult { uint32_t page; }` (or similar) carrying an encoded selection, or the
   topic screen reporting through the result's `MenuResult::action`. The spec phase picks one; T4's show
   mode also returns a card index through the same mechanism, so it is a contract.
5. **`PreachingOrigin` can stay Arduino-free** with a `uint16_t` tag id and a kind byte, keeping
   `ReaderEntryIntent` trivially copyable (it is copied by value at `EpubReaderActivity.cpp:1102`).
   `char href[64]` makes it ~70 B larger; `goToReader` takes it by `const&` and the reader stores one
   copy (`EpubReaderActivity.h:60`).
6. **Back on the root list.** The spec says the left-edge swipe goes Home from the list. Since
   Predicación is the root after `replaceActivity`, `finish()` with an empty stack already goes Home
   (`ActivityManager.cpp:109-113`). When rebuilt with the topic pushed, Back from the topic pops to the
   list. Settings' two-stage Back (row → tab bar → Home) is the alternative pattern; the spec's wording
   matches Text Settings' one-stage `finish()`.
7. **The counts pass and `PreachingTopicsFile::load()` are loop-task work** (`PreachingTopicsFile.h:16-18`;
   both read SD under `storageMutex` through `HalStorage`). Running them in `onTabAction`/`stepTab`
   (loop task) the first time Mis temas opens fits; `buildScreen` runs on the render task
   (`UiListActivity.h:36-38`) and must only read the held result.
8. **A word wider than the card.** `StudySleepFit`'s wrap flags it and refuses. Cards must never cut,
   so the card layout needs a policy for one over-wide word (e.g. a line of its own, overflowing, or a
   hard break). Real NWT quotes have no such word at 12 pt on a 480-px-wide portrait screen, but
   `Text` blocks with URLs or long compounds could.
9. **Host-test placement.** Pure paging over blocks can live in a header under
   `src/activities/preaching/` and be tested in `test/ui_layout` (it already includes `src`), adding an
   executable to `test/ui_layout/CMakeLists.txt` — not the shared `test/CMakeLists.txt`.
10. **Font cache.** The "clear the font cache" rule applies to overlays entered from the reader menu
    (`BibleNavigationActivity.cpp:81-82`). Predicación replaces the reader (D8) rather than stacking on
    it, so it does not apply here.

## 7. Tier

`heavy` stands. The task touches the `ReaderEntryIntent` contract (T7 builds on it), `ActivityManager`
and the translations every sibling depends on; finding 2 adds a one-case touch in
`EpubReaderActivity.cpp`, already within the brief's allowance. No migration, no on-disk format, no
surface beyond `ui`. Not raised further.
