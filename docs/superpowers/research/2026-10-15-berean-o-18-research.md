# Research: berean-o-18 — Elegir temas, the Predicación topic picker (t5)

Branch `feature/preaching-topic-picker`, cut from `batch/20261015-predicaci-n-193x`
(`git merge-base --is-ancestor origin/batch/20261015-predicaci-n-193x HEAD` → true; tip
`61bb11f9`). Spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §3, §7, D4, D5.

## Tools and packages installed

| | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on PATH) |
| Platform | pioarduino espressif32 55.03.37 | `platformio.ini:15` |
| ArduinoJson | 7.4.2 | `platformio.ini:155` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| Firmware version | 1.30.0 | `platformio.ini:11` |

Host tests build as in CI: `cmake -S test -B build/test -G Ninja`, `cmake --build build/test`,
`ctest --test-dir build/test` (`.github/workflows/ci.yml:145-151`).

## Who owns the behaviour today

**The seam.** `PreachingActivity::openTopicPicker()` only logs:
`src/activities/preaching/PreachingActivity.cpp:208` (`LOG_INF(MODULE, "Elegir temas is not built yet")`).
It is reached from `activateIndex` when the Mis temas row index equals `topics.size()`, which is
the "+ Elegir temas" row (`PreachingActivity.cpp:200-205`; the row is built at `:102-105` from
`chooseTopicsLabel`, set at `:37`).

**The rebuild after a save already exists.** `PreachingActivity::onTopicsChanged()`
(`PreachingActivity.cpp:210-222`, declared `.h:39-40`) clears the rows, calls
`session.reloadTopics()`, calls `session.ensureCounts()` if Mis temas is showing, and rebuilds,
all under one `RenderLock`. Nothing calls it yet. It always recounts: it does not compare the
old and new picks, which the brief asks for ("Re-run T2's counts pass only when the selection
changed").

**The session.** `PreachingSession` (`src/activities/preaching/PreachingSession.h:22-74`) owns
a `PreachingTopicsFile topicsFile` (`:63`). That file is **private**: the session exposes only
`topics()` (active picks in `activeIdsByName` order, `:46`), `topicName(id)` (through
`topicsFile.palette()`, `:47`), `topicRow` (`:49`), and `reloadTopics()` (`:44`).
- `ensureTopics()` loads the file once, then keeps the picks that appear in
  `palette().activeIdsByName()`, in that order (`PreachingSession.cpp:116-128`).
- `reloadTopics()` clears `topicsLoaded`, `countsLoaded` and `counts`, then calls
  `ensureTopics()` (`:130-135`). That is a fresh `topicsFile.load()`, which re-reads
  `tags.json` and `preaching.json`.
- `ensureCounts()` runs `PreachingPassages::countByTag` over `bible.json` once
  (`:142-152`).

`PreachingSession.*` is outside this task's files. The brief owns only `PreachingTopicPicker*`
and `PreachingActivity.*`. So the picker cannot save through the session's file object without a
new session method.

**The store (T2).** `PreachingTopicsFile` (`src/study/PreachingTopicsFile.h:21-49`,
`.cpp:43-98`):
- `load()` reads `tags.json` into its own `TagPalette palette_` and reads `preaching.json`.
  `saveRefused_` is true if either read failed (`.cpp:58-59`). A fresh object refuses to save
  until `load()` has run (`.h:48`, `saveRefused_ = true`).
- `save(picked)` returns `Refused` when latched (`.cpp:64-67`).
- Otherwise `save(picked)` keeps only ids active in its own `palette_` (`:69-73`). It refuses
  `TooLarge` over 200 tags or over the 2,048 B budget (`:75-93`), and writes through
  `writeDocToFileAtomic` (`:94`). Only on `Ok` does it replace `tags_` (`:96`); every other
  result leaves `tags()` unchanged.
- `SaveResult` is `{ Ok, TooLarge, WriteFailed, Refused }` (`.h:28`).

17 host tests cover it in `test/storage_io/PreachingTopicsFileTest.cpp:43-213`, among them the
latch (`:68,79,89,134,147`), pruning (`:169`), the budget (`:182`), a failed write (`:193`) and
a missing palette (`:204`).

**The string** exists, from T3:
- `STR_PREACHING_SAVE_FAILED`: "Couldn't save your topics" / "No se pudieron guardar tus
  temas" (`lib/I18n/translations/english.yaml:498`, `spanish.yaml:445`).
- `STR_PREACHING_CHOOSE_TOPICS` is at `english.yaml:488` and `spanish.yaml:435`.

## The nearest example: `TagPickerActivity` on `TagChipGridActivity`

`TagPickerActivity` (`src/activities/reader/TagPickerActivity.{h,cpp}`) is the model the brief
names.
- **Seeding.** It seeds `selectedIds_` from `TagChips::selectedInOrder(STUDY.palette().activeIds(), initial)`
  (`.cpp:22-25`).
- **Chips.** It builds a `NewTag` chip, then one chip per `STUDY.palette().activeIdsByName()`
  id (`.cpp:58-74`; `ChipEntry::label` is `char[40]`, `TagChipView.h:21`).
- **Toggling.** It toggles under `RenderLock` and calls `app.clearTapFlash()` first
  (`.cpp:95-120`).
- **Retire.** It drops a retired id from the selection in `onTagRetired` (`.cpp:170-173`).
- **Home.** Home calls `commitAndFinish()`, which sets a `TagSelectionResult` and calls
  `finish()` (`.cpp:175, 183-189`).
- **Done.** It draws a header "Done" pill (`.cpp:52-56, 81-93`).
- **Chip state.** `chipState` uses `TagChips::pickerState(kind, selected, count, cap)`
  (`TagChipRow.h:95-99`): `Active` when selected, `Inactive` or `Disabled` against a cap.
- **The tick.** "Ticked" on this panel is the `Active` look, an inverted black pill with bold
  text (`TagChipView.cpp:94-97, 118-120`). There is no tick glyph.

`TagFilterActivity` is the other subclass (`TagFilterActivity.h:18-38`). Its `onHome()` cancels,
the same as Back (`TagFilterActivity.cpp:52-55`).

### What the base fixes, which shapes this task

`TagChipGridActivity.h:31-109`:
- **Back always cancels.** `onBackButton()` is **`final`** (`:65`). It sets
  `isCancelled = true` with a monostate payload and calls `finish()` (`.cpp:136-141`).
  `handleButtons()` (`:70`, `.cpp:221`) and `handleCustomInput()` (`:68`) are `final` too.
- **The left-edge swipe is Back.**
  `MappedInputManager::wasReleased(Button::Back)` is true for `wasBackGesture()`
  (`src/MappedInputManager.cpp:265-266`), which is the left-edge swipe (`:222-227`, pressed at `:257`).
  `UiListActivity::handleButtons` sends it to `onBackButton()` (`UiListActivity.cpp:48-52`).

  **So in any `TagChipGridActivity` the swipe cancels, and a subclass cannot override that
  through the hooks.** The brief wants the swipe to save. Two seams are left:
  - `loop()` is still overridable. `UiListActivity::loop` is `override` and not `final`
    (`UiListActivity.h:22`). The base runs `handleCustomInput()` first, which gives the retire
    popup Back (`TagChipGridActivity.cpp:204-212`, `UiListActivity.cpp:84-86`). An override
    would have to leave Back to the popup while it is open (`retireConfirmationOpen()`, `.h:81`).
  - The result can be ignored. The picker could publish its selection to state owned by
    `PreachingActivity`, which then saves in its result handler whatever the result says. A
    precedent for a pushed screen holding a reference to state owned by `PreachingActivity` is
    `PreachingTopicActivity` holding `const PreachingSession&`
    (`PreachingTopicActivity.h:24,65`).
- **Home goes to `onHome()`.** `handleHomeGesture()` is `final` and always returns true; it calls
  `onHome()` unless the retire popup is open (`.cpp:143-148`). `ActivityManager::loop` therefore
  never runs its own `goHome()` for a chip grid (`ActivityManager.cpp:76-81`).
  - The brief's "Home goes Home, as the other chip grids do" does not match either existing
    grid. TagPicker's Home **commits and returns to its caller**; TagFilter's Home **cancels to
    its caller**.
  - Neither reaches the Home screen. Doing that literally would mean `onHome()` calling
    `activityManager.goHome()` (`ActivityManager.h:93`, a `replaceActivity`, `.cpp:252-254`).
  - Left for the spec.
- **Retire.** The long-press path is in the base and always goes through `STUDY`:
  - `onRowLongPress` (`.cpp:163-172`) → `showRetireConfirmation`, which refuses only on
    `STUDY.saveDisabled()` (`:174-182`).
  - Then `retireTag` → `STUDY.retireTag(id)` (`:195-202`).
- **Threads.** Chips are rebuilt on the loop task. Render-read state is written only under
  `RenderLock` (`.h:25-28`).

### How a pushed screen exits and hands back its result

`ActivityManager::loop`, in its Pop branch (`ActivityManager.cpp:94-136`):
1. It moves `currentActivity->result` out first.
2. Then it calls `exitActivity` under `RenderLock`, which runs the child's `onExit()` and
   destroys the child.
3. Then it runs the parent's `resultHandler` with the lock released.

A result set in `onExit()` is therefore too late. A save in the parent's handler runs on the loop
task with no lock held, which is what `PreachingTopicsFile` requires ("Loop task only",
`PreachingTopicsFile.h:18-20`).

`PostedMessage::post(tr(...))` queues a toast over the next finished render
(`src/activities/PostedMessage.h:5-24`). It is what `PreachingActivity` already uses for failures
(`PreachingActivity.cpp:224-227`), and `ReaderUtils::showMessage` is a thin wrapper over it
(`ReaderUtils.h:233`).

## Finding: retiring a tag here can wipe `tags.json`

**Where `STUDY`'s palette gets loaded.** `StudyStore`'s palette is filled only by
`openPublication` (`src/study/StudyStore.cpp:64-98`, load at `:72`). The only caller of that is
the reader (`src/activities/reader/EpubReaderActivity.cpp:280`):
- `grep -rn "TagPaletteFile::load\|openPublication("` over `src`/`lib` returns only `StudyStore`,
  `PreachingTopicsFile` and `MigrationRunner`, and each fills its own object.
- `StudyStore::mutablePalette()` (`StudyStore.h:164`) has no caller.
- `closePublication()` does not clear `palette_` (`StudyStore.cpp:100-109`).

**Predicación does not open `STUDY`.** `PreachingSession` reads the palette through its own
`PreachingTopicsFile` (`PreachingSession.h:47,63`).

**Home is reachable with no book opened.** Boot routes `Home` and `SilentHome` go straight to
`goHome()` (`src/main.cpp:588-593`). Home → Predicación → Elegir temas can therefore run with
`STUDY.palette()` default-empty and `STUDY.saveDisabled()` false.

**What each choice of chip palette leads to:**
- **Chips built from `STUDY.palette()`, as TagPicker does.** The grid is **empty** in that case:
  the owner sees no tags to pick until some book has been opened this boot.
- **Chips built from a palette read from `tags.json`, such as a `PreachingTopicsFile`'s.** A
  long-press is then possible, and the shared retire calls `STUDY.retireTag(id)`
  (`StudyStore.cpp:258-266`), which does three things:
  1. `palette_.retire(id)` is a no-op for an unknown id (`TagPalette.h:47-49`).
  2. `TagPaletteFile::save(palette_)` writes the **empty** in-memory palette over `tags.json`
     atomically. It has no guard beyond the byte budget (`src/study/TagPaletteFile.cpp:25-35`).
  3. `save()` then returns false on an empty `pubKey_` (`StudyStore.cpp:156`), so the user sees
     `STR_TAG_SAVE_FAILED`.

  The global tag palette is lost. Every passage keeps tag ids that no longer resolve.

**When `STUDY` is open, the retire is consistent with the reader.** Once any publication has
been opened this boot, `STUDY` holds the real palette. `closePublication` is never called on
reader exit; `grep -rn closePublication src` finds only `StudyStore` itself and a comment at
`ReaderBookmarks.h:51`. A retire from the picker then behaves as it does in the reader: it updates
`tags.json` and strips the tag from whichever publication `STUDY` has open. That is the Bible's
`bible.json` only if the Bible was the last book read.

**The other chip grids are safe.** They are reachable only from the reader, where `STUDY` is
open. So this hazard is new with this task, not a latent bug in the existing grids.

### Ways out, for the spec and the decision

| | How | In T5's files? | Cost |
|---|---|---|---|
| A | Before pushing the picker, open `STUDY` on the session's Bible with the existing API when `!STUDY.isOpen()` (`StudyStore.h:37,94`; `session.book()` is the parked `shared_ptr<Epub>`, `PreachingSession.h:36`) | Yes (`PreachingActivity`) | `openPublication` resolves the pubkey, loads `tags.json` and `bible.json`, and begins the unit index (`StudyStore.cpp:64-98`; `UnitIndexCache::begin`, `src/study/UnitIndexCache.cpp:50`). A reader opened later on the same `Epub` can then reuse it (`canResumePublication`, `StudyStore.cpp:111-119`). The cost is one load, timed on the device. |
| B | Guard the shared retire: refuse it when `STUDY` has not loaded a palette | No: `TagChipGridActivity` and/or `StudyStore` (data surface) | Touches every chip grid, and another surface |
| C | Build chips from `STUDY.palette()` and accept an empty grid until a book has been opened | Yes | The picker is unusable straight after boot |

## Smaller facts the spec needs

- **Popup text.** A save failure has one string, `STR_PREACHING_SAVE_FAILED`. `TooLarge`,
  `WriteFailed` and `Refused` all map to it.
- **Seeding.** The picker can seed from `session.topics()`, the active picks already filtered.
  An unpicked palette is empty, and that is valid: "Nothing picked → … the Elegir row" (spec §3).
- **What "changed" means.** The brief recounts only when the selection changed. Comparing
  `session.topics()` before and after `reloadTopics()` (both `std::vector<study::TagId>` in
  `activeIdsByName` order) is enough. Retiring a picked tag changes `topics()` even when nothing
  was toggled, so it counts as a change.
- **Where the latch lives.** A picker that owns its own `PreachingTopicsFile` loads, and latches,
  once per opening, not once per Predicación session. The spec's latch is "for the session"
  (§7). A second read refuses again unless the first refusal was transient, because nothing writes
  the file while it is latched.
- **Host tests.** The brief asks for tests only of logic extracted here. The existing pure helpers
  are `TagChips::selectedInOrder` and `pickerState` (`TagChipRow.h`, tested in
  `test/tag_rows/TagChipRowTest.cpp`). A "did the picks change" helper, if extracted, would sit
  beside them.
- **Shared files.** `test/CMakeLists.txt`, the translations and `src/main.cpp` are report-only
  for ui-dev (`.claude/agents/ui-dev.md`, "Shared files"). This task needs no new string.

## Open questions for the spec

1. **The retire hazard** (above). It is a data-integrity choice, so it is raised as a decision.
2. **Home.** It could commit and return (TagPicker), cancel and return (TagFilter), or
   `goHome()`, either saving first (and toasting a failure over Home) or not.
3. **The swipe-saves mechanism** against the `final` `onBackButton`. It could be a `loop()`
   override in the picker, or a save in `PreachingActivity`'s result handler driven by state the
   picker writes.
4. **Where the saving `PreachingTopicsFile` object lives.** It could be the picker's own (load on
   enter) or the session's, which needs a `PreachingSession` method outside this task's files.

Tier stays `standard` if decision 1 resolves to A: everything lands in `PreachingTopicPicker*`
and `PreachingActivity.*`. B would reach `TagChipGridActivity` and `StudyStore`, and would raise
the tier.
