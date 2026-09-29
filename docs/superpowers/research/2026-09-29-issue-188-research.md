# Issue #188 research — tagged passages keep the full verse

Base: `fix/188-full-verse-passages` at `a4e2288d` (release 1.19.5, which includes #183 / `945139dd`).
Every line number below was read at that commit.

## 1. Who owns the behaviour

| Concern | File | What it does today |
|---|---|---|
| Format, caps, validation | `lib/StudyStore/StudyStore/PassageDoc.h:25-50`, `PassageDoc.cpp` | `FORMAT_VERSION = 3`, `MAX_SNIPPET_BYTES = 120`, `MAX_DISPLAY_TEXT_BYTES = 384`, `SAVE_BYTE_BUDGET = 200000` |
| Record shape | `lib/StudyStore/StudyStore/TaggedPassage.h:30-43` | `snippet` (`"x"`), `displayText` (`"w"`), `start`/`end` Units, `document`/`documentSpine` hints, `pendingUpgrade` (`"g"`) |
| Storage shell | `src/study/PassageFile.cpp:18-37` | streamed load via `loadAdopting` + `readDocFromFileStreamed`; save checks `SAVE_BYTE_BUDGET` then `writeDocToFileAtomic` |
| Resident store | `src/study/StudyStore.{h,cpp}` | one publication's `PassageDoc` resident from `openPublication` (`:49`) to `closePublication` (`:71`) |
| Capture | `src/activities/reader/PassageSelectActivity.cpp:368-390`, `PassageLabel.h`, `PassageSelectActivity.h:154` | joins the selected laid-out words into a `passage_label::Builder` capped at `MAX_DISPLAY_TEXT_BYTES` |
| Sleep pick | `src/activities/boot_sleep/StudySleepScreen.cpp:101-199`, `StudySleepPick.h` | re-reads every `/.berean/passages/*.json` from SD at sleep time; does **not** use the resident `StudyStore` |
| Sleep fit | `src/activities/boot_sleep/StudySleepFit.h:101-137` | `fitPassage` over serif 18/16/14/12; at 12pt it truncates and appends `…` |
| Format doc | `docs/file-formats.md:487-518` | documents v1/v2/v3 and "`w` is 121..384 bytes" |

## 2. Current control flow

### 2a. Capture → store (the three cuts, located)

1. `PassageSelectActivity::appendWords` (`PassageSelectActivity.cpp:245-260`) feeds each selected
   word into `label` — a `passage_label::Builder{MAX_DISPLAY_TEXT_BYTES}` (`PassageSelectActivity.h:154`).
   `Builder::addWord` stops at the first word that would leave no room for `…` and `text()` appends
   `…` (`PassageLabel.h:26-41`). **Cut #2 happens here, before the store ever sees the text.**
   The text is the *selected words only*: it starts and ends wherever the user tapped, so a mid-verse
   selection stores a mid-verse text.
2. `finalizeSelection` (`:368-390`) calls
   `STUDY.addPassage(spineIndex, range.start, range.end, label.text(), verseReference(range.start), tags)`.
   `range.end` is the last word's offset **+ 1** (half-open, comment at `:376-380`).
3. `StudyStore::addPassage` (`StudyStore.cpp:276-300`) resolves `start`/`end` with
   `study::resolve(units, offset)` (`UnitAnchors.cpp:88-99`: the anchor at or before the offset, with
   `Unit::offset` relative to it), then sets **both** `snippet` and `displayText` to that same label
   string (`:286-287`).
4. `PassageDoc::add` (`PassageDoc.cpp:114-134`) runs `utf8SafeSummary(snippet, 120)` and
   `utf8SafeSummary(displayText, 384)`, clears `displayText` when it is ≤ 120 bytes (`:123`), and
   refuses the append if `measureBytes() > SAVE_BYTE_BUDGET` (`:129-132`). A refusal returns `false`;
   `finalizeSelection` then shows `STR_HIGHLIGHTS_SAVE_FAILED` (`:382-386`). So "refuse and report
   with a UI message" **already exists** for an over-budget add.
5. `StudyStore::save` → `PassageFile::save` (`PassageFile.cpp:26-37`): builds the whole
   `JsonDocument`, refuses over budget (`SaveResult::TooLarge`), else `writeDocToFileAtomic`.
   `addPassage` rolls the append back on failure (`StudyStore.cpp:295-299`).

Selections never cross a spine document: `advancePage` only calls `section.loadPage(currentPageNumber + 1)`
on the same `Section` (`PassageSelectActivity.cpp:131-145`). So `start` and `end` always live in
one document, `spineIndex`.

### 2b. Load and version rules

- `fromJson` refuses a future `v` (`PassageDoc.cpp:250-251`, `persist::isKnownFormatVersion`,
  `lib/Serialization/FormatVersion.h:13-15`).
- `displayTextFromJson` **refuses the whole file** when a `"w"` is ≤ 120 or > 384 bytes
  (`PassageDoc.cpp:87-96`). A refused load latches `saveDisabled_` for the session
  (`StudyStore.cpp:49-53`). So a file written with a longer `"w"` under an unchanged `v:3` would
  make every 1.19.x build refuse the whole file and latch saving off; a new `v` makes those builds
  refuse it by version instead, which is the rule the header states (`PassageDoc.h:36-50`).
- The snippet is re-summarised on load, never refused (`:265`).
- The file is written at the lowest version that holds it (`formatVersionFor`, `:106-110`).

### 2c. Sleep screen

- `render` (`StudySleepScreen.cpp:373-408`) heap-allocates a `Sampler` (two `Candidate`s, each with a
  fixed `char text[385]`, `StudySleepPick.h:19-23,84-90`) and scans the passages directory.
- `offerFile` (`:135-163`) parses each file whole into a `JsonDocument` via
  `readDocFromFileStreamed` (no 50,000-byte cap), per file ≤ `SAVE_BYTE_BUDGET + 4096`, total
  ≤ 262,144 bytes (`:43-47`).
- `offerRow` (`:101-132`) uses `"w"` when `wholeTextFits(w, 384)` (`StudySleepPick.h:146-148`),
  else the 120-byte `"x"` — **cut #1**: every pre-#183 row, and every row whose text was ≤ 120 bytes
  of a longer verse, shows its snippet.
- `Sampler::copyInto` silently clips anything over `TEXT_CAPACITY - 1` (`StudySleepPick.h:119-123`).
  With an uncapped `"w"` this would be a new silent cut; it must change.
- `drawScreen` (`:295-369`) reserves fixed chrome — date line (UI 12), opening quote (serif 18 bold),
  reference (UI 12 bold), tag pill (small font), progress strip (48 px bars + caption), Berean mark
  (40 px) and footer — and gives the passage the rest. `fitPassage` tries serif 18/16/14/12 italic
  (`PASSAGE_FONT_IDS`, `:64-66`) and at 12pt truncates with `…` (`StudySleepFit.h:119-136`) — **cut #3**.
- The `Study pick` heap log line the acceptance criterion names already exists
  (`StudySleepScreen.cpp:385-387`), as does the start line (`:374-375`).

### 2d. Fonts available below 12pt

`src/fontIds.h:4-14` and `src/main.cpp:68-129,307-320`: Noto Serif and Noto Sans exist at 12/14/16/18
only. Below 12pt there is `UI_10_FONT_ID` (Ubuntu 10, regular + bold, **no italic**) and
`SMALL_FONT_ID` (Noto Sans 8 **regular only**). There is no serif face below 12pt. A Noto Serif 10
italic would be a new built-in font: `lib/EpdFont/builtinFonts/notoserif_12_italic.h` is 334,032
bytes of source, and adding one touches `src/main.cpp` and `src/fontIds.h` (a shared file per
`.claude/agents/data-dev.md`) plus the font conversion step. Its flash cost is not measured here.

## 3. The verse machinery the fix must reuse

- `Unit` (`lib/StudyStore/StudyStore/Unit.h:23-33`): `kind` ∈ {DocumentOffset, Paragraph, Verse};
  for Verse, `book`/`major`=chapter/`minor`=verse, `offset` in codepoints into the unit.
- `DocumentUnits` + `resolve` / `anchorIndexOf` / `unitEndOffset` / `documentOffsetOf`
  (`UnitAnchors.h:17-74`, `.cpp:88-120`). `unitEndOffset` returns the next anchor's offset or
  `UINT32_MAX` for the last unit. **Verse snapping is expressible with these alone:** the whole-verse
  span of a passage is `[anchor(start).offset, unitEndOffset(anchorIndexOf(end)))`.
- `UnitIndexCache::unitsFor(spine)` (`src/study/UnitIndexCache.h:35`) gives a document's units,
  building and persisting them on first use; `unitText(spine, unit)` (`UnitIndexCache.cpp:388-398`)
  streams the document through `SpineHtmlStream` into a `UnitTextScanner` with
  `setRange(anchor, unitEndOffset)` and returns exactly one unit's text. `UnitTextScanner::setRange`
  takes any `[from, to)` (`UnitText.h:43-44`), so a multi-verse span is one streamed pass, not N.
- `UnitText` is the single producer of "a unit's visible text"; its header warns that the reader's
  laid-out words lose the U+202F before verse text (`UnitText.h:9-14`). Its test fixture shows what a
  unit's text contains: the `<sup>` verse number, then U+202F, then the words
  (`test/unit_text/UnitTextTest.cpp:9-16,48-53`). **So a verse-snapped text built from UnitText
  embeds verse numbers** ("7 alpha bravo 8 charlie delta"). Whether the stored/shown text keeps them
  is a spec decision.
- The last unit of a document runs to `UINT32_MAX`, i.e. to the end of `<body>`
  (`UnitAnchors.cpp:108-111`, `UnitText.cpp:134-147`). Whether an NWT chapter document carries
  footnotes or other trailing text after its last verse is **not verified here**; if it does, a
  passage ending on a chapter's last verse would capture it. The spec must check a real document.
- Kinds per document, measured on the NWT (`Unit.h:17-20`): 1,189 Verse, 153 Paragraph, 2,595
  DocumentOffset. The two meeting publications address by `data-pid` (Paragraph). "Whole verse" has
  no meaning for Paragraph or DocumentOffset units; the spec must say what those snap to (whole
  paragraph, or the stored range unchanged). `VerseAnchors` does not generalise
  (`CLAUDE.md`, "Do not repeat these").

## 4. Memory

- `CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2153-2154`): a
  `std::string` under 4,096 bytes lands in internal SRAM. Every per-passage text will be under that,
  so an uncapped resident `displayText` is internal SRAM, one block per passage.
- #183 accepted ~25 KB resident at the 384 cap (spec A16, `docs/superpowers/specs/2026-09-27-issue-182-design.md:265-279`,
  Resources table `:393-395`) and rejected (a) a PSRAM `std::string` allocator — no string/JSON store
  in the repo uses one; the precedents are raw buffers, `BibleSearchStore::psramAllocator`
  (`src/study/BibleSearchStore.cpp:25-27,97`) — and (b) non-resident text, because
  `PassageDoc::toJson` rewrites the whole file from `passages_`, so a text not in RAM is erased on
  the next save.
- Rough scale, from #183's own figure of ~705 B for a 5–7-verse passage (issue #188, spec A1): the
  owner's 63 passages at ~700 B each is ~44 KB resident, ~88 KB during a save (the transient
  `JsonDocument` copy). That is above the ~25 KB #183 accepted and near the ~50 KB free-heap floor
  #183 designed around. This is an estimate, not a measurement.
- The sleep path is separate from the resident store: it parses one file at a time and frees it,
  so its transient peak is one file's `JsonDocument`.

## 5. Repair: what exists to build on

- **No lazy on-open repair exists.** `pendingUpgrade` ("upgrade on next open", `TaggedPassage.h:42`)
  is written by `MigrationPlanner.cpp:51` and serialised as `"g"` (`PassageDoc.cpp:230,267`), but
  nothing in `src/` consumes it (`grep -rn pendingUpgrade src lib` shows only the migration summary
  counters, `src/study/MigrationRunner.cpp:147,336`, `src/main.cpp:516`).
- **The in-memory spine-hint repair is the nearest on-open precedent**: `passagesInDocument`
  repairs `documentSpine` in memory only and relies on the next edit to persist it, precisely
  because it runs on the page-turn path (`StudyStore.cpp:253-256`); `locate` does the same
  (`:149-156`).
- **The nearest bulk rewrite of a passages file is `MigrationRunner`** (`src/study/MigrationRunner.{h,cpp}`):
  runs at boot from `src/main.cpp:482-517`, opens the EPUB, builds a `UnitIndexCache`, calls
  `unitText` per passage, yields `vTaskDelay(1)` per document to stay under the 5 s task watchdog
  (`MigrationRunner.cpp:359-364`), then one `PassageFile::save`. Idempotent via a ledger.
- **Only the open publication can be resolved.** There is no pubkey → EPUB path lookup except
  `PubKeyRegistry::findBySymbol` for downloaded publications (`src/study/PubKeyRegistry.h:27-30`);
  `BookPathIndex` only inverts legacy highlight filenames (`src/study/BookPathIndex.h:18-24`). The
  Bible's pubkey is language-free (`Unit.h:9-15`). So a repair keyed on "the publication the user
  just opened", where `StudyStore` already holds the `Epub` and a ready `UnitIndexCache`, needs no
  new lookup; a boot/sleep-time repair across all files would.
- `storageMutex` is recursive and taken by every `HalFile` operation (`lib/hal/HalStorage.cpp:20,34-35`);
  `writeDocToFileAtomic` streams to `<path>.tmp` and renames (`lib/Serialization/PersistableStore.cpp:104-122`).
  Every existing passage write already goes through it.

## 6. Tests that exist today

- `test/passage_doc/PassageDocTest.cpp` — `PassageDoc` format, caps and version rules.
- `test/passage_label/PassageLabelTest.cpp` — `Builder`'s cap and ellipsis.
- `test/study_sleep_pick/StudySleepFitTest.cpp`, `StudySleepPickTest.cpp` — `fitPassage`, `Sampler`,
  `wholeTextFits`.
- `test/unit_text/UnitTextTest.cpp`, `test/unit_anchors/` — unit text extraction and resolution.
- Registered at `test/CMakeLists.txt:111,113,115,126,127` (a shared append point: new suites are
  reported, not edited, per `data-dev.md`).
- #183 recorded that nothing in `test/` constructs `PassageSelectActivity` (spec A3), so a
  mid-verse-selection host test has to target a pure function (e.g. the snapping over
  `DocumentUnits`), not the activity.

## 7. Installed versions

| Tool / package | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini` `[base] platform =` |
| Arduino core | framework-arduinoespressif32 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json` |
| ArduinoJson (firmware and host tests) | 7.4.2 | `platformio.ini:155`; `test/CMakeLists.txt:30-31` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:15-17` |
| CMake | 4.4.2 | `cmake --version` |

## 8. The nearest existing example of this change

`945139dd` (#183) is the same kind of change on the same files: a `PassageDoc` format bump
(v2 → v3, lowest-version-that-holds-it), a new validated field refused rather than cut on load,
the capture path in `PassageSelectActivity`/`PassageLabel.h`, the sleep pick/fit headers with host
tests in `test/passage_doc`, `test/passage_label`, `test/study_sleep_pick`, and a
`docs/file-formats.md` update. Its spec is `docs/superpowers/specs/2026-09-27-issue-182-design.md`.
For the repair pass, `MigrationRunner` (bulk, EPUB-resolved, watchdog-yielding, atomic save) and
`StudyStore::passagesInDocument` (on-open, in-memory hint repair) are the two patterns to mirror.

## 9. Scope and tier

The work spans more than surface `data`: `src/activities/reader/PassageSelectActivity.*` and
`src/activities/boot_sleep/StudySleep*` are `ui`, and a sub-12pt serif face would add a built-in font
(`lib/EpdFont`, `src/main.cpp`, `src/fontIds.h`). It is also a format migration (v3 → v4 plus a
repair of existing records). The tier is already `heavy`, the highest, so no change is needed.

## 10. Open questions the spec must settle

1. Keep or strip the embedded verse numbers in a verse-snapped text (§3).
2. What Paragraph and DocumentOffset passages snap to (§3).
3. Whether a chapter's last verse picks up trailing non-verse text (§3, unverified).
4. Where uncapped text lives: resident internal SRAM (~44 KB estimated), PSRAM, or on demand with a
   save path that preserves text it did not load (§4).
5. When the repair runs: on publication open (no new lookup, only repairs what is opened) or
   across all files at boot/sleep (needs a pubkey → EPUB resolver) (§5).
6. Sub-12pt: the italic 12pt serif is the smallest serif face; below it the options are a new font or
   a non-serif, non-italic face (§2d).
