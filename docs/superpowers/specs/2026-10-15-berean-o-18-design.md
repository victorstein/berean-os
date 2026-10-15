# Design: Elegir temas, the Predicación topic picker (berean-o-18, t5)

Surface: `ui`. Tier: `heavy` (raised by the orchestrator with decision d1). Files:
`src/activities/preaching/PreachingTopicPickerActivity.{h,cpp}` (new) and
`src/activities/preaching/PreachingActivity.{h,cpp}`.

Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §3 (Mis temas), §7
(Elegir temas), D4 and D5. Research: `docs/superpowers/research/2026-10-15-berean-o-18-research.md`.
Modelled on **`TagPickerActivity`** (`src/activities/reader/TagPickerActivity.{h,cpp}`).

## Problem

Mis temas lists the tags the owner picked as preaching topics (`preaching.json`, T2). Nothing on
the device can pick them yet.
- The "+ Elegir temas" row exists, but its handler only logs: `PreachingActivity::openTopicPicker`,
  `src/activities/preaching/PreachingActivity.cpp:208`.
- So Mis temas is always "Nothing picked" unless the file is written by hand.

## Goal

1. **Open the picker.** "+ Elegir temas" opens a full-screen, multi-select grid of every active
   tag, with the current picks shown as Active chips. There is no tag creation.
2. **Swipe saves.** The left-edge swipe saves the picks through `PreachingTopicsFile` and returns
   to Predicación.
   - A failed save, including one refused because a load failed, shows
     `STR_PREACHING_SAVE_FAILED`. The file keeps its previous picks.
3. **Mis temas refreshes.** Back in Predicación, Mis temas shows the picks in `activeIdsByName`
   order with fresh counts. The `bible.json` count pass runs again only when the picks changed.
4. **Retire is safe.** A long-press retires a tag, as in every chip grid, without ever writing an
   empty palette over `tags.json` (decision d1).

## Non-goals

- No change to `TagChipGridActivity`, `StudyStore`, `PreachingSession`, `PreachingTopicsFile`, the
  strings, or any file format. Those belong to other tasks, or are shared.
- No tag creation, renaming or ordering in the picker.
- No cap on how many tags are picked, beyond what the store enforces (A6).
- No new host test suite: no logic is extracted (§Testing).

## Facts this rests on (verified)

**The chip grid fixes Back, Home and retire.** In `TagChipGridActivity`
(`src/activities/reader/TagChipGridActivity.h:31-109`):
- **Back cancels.** `onBackButton()` is `final` (`:65`). It sets `isCancelled = true` with a
  monostate payload, then `finish()` (`TagChipGridActivity.cpp:136-141`). `handleButtons` (`.h:70`)
  and `handleCustomInput` (`.h:68`) are `final` too.
- **The left-edge swipe is Back.** `MappedInputManager::wasReleased(Button::Back)` is true for
  `wasBackGesture()` (`src/MappedInputManager.cpp:264-265`), which is the left-edge swipe
  (`:222-227`).
- **The seam left open.** `loop()` is not `final` (`UiListActivity.h:22`). Its first step is
  `handleCustomInput()`, which gives the retire popup first claim on input
  (`UiListActivity.cpp:84-86`, `TagChipGridActivity.cpp:204-212`).
- **Home.** `handleHomeGesture()` is `final`, always consumes Home, and calls `onHome()` unless the
  retire popup is open (`TagChipGridActivity.cpp:143-148`).
- **Retire.** A long-press shows a confirmation, refused when `STUDY.saveDisabled()`
  (`TagChipGridActivity.cpp:174-182`). It then calls `STUDY.retireTag(id)` and `onTagRetired(id)`
  when the tag went inactive (`:195-202`).

**How results come back.** On a pop, the manager moves the child's `result` out first, then
destroys the child under `RenderLock` (its `onExit` runs there), then runs the parent's
`resultHandler` with the lock released (`src/activities/ActivityManager.cpp:94-136`). After the
handler it requests a render (`:131-133`).

**The store.** `PreachingTopicsFile` (`src/study/PreachingTopicsFile.h:21-49`):
- **`load()`** reads `tags.json` into its own palette and reads `preaching.json`. It latches
  saving off when either read failed (`.cpp:43-61`). A new object refuses to save until it has
  loaded (`.h:48`).
- **`save(picked)`** keeps the ids active in that palette and enforces the 200-tag cap and the
  2,048 B budget (`.cpp:63-97`). It writes atomically and changes `tags()` only on `Ok`.
- **Results.** `SaveResult` is `{Ok, TooLarge, WriteFailed, Refused}` (`.h:28`).
- **Thread.** Loop task only (`.h:18-20`).

**The session.** `PreachingSession` (`src/activities/preaching/PreachingSession.h`):
- **`topics()`** holds the active picks in `activeIdsByName` order (`:46`, built at
  `PreachingSession.cpp:116-128`).
- **`reloadTopics()`** re-loads the file and its palette and drops the counts (`.cpp:130-135`).
- **`book()`** is the Bible's `shared_ptr<Epub>` (`.h:36`).
- **`hasBible()`** says whether one is loaded (`.h:34`).
- **The file object is private** (`.h:63`).

**`PreachingActivity::onTopicsChanged`** already exists (`PreachingActivity.cpp:210-222`). Under
one `RenderLock` it clears the rows, calls `reloadTopics()`, recounts if Mis temas is the
current tab, and rebuilds. Nothing calls it yet.

**`STUDY` is loaded only by the reader.** The only `openPublication` call is the reader's, which
first checks whether it can resume (`src/activities/reader/EpubReaderActivity.cpp:274-281`). The
palette is filled only there (`src/study/StudyStore.cpp:72`). On a boot straight to Home
(`src/main.cpp:588-593`) it is empty, and `retireTag` would save that empty palette
(`StudyStore.cpp:258-266`, `TagPaletteFile.cpp:25-35`).

**What `openPublication` costs here** (`StudyStore.cpp:64-98`):
- It resolves the pubkey, loads `tags.json`, loads this publication's passages (`bible.json` for
  the Bible), and constructs and `begin()`s the unit index.
- `begin()` reads or writes only the index header and a zeroed table. It scans no document
  (`src/study/UnitIndexCache.cpp:50-110`), so `renderer` is not drawn with during the open.
- A failed tags or passages read sets `saveDisabled_` (`:73-83`).

**Toasts.** `PostedMessage::post` shows a toast over the next screen that finishes a render
(`src/activities/PostedMessage.h:5-24`). Both screens this picker returns to draw it: the list
base at `UiListActivity.cpp:170` (Predicación is a `UiTabListActivity`) and Home at
`LauncherActivity.cpp:376`.

## Decision d1 (settled by the orchestrator)

Before pushing the picker, `PreachingActivity` opens `STUDY` on the session's Bible, the way the
reader does: `if (!STUDY.canResumePublication(epub)) STUDY.openPublication(epub, renderer)`.
- **The chips come from `STUDY.palette()`.**
- **Mis temas never shows a retired tag:** it refreshes from `tags.json` on return.
- **The open's duration is logged.**

`canResumePublication`, not `isOpen()`, so that a `STUDY` open for another publication is
reopened on the Bible. A retire then strips the tag from `bible.json` (D5).

## Design

### Assumptions (each is a choice the review may attack)

- **A1 — The swipe is intercepted in `loop()`.**
  - `PreachingTopicPickerActivity::loop()` checks `mappedInput.wasReleased(Back)` before
    delegating to `TagChipGridActivity::loop()`. It does so only while
    `!retireConfirmationOpen()`, so the popup still gets Back to dismiss itself.
  - On Back it saves and finishes (§Control flow). Otherwise it calls the base `loop()` unchanged.
  - Precedent for a list activity wrapping the base loop: `MeetingsActivity::loop`
    (`src/activities/network/MeetingsActivity.cpp:284-295`) and `BibleSearchActivity`
    (`src/activities/reader/BibleSearchActivity.cpp:158`).
  - **Rejected:** saving in `PreachingActivity`'s result handler from state the picker writes into
    its parent. No screen in this repo writes into its parent; `PreachingTopicActivity` only reads
    a `const PreachingSession&`. Also rejected: changing the base's `final`, which is out of scope.
  - **Consequence:** the base's cancelling `onBackButton()` becomes unreachable from the picker,
    because every Back release while the popup is closed is taken first.
- **A2 — The picker owns the `PreachingTopicsFile` it saves through, loaded in `onEnter()`.**
  - The session's object is private (`PreachingSession.h:63`), and `PreachingSession.*` is outside
    this task's files.
  - **The latch is per opening of the picker, not per Predicación session** (parent spec §7 says
    "for the session"). It is still safe. While a file is latched nothing writes it, so a second
    read refuses again. If it reads cleanly the second time, the first failure was transient, and
    saving over cleanly read data is what the latch exists to allow.
- **A3 — The picker seeds its selection from its own loaded `tags()`**, filtered to
  `STUDY.palette().activeIds()` with `TagChips::selectedInOrder` (`TagChipRow.h:166-174`). This is
  `TagPickerActivity`'s seeding (`TagPickerActivity.cpp:22-25`), applied to what is on disk now.
  - If the load latched, `tags()` is empty (`PreachingTopicsFile.cpp:58`), so the picker opens
    with nothing picked. The swipe's save is then refused with the popup.
- **A4 — Chips are one `Kind::Tag` chip per `STUDY.palette().activeIdsByName()`**, with no
  `NewTag` chip. This is `TagPickerActivity::buildChips` (`TagPickerActivity.cpp:58-74`) without
  its first chip.
  - **State:** `Active` when picked, otherwise `Inactive`.
  - **"Ticked"** is the `Active` look, an inverted pill with bold text
    (`TagChipView.cpp:94-97, 118-120`). No glyph is added.
- **A5 — Home saves exactly as the swipe does, then goes to the Home screen**
  (`activityManager.goHome()`, `ActivityManager.h:93`).
  - The brief says "Home: goes Home"; Predicación's own Home key goes Home (parent spec §3). Saving
    first means Home never silently drops picks the owner made.
  - A failed save posts the same toast, which Home draws (`LauncherActivity.cpp:376`).
  - **Rejected:** TagPicker's commit-and-return (`TagPickerActivity.cpp:175`) and TagFilter's
    cancel-and-return (`TagFilterActivity.cpp:52-55`). Neither "goes Home".
- **A6 — No cap in the picker.**
  - `pickerState`'s `Disabled` exists for a per-passage cap (`TagChipRow.h:93-99`), and its
    message, `STR_TAG_LIMIT_PER_HIGHLIGHT`, is about highlights.
  - The store refuses more than 200 picks with `TooLarge` (`PreachingTopicsFile.cpp:75-79`), which
    shows the save-failed toast. The owner's palette is 48 tags (`TagPalette.h:35`).
- **A7 — The result is `TagSelectionResult` holding the picker's final selection**, in
  `STUDY.palette().activeIdsByName()` order (`ActivityResult.h:88-90`). The picker sets it whether
  or not the save succeeded. The save's outcome is reported by the picker's own toast.
  `isCancelled` is never set by the picker.
- **A8 — Recount only on a change.**
  - In `onTopicPickerResult` (below), `PreachingActivity` compares the returned selection with
    `session.topics()` as sets (`std::is_permutation`). It calls the existing `onTopicsChanged()`
    only when they differ.
  - Unchanged → no reload and no count pass. The manager's post-handler render repaints
    (`ActivityManager.cpp:131-133`).
  - A result that is not a `TagSelectionResult` (unreachable under A1) is treated as changed, so a
    retire is never left showing.
  - Retiring a picked tag removes it from the selection (`onTagRetired`), so it counts as a change
    even when the save failed. The reload then re-reads `tags.json` and drops the retired tag.
  - Retiring an unpicked tag changes nothing Mis temas lists.
- **A9 — Where the ring lands.** After a change the Mis temas rows are rebuilt and the ring is
  moved to the "+ Elegir temas" row, ring position `listCount()`, with `moveRingTo`, as
  `onTopicResult` does (`PreachingActivity.cpp:277-281`). Unchanged → the ring stays where it was,
  on that row.
- **A10 — `STUDY` opens on the "+ Elegir temas" tap**, in `openTopicPicker()`, and only then.
  - Only the picker needs it, so a visit that never opens the picker pays nothing.
  - It runs on the loop task with no `RenderLock` held, as `activateIndex` is called
    (`UiListActivity.h:39-42`).
  - **`resumePublication()` is not called.** Unlike the reader (`EpubReaderActivity.cpp:276`), it
    only resets the marked link source (`StudyStore.cpp:122`), which nothing here reads. The reader
    calls it itself on its own entry.
- **A11 — No Bible, no picker.**
  - If `!session.hasBible()`, `openTopicPicker()` posts `STR_PREACHING_OPEN_FAILED` and does not
    push the picker. Without the Bible's `Epub`, `STUDY` cannot be opened safely (d1).
  - In practice this is rare: Home routes a Predicación with no Bible to the download (parent
    spec §2).
- **A12 — Title.** The header title is `tr(STR_PREACHING_CHOOSE_TOPICS)` through the default
  `drawChrome` (`UiListActivity.h:63-66`). There is no Done pill: the swipe is the exit, and
  `TagPickerActivity`'s Done exists for its touch commit beside a cancelling Back
  (`TagPickerActivity.h:18-19`).

### `PreachingTopicPickerActivity` (new)

`src/activities/preaching/PreachingTopicPickerActivity.{h,cpp}`: a `final` subclass of
`TagChipGridActivity`, constructor `(GfxRenderer&, MappedInputManager&)`.

| Member | Behaviour |
|---|---|
| `onEnter()` | `TagChipGridActivity::onEnter()`; `topicsFile.load()`; seed `selectedIds` under `RenderLock` (A3); `rebuildChips()` |
| `loop()` | A1 |
| `headerTitle()` | A12 |
| `buildChips(out)` | A4; `out.reserve(ids.size())` first |
| `chipState(chip)` | A4; render task, reads `selectedIds` only |
| `activateIndex(i)` | Ignored while `retireConfirmationOpen()` or out of range. Otherwise `app.clearTapFlash()`, toggle under `RenderLock`, `requestUpdate()`, as `TagPickerActivity.cpp:95-120` |
| `onTagRetired(id)` | Drop `id` from `selectedIds` under `RenderLock`, as `TagPickerActivity.cpp:170-173` |
| `onHome()` | `saveAndReturn()` then `activityManager.goHome()` (A5) |

`saveAndReturn()` is private, on the loop task:
1. `picks = TagChips::selectedInOrder(STUDY.palette().activeIdsByName(), selectedIds)`.
2. `topicsFile.save(picks)`. On any result but `Ok`, `LOG_ERR` the result and
   `PostedMessage::post(tr(STR_PREACHING_SAVE_FAILED))`.
3. `app.clearTapFlash()`; `setResult(TagSelectionResult{picks})` (A7); `finish()`.

The swipe calls `saveAndReturn()`. Home calls it and then `goHome()`, whose replace drops the
stack and the result handler with it (`ActivityManager.cpp:144-151`).

**State and threads.**
- **`selectedIds`** (`std::vector<study::TagId>`) is written by the loop task under `RenderLock`
  and read by `chipState` on the render task. These are the base's rules
  (`TagChipGridActivity.h:25-28`), the same as `TagPickerActivity.h:49-50`.
- **`topicsFile`** (`PreachingTopicsFile`) is loop task only.

### `PreachingActivity` changes

- **`openTopicPicker()`** replaces the log at `PreachingActivity.cpp:208`:
  1. `app.clearTapFlash()` (the screen is being left, `UiListActivity.h:39-41`).
  2. No Bible → `reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED))` and return (A11).
  3. `ensureStudyOpen()`, below.
  4. `auto picker = makeUniqueNoThrow<PreachingTopicPickerActivity>(renderer, mappedInput)`. On
     null: `LOG_ERR`, `reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED))`, return. This is the
     pattern of `pushTopic` (`:259-267`).
  5. `startActivityForResult(std::move(picker), [this](const ActivityResult& r) { onTopicPickerResult(r); })`,
     the same lambda form as `:266`.
- **`ensureStudyOpen()`** is new and private:
  - `const auto& epub = session.book();`
  - If `STUDY.canResumePublication(epub)`, return.
  - Otherwise time `STUDY.openPublication(epub, renderer)` with `millis()`, and log
    `LOG_INF(MODULE, "Study opened for Elegir temas in %lu ms")` for the device check (d1.4).
  - If `STUDY.saveDisabled()` is set afterwards, `LOG_ERR`. The base refuses a retire in that
    state (`TagChipGridActivity.cpp:176-181`), so nothing more is done here.
- **`onTopicPickerResult(const ActivityResult&)`** is new and private, implementing A8 and A9:
  - Changed → `onTopicsChanged()`, then `moveRingTo(listCount())`.
- **`onTopicsChanged()`** stays as it is. Its comment, "for after Elegir temas saves"
  (`PreachingActivity.h:39`), still holds.

The header comment at `PreachingActivity.h:37` ("Elegir temas (T5).") becomes a real description
when the seam is filled.

## Data and control flow

```
Mis temas: tap "+ Elegir temas"  (loop task, no lock)
  PreachingActivity::openTopicPicker
    no Bible?  -> toast OPEN_FAILED, stay
    ensureStudyOpen: canResume(Bible)? : openPublication(Bible)   [LOG_INF ms]
    push PreachingTopicPickerActivity
      onEnter: topicsFile.load()  -> tags.json, preaching.json (latch on failure)
               selectedIds = selectedInOrder(STUDY.activeIds, topicsFile.tags())
               chips = STUDY.activeIdsByName, no New tag
      tap chip      -> toggle (RenderLock)
      long-press    -> base retire via STUDY (Bible open, so tags.json + bible.json)
                       -> onTagRetired: drop from selectedIds
      left swipe    -> saveAndReturn: save(picks); != Ok -> toast SAVE_FAILED
                       result = TagSelectionResult{picks}; finish()
      Home          -> saveAndReturn, then goHome()  (handler never runs)
  pop: picker onExit/destroy under RenderLock; handler with lock free
    onTopicPickerResult:
      picks == session.topics() (as sets)?  -> nothing; manager re-renders
      else -> onTopicsChanged(): reloadTopics (re-reads tags.json + preaching.json),
              ensureCounts (bible.json pass), rebuildRows   [all under RenderLock]
              moveRingTo(Elegir row)
```

On disk: `/.berean/preaching.json`, written only by `PreachingTopicsFile::save`, atomically and
under its budget. `tags.json` and `bible.json` are written only through the unchanged retire
path, with `STUDY` holding the real palette and the Bible's passages.

## Error handling

| What fails | What happens | What the user sees |
|---|---|---|
| No Bible loaded | Picker not pushed (A11) | `STR_PREACHING_OPEN_FAILED` |
| `openPublication`: tags or `bible.json` unreadable | `saveDisabled_` latched by `StudyStore` (`StudyStore.cpp:73-83`) | Picker opens; a retire is refused with `STR_HIGHLIGHTS_LOAD_FAILED` (base) |
| `tags.json` unreadable as `STUDY` loads it | Palette empty, so no chips | An empty grid; nothing is written |
| `preaching.json` corrupt or newer, or `tags.json` unreadable, in the picker's load | `topicsFile` latched; selection starts empty (A3) | On leaving: `STR_PREACHING_SAVE_FAILED`; the file is untouched |
| Save `TooLarge` / `WriteFailed` / `Refused` | `tags()` and the file unchanged (`PreachingTopicsFile.cpp:63-97`) | `STR_PREACHING_SAVE_FAILED` over Predicación or Home; Mis temas shows the previous picks (minus any retired) |
| Retire saved the palette but not the passages | Base posts `STR_TAG_SAVE_FAILED` (`TagChipGridActivity.cpp:199-201`) | As in every chip grid |
| OOM creating the picker | `makeUniqueNoThrow` null → `LOG_ERR` | `STR_PREACHING_OPEN_FAILED` |

No abort path is added. Every allocation is either `makeUniqueNoThrow` or a `std::vector` the
existing chip code already makes.

## Memory

- **The picker:** one heap object. It holds the chip list the base keeps (one 40-byte label per
  active tag, `TagChipView.h:21`) and `topicsFile`, whose palette copy is about 48 names of ≤ 24
  bytes plus the entries. That is a few KB of internal heap, all freed in its destructor on pop. It
  is a member, not a stack local; the stack stays under 256 B.
- **Three palettes while it is open:** `STUDY`'s, the session's and the picker's. This is the cost
  of A2. It ends when the picker pops.
- **`STUDY` on the Bible:** the open loads `bible.json` passages through the passage-doc
  allocator, into PSRAM: `closePublication` resets the document onto `PsramJsonAllocator::passageDoc()` (`StudyStore.cpp:102`) before `PassageFile::load` fills it (`:79`). They are held
  until the next `openPublication`, exactly as after reading the Bible. A reader that later takes
  the parked Bible resumes it instead of reloading (`StudyStore.cpp:111-120`).

## Testing

- **No new host suite.** The brief asks for tests only of logic extracted here, and none is:
  - selection ordering is `TagChips::selectedInOrder`, tested in `test/tag_rows/TagChipRowTest.cpp`;
  - the change check is one `std::is_permutation`;
  - save, latch, pruning and budget are T2's, tested in
    `test/storage_io/PreachingTopicsFileTest.cpp:43-213`.

  Nothing touches `test/CMakeLists.txt`.
- **Build:** `pio run -e x4pro`, no new warnings. **Format:** `./bin/clang-format-fix` over the
  whole tree after `git add`.
- **Existing host suites run unchanged** (`ctest`), as a regression guard. No test file changes.
- **On the device** (the owner):
  1. Boot straight to Home, open Predicación → Mis temas → Elegir temas. The grid lists every tag.
     The serial log shows "Study opened for Elegir temas in N ms".
  2. Pick three, swipe back. Mis temas lists them in name order with counts. `preaching.json`
     holds the three ids.
  3. Reopen the picker. The three are Active. Swipe without changing anything: no count pass in
     the log.
  4. Long-press a picked tag and retire it, then swipe. Mis temas drops it, and `tags.json` still
     holds every other tag (the d1 hazard).
  5. Make `preaching.json` unreadable (e.g. `{"v":2}`), pick, swipe. The toast appears, the file
     is unchanged, and Mis temas is unchanged.
  6. Home from the picker saves and lands on Home.
  7. Free heap equal before Elegir temas and after returning.
