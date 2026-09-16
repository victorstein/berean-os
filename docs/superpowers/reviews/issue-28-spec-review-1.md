# Adversarial review — `2026-09-16-issue-28-design.md` (pass 1)

**Reviewed:** `docs/superpowers/specs/2026-09-16-issue-28-design.md` @ `9a7f9d77`
**Against:** issue #28 (`gh issue view 28`), `docs/superpowers/research/2026-09-16-issue-28-research.md`
**Branch state:** docs only — `git status --short` is clean and the branch carries no code, so there
is still no implementation to check. The pass-1 claim under test is the "What changed in pass 1"
table: *"All are accepted; none is half-applied."*

## Every pass-0 finding, re-checked against the code

| Pass 0 | Claimed | Verified |
|---|---|---|
| BLOCKER 1 — the byte guard froze a 219–242-record legacy file read-only | A-3 rewritten with a shrink exception | **Applied and correct.** Traced the blocker's own scenario through `bookmarkSaveAction`: 230 records (≈47,400 B on disk) minus one → `measured` ≈47,200; `47200 > 50000`? no; `47200 <= 45000`? no; `47200 < 47400` → `Write`. The set walks down monotonically, because each accepted write lowers `bytesOnDisk` for the next. The comparison is apples-to-apples: `SDCardManager::writeFile` writes exactly `content.length()` bytes (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:292-294`) and that `String` came from `serializeJson`, so file size == `measureJson`. The `>` against the read cap is right: `SDCardManager.cpp:204`'s loop is `while (f.available() && readSize < maxSize)`, so exactly 50,000 reads whole. The `"v":1` the new `toJson` adds (+6 B) never outweighs a deleted record (≥167 B, research §1) |
| BLOCKER 2 — the ≈99 KB internal-SRAM premise; the unrequested `MAX_BOOKMARKS` | A-2 deleted; figures corrected here and in the research note | **Applied and correct.** `CONFIG_SPIRAM_USE_MALLOC=y` / `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` really are at `framework-arduinoespressif32-libs/esp32s3/sdkconfig:2146-2147` on this machine (pass 0's `:2259-2260` was the wrong number; the spec's is right). `heap_caps.c:150-154` routes `realloc` over the limit to `MALLOC_CAP_SPIRAM`; `WString.cpp:212` is the `realloc`; `Esp.cpp:163-164` is `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`. All three of pass 0's numbered corrections landed, including the one I expected to have been dropped with the cap — human-tester item 5 pairs `ESP.getFreeHeap()` with `ESP.getFreePsram()`. Research §5 carries a dated `CORRECTED` block |
| MAJOR 3 — the `onEnter` bail would abort the firmware | A-9 added | **Applied and correct.** `isCancelled = true` routes `progressChangeResultHandler` to `openReaderMenu()` (`EpubReaderActivity.cpp:690-691`) instead of `std::get<ProgressChangeResult>` at `:693`. `finish()` from inside `onEnter` is handled: `ActivityManager.cpp:162-165` runs `onEnter` then `continue`s into the pop. A handler that pushes is handled too (`:133-138`). The spec also corrected pass 0's own slip — `ReaderUtils::showMessage` is `GUI.drawPopup` (`ReaderUtils.h:233`), and `drawPopup` is what calls `displayBuffer()` (`BaseTheme.cpp:853`) |
| MINOR 4 — `"v": 0` prose vs code | prose corrected | Applied. A-4 now says only *absent* is read as 1, matching `doc["v"] | FORMAT_VERSION` and `PassageDoc.cpp:101-103` |
| MINOR 5 — "enforced the same way" | moot, cap gone | Applied |
| MINOR 6 — test item 5 inverted its donor's point | rewritten | **Half-applied — see MAJOR 1** |
| MINOR 7 — six citation drifts | fixed throughout | All six fixed, and verified: `:1765-1803`, `TagPaletteFile.cpp:70` vs `HighlightFile.h:38-42`, `PassageDoc.cpp:131` / `:125-128`, `test/highlight_file/CMakeLists.txt:9-12`, `HighlightFileAction.h:34-47`, and the Error table now states the hidden menu entry |

I re-resolved every `file:line` in the spec. Beyond the findings below, they all land:
`BookmarkFile.cpp:17-19,47-56,59-62`, `BookmarkFile.h:11-13`, `BookmarkUtil.cpp:10-12,14`,
`HighlightFile.{h,cpp}` throughout, `HighlightFileAction.h:8-19,26-32,49-56`,
`PersistableStore.{h,cpp}:13-21,23,38-39`, `SaveBudget.h:19`, `StudyStore.cpp:37-47,56-62,208-241`,
`PassageFile.cpp:8,15-49,72-97`, `PassageSelectActivity.cpp:29-33`,
`EpubReaderActivity.cpp:55,240,247-250,283,343,689,690-693,871-873,877,1291,1737-1749,1765-1803`,
`EpubReaderActivity.h:46`, `EpubReaderBookmarksActivity.cpp:33,91-92,136-137,169-176,171-173,187-188`,
`ActivityManager.cpp:77,105,122-129,200-202`, `ActivityResult.h:88-96`, `HomeActivity.cpp:325`,
`ProgressMapper.cpp:141,176,804-811`, `Utf8.h:32`, `english.yaml:201,389,390,400`,
`HighlightDocTest.cpp:167-193,190-193`, `test/CMakeLists.txt`. `BookmarkFile::` still appears at
exactly the four sites A-8 names. `HomeActivity` really is never instantiated in `src/` — its only
reference outside its own pair of files is the `#include` at `ActivityManager.cpp:16` — and issue #29
carries exactly the title the spec quotes.

---

## MAJOR 1 — §Testing item 5 applies pass 0's MINOR 6 without reconciling it against pass 0's BLOCKER 2, and lands on a test that cannot fail

**Claim.** §Testing, `test/bookmark_doc/`, item 5: *"Worst case — 16-deep xpath (`MAX_XPATH_DEPTH`,
`ProgressMapper.cpp:141`), four-digit indices, a summary of characters JSON escapes — is measured and
**asserted to fit under the read cap with its margin recorded**, so a new field on `BookmarkEntry`
fails here first. This deliberately inverts `HighlightDocTest.cpp:190-193` … bookmarks have no count
cap, so for bookmarks the byte budget is the only bound and the test must prove it is a real one."*

**Problem.** The two pass-0 fixes collided and only one of them was thought through. Pass 0's MINOR 6
wording was written for a spec that still had `MAX_BOOKMARKS = 64`; with A-2 deleted there is no
bounded worst-case *document* left to measure, and the item is wrong three separate ways:

1. **There is no worst case.** `HighlightDocTest.cpp:171-185` builds a document at
   `HighlightDoc::MAX_HIGHLIGHTS` — the cap is what makes "the worst case" a finite object. Without a
   count cap the only measurable worst case is a single *record*, which the item's wording
   ("a new field on `BookmarkEntry` fails here first") half-admits.
2. **A single worst-case record cannot fail the assertion.** A 16-deep xpath with four-digit indices
   is ≲290 B (`XPathStep::tag` is 12 bytes, so each segment is at most `/` + 11 + `[9999]` = 18 B), a
   fully escaped 72-byte summary is ≤144 B, keys and scalars ~60 B — ≲500 B against a 50,000-byte cap.
   The assertion has ~49,500 bytes of slack. Adding a field to `BookmarkEntry` moves it by tens of
   bytes and the test still passes, so the stated purpose is not achieved.
3. **The "16-deep xpath" worst case contradicts the spec's own §Format.** §Format says *"`xpath` is
   **not** bounded"*, and it is right: `buildParagraphXPath` emits one segment per ancestor with no
   limit (`lib/ProgressMapper/ChapterXPathResolver.cpp:52-62`), and `MAX_XPATH_DEPTH` is only how many
   steps the *parser* will read — `parseXPathSteps`'s loop is `while (pos < stepsEnd && count <
   MAX_XPATH_DEPTH)` (`ProgressMapper.cpp:168`), which stops early rather than rejecting. A nested
   EPUB (div/table/ul) produces more than 16 segments and the bookmark stores all of them. So 16 is
   not a worst case at all, and the item cites `ProgressMapper.cpp:141` as if it bounded the field.

The last sentence also inverts itself: a test that asserts a document *fits* proves nothing about
whether a bound is real. The bound is `bookmarkSaveAction`, and `test/bookmark_save_action/` items
1–6 already pin it exhaustively.

**Evidence.**
- Donor's shape: `test/highlight_doc/HighlightDocTest.cpp:178` — `for (size_t i = 0; i < HighlightDoc::MAX_HIGHLIGHTS; ++i)`.
- Donor's point: `HighlightDocTest.cpp:190-193`, `EXPECT_GT(bytes, 50000u) << "if this ever fits, the caps changed"`.
- xpath unbounded: `ChapterXPathResolver.cpp:55-57`, `for (const auto& segment : path) xpath += "/" + segment.name + "[" + std::to_string(segment.index) + "]";`
- Parser truncates rather than rejects: `ProgressMapper.cpp:168`, and `:176` rejects only an
  over-long *element name*, not an over-deep path.
- Spec's own contradicting sentence: §Format, *"`xpath` is **not** bounded — truncating it corrupts
  the position it addresses"*.

**Concrete fix.** Replace item 5 with two assertions that can actually fail, and drop the
`MAX_XPATH_DEPTH` framing:

- **5a.** Measure one realistic-worst record (deep xpath, four-digit indices, fully escaped 72-byte
  summary, `vo` present) and `EXPECT_LT(bytes, N)` at a pinned per-record ceiling, with the measured
  value in the failure message. A new field on `BookmarkEntry` then fails here first, which is what
  the item wanted.
- **5b.** Assert the derived figure the design actually relies on: `SAVE_BYTE_BUDGET / bytesPerRecord`
  is still ≥ the ≈218 the spec quotes in A-2. That is the sentence in §Budget the whole "no record
  cap" decision rests on, and nothing currently tests it.

Then delete the "deliberately inverts `HighlightDocTest`" paragraph — with no count cap, the donor's
argument has no analogue here — and say instead that the byte guard is bounded-and-tested in
`test/bookmark_save_action/`, and that `test/bookmark_doc/` only pins per-record cost.

---

## MINOR 2 — §Testing item 6 contradicts `bookmarkSaveAction`

**Claim.** §Testing, `test/bookmark_save_action/`, item 6: *"Exactly at the budget, and exactly at the
cap, both write."*

**Problem.** "Exactly at the cap" does not write, except in a state the design declares unreachable.
Walk it through the function the spec prints:

```cpp
if (measuredBytes > readCap) return RefuseTooLarge;          // 50000 > 50000 → false
if (measuredBytes <= budget) return Write;                   // 50000 <= 45000 → false
return measuredBytes < bytesOnDisk ? Write : RefuseTooLarge; // needs bytesOnDisk > 50000
```

`Write` at exactly 50,000 requires `bytesOnDisk > 50,000` — a file `SDCardManager::readFile` truncates
(`SDCardManager.cpp:202-208`), which loads `Failed`, which latches saving off, so `save()` is never
reached. `EXPECT_EQ(bookmarkSaveAction(50000, 0, 45000, 50000), Write)` fails; the implementer has to
guess whether the function or the test is wrong.

**Concrete fix.** Reword item 6 to *"Exactly at the budget writes with any `bytesOnDisk`. Exactly at
the read cap writes only on the shrink arm (`bytesOnDisk > readCap`) and refuses otherwise — and note
that the shrink arm at that value is unreachable in practice, because a file over the cap loads
`Failed`."* That keeps the boundary pinned and records why it is boundary-only.

---

## MINOR 3 — A-1 and A-3 disagree about whether the 5,000-byte margin is negotiable

**Claim.** A-1 rejects raising the budget to ~49,000: *"that 5,000-byte margin is what stands between
a miscount — a field added to `BookmarkEntry`, an escape-heavy summary, a serialiser change — and
silent truncation, which is unrecoverable. Simplicity is not worth trading for it."* A-3 then permits
any write up to `persist::SD_READ_TRUNCATION_CAP`, i.e. with **zero** margin, whenever the document is
shrinking.

**Problem.** The design refuses to spend the margin for simplicity and then spends all of it for the
shrink exception — in exactly the population (inherited over-budget files) where the miscount risk is
highest, since those are the files this build did not write and whose per-record cost it has never
measured. The two paragraphs are two pages apart and never acknowledge each other.

It is defensible on the merits — `measureJson` and the bytes `writeFile` writes are the same number
(`PersistableStore.cpp:28` → `SDCardManager.cpp:292-294`), so no miscount is possible between the
guard and the write — but that argument belongs in the spec, and it is the same argument that
undercuts A-1's rejection of option (c).

**Concrete fix.** One sentence in A-3: the shrink arm may run without the budget's margin because
`measureJson` measures the exact byte count `Storage.writeFile` then writes, so the only quantity the
margin protects against — a future divergence between what is measured and what is stored — is not
in play on a path that is already strictly reducing the file. And one sentence in A-1 conceding that
the margin's job is headroom for *future* record-shape changes, not for the write path itself.

---

## MINOR 4 — the rejection of option (b) claims there is no on-device recovery path; there is one

**Claim.** A-3's rejection of option (b): *"**That escape hatch does not exist on this firmware.** …
**Under (b) the real recovery path is physically removing the SD card and editing the file on a
computer**, which is not a recovery path this fix may hand a user."*

**Problem.** The web-server half is right and well-evidenced (I confirmed `goToFileTransfer` at
`ActivityManager.cpp:200-202` has exactly one caller, `HomeActivity.cpp:325`, reached from
`HomeActivity.cpp:164`, and `HomeActivity` is never instantiated). But the generalisation to "the real
recovery path is physically removing the SD card" is false: the device has a file browser that can
show dotfiles and delete files.

- `FileBrowserActivity.cpp:56` — `if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') || …) continue;`
- `SettingsList.h:376` — `SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, …)`, a user-facing toggle.
- `FileBrowserActivity.cpp:308-310` — a `STR_DELETE` confirmation, `:170` / `:228` — `Storage.remove(...)`.

The path is narrow — the browser is reachable only from `LauncherActivity::openBible()` when no
Bible-looking book is in recents (`LauncherActivity.cpp:98-100, 499-502`), so a user whose oversized
file belongs to the Bible cannot reach it — but "delete `/.crosspoint/bookmarks/<name>.json` on the
device" is a real option the spec says does not exist.

This does not change the decision: A-3 is still better than (b), because deleting one bookmark beats
deleting all of them. It changes the accuracy of the argument used to reject (b), which pass 0
flagged as needing the human's sign-off.

**Concrete fix.** Rewrite that sentence: the web-server escape hatch does not exist (issue #29); the
only on-device one is Settings → show hidden files → File Browser → delete the whole bookmark file,
which destroys every bookmark for the book and is not reachable at all while the launcher finds a
Bible. A-3 is preferred because it recovers one record at a time.

---

## MINOR 5 — the new stat-then-write is a two-call sequence, and `BookmarkFile`'s single-writer premise is never stated

**Claim.** §Budget: *"`bytesOnDisk` is read only when it can change the answer"*, with a
`Storage.openFileForRead` / `existing.size()` probe, then §Save step 5 writes through
`writeDocToFileAtomic`.

**Problem.** `HalStorage` serialises each *call*, not the sequence: `HalFile::size()` takes and
releases `storageMutex`, and so does every call inside `writeDocToFileAtomic`. So `bytesOnDisk` can be
stale by the time the shrink decision is acted on. That is harmless only under a single writer —
which is true today (both call sites are reader-side activities on the main task,
`EpubReaderActivity.cpp:1747,1801` and `EpubReaderBookmarksActivity.cpp:33,175`) but is nowhere
written down. The donor states it explicitly and the spec does not carry it over:
`HighlightFile.h:16-18`, *"Single-writer only: the static helpers here take no lock. If the web server
ever writes highlights alongside the main task, add a mutex."* CLAUDE.md's storage discipline asks for
the same thing in rule 5 ("Name its owning task").

**Concrete fix.** Copy `HighlightFile.h:16-18`'s note onto `BookmarkFile.h`, extended to name the new
hazard: the `bytesOnDisk` probe and the write are separate `storageMutex` acquisitions, so a second
writer would make the shrink decision race. Name the owning task (the main/UI task) in §Save.

---

## MINOR 6 — the load-failed toast re-fires on every return through `loadCachedBookmarks()`

**Claim.** §`EpubReaderActivity` — the latch: the `Failed` branch sets `bookmarksSaveDisabled` and
calls `ReaderUtils::showMessage(...)` *inside* `loadCachedBookmarks()`, *"mirroring what `onEnter`
already does for the study store nine lines later (`:240`, then `:247-250`)"*, and then notes
*"`loadCachedBookmarks()` is also called on return from the progress-change flow (`:689`)."*

**Problem.** It does not mirror the donor. The study-store surfacing at
`EpubReaderActivity.cpp:247-250` is in `onEnter` and runs once per book open. `loadCachedBookmarks()`
runs again at `:689` on every return from the bookmarks list and the progress-change flow, so under a
persistent SD fault the user gets a full-screen popup — `GUI.drawPopup` ends in
`renderer.displayBuffer()` (`BaseTheme.cpp:853`), a full e-ink refresh — on every one of those
returns, immediately before `openReaderMenu()` repaints. The latch is already set; the repeat carries
no information.

**Concrete fix.** Guard the toast on the latch transition, not on the result:
`if (result == Failed && !bookmarksSaveDisabled) { bookmarksSaveDisabled = true; showMessage(...); }`
— one word of change, and it makes "the latch is never cleared" do real work.

---

## MINOR 7 — two supporting claims do not check out

Both are rationale rather than mechanism, but this spec's authority is its citations.

| Spec says | Actual |
|---|---|
| Non-goals: *"**Widening the shared `highlightSaveAction`.** Three stores depend on it (`src/util/HighlightFile.cpp:76`, and `src/study/PassageFile.cpp:72` uses its load-side sibling)."* | Exactly **one** store uses `highlightSaveAction` — `HighlightFile.cpp:76`, the only hit in `src lib test`. Three use the *load-side* `highlightLoadAction` (`HighlightFile.cpp:40`, `PassageFile.cpp:72`, `TagPaletteFile.cpp:39`), and `TagPaletteFile` — which does not go through the shared save helper at all, it inlines `measureJson(json) > persist::DEFAULT_SAVE_BUDGET` at `:70` — is not named. Keeping the shrink logic bookmark-local is still the right call; the blast-radius argument for it is not true as stated |
| §Architecture: *"`BookmarkDoc.cpp` does not instantiate the JSON serializer … `HighlightDoc.cpp` and `PassageDoc.cpp` are the proof this is flash-neutral."* | Half of the proof proves the opposite. `PassageDoc.cpp:134-137` — `size_t PassageDoc::measureBytes() const { JsonDocument doc; toJson(doc); return measureJson(doc); }` — and `measureJson` is precisely the serializer template `PersistableStore.h:16-20` exists to keep out of per-store TUs. `HighlightDoc.cpp` genuinely has no `measureJson` and is the real precedent. The design's own choice is fine: A-5 means `BookmarkDoc::fromJson` never measures, so `BookmarkDoc` needs no `measureBytes()` at all — but §Architecture should cite `HighlightDoc.cpp` alone and say *why* `BookmarkDoc` escapes `PassageDoc`'s fate |

---

## MINOR 8 — CLAUDE.md's storage rule 3 is declined without being named

**Claim.** §Rejected alternatives, *"Stream the read"*: rejected because *"with the corrected memory
picture it buys almost nothing in internal SRAM"* and because it would duplicate a file-local class.

**Problem.** CLAUDE.md's storage discipline says every store must *"3. Stream, not
`Storage.readFile`, if it can exceed ~40 KB."* `SAVE_BYTE_BUDGET` is 45,000, so by the project's own
rule this store must stream, and `PassageFile` — the newest store — does (`PassageFile.cpp:15-49`).
The spec answers a memory question the rule never asked: the rule is about the silent 50,000-byte
truncation, which streaming removes outright and which is the root of the entire loadable-but-
unsaveable band A-3 exists to handle. Declining a governing rule is allowed; declining it without
naming it leaves the implementer to rediscover the conflict.

Streaming would *not* remove the need for A-3 — an over-budget legacy file would still be refused on
the way out — so the rejection is probably still right. But the spec should say so on the rule's own
terms.

**Concrete fix.** Add to the rejection: CLAUDE.md storage rule 3 asks for streaming above ~40 KB and
`PassageFile.cpp:15-49` follows it; bookmarks deliberately follow `HighlightFile`'s precedent instead,
because streaming removes the read cap but not the budget refusal, so the shrink exception would be
needed either way — and it costs a second copy of `HalFileReader` in a data-loss fix. Revisit with
issue #29 or whenever the budget moves.

---

## Verified and not a finding

- **The shrink exception is safe against the cap.** Every value the `Write` arm can return is
  ≤ 50,000, and `SDCardManager::readFile` reads a 50,000-byte file in full, so nothing the new logic
  permits can produce a file that reads back truncated.
- **`bytesOnDisk` is comparable to `measureJson`.** `PersistableStore.cpp:28` serialises into a
  `String`; `SDCardManager.cpp:292-294` writes `content.length()` bytes and verifies the count.
- **A `Failed` load cannot leave a partly-filled vector.** In the spec's A-4 shape both `fromJson`
  rejections (`!doc.is<JsonObjectConst>()`, the version guard) precede the fill, and `ReportFailed`
  never calls `fromJson`, so the Error table's "`cachedBookmarks` is empty, so `EpubReaderActivity.cpp:283`
  hides the Bookmarks menu entry" holds.
- **A-7 still holds.** `STR_HIGHLIGHTS_TOO_LARGE` (`english.yaml:390`) is still unreferenced outside
  the YAML and still says "highlights". Routing three refusals through `STR_ERROR_GENERAL_FAILURE`
  (`english.yaml:201`) meets criterion 3 without touching the files the brief reserves.
- **A-8's blast radius is exactly four call sites**, still.
- **The host-test plumbing works as described.** `crosspoint_test_common` already puts `${REPO_ROOT}`
  on the include path (`test/CMakeLists.txt:38-41`) and `test/highlight_file/CMakeLists.txt:9-12` adds
  `${REPO_ROOT}/src`; `src/BookmarkEntry.h` pulls only `<cstdint>`/`<string>`.

## What I could not verify

- Nothing was built or run. The branch adds no code, and no finding here depends on a compile.
- Whether a 219+ record bookmark file exists in the field. As in pass 0, BLOCKER 1's descendant
  reasoning is reachability from the research's own per-record measurement, not an observation.
- The PSRAM/internal split is still derived from the framework config and allocator source on this
  machine, not measured on hardware. Human-tester item 5 is the right instrument for it.

---

1 MAJOR, 7 MINORs, no BLOCKERs. Both pass-0 blockers and the major are genuinely fixed — I traced
each through the code rather than taking the table's word for it — and the one half-applied fix
(MAJOR 1) is a test-plan incoherence with a concrete rewrite, not a design reversal. Everything below
BLOCKER is fixable inline without a decision only the human can make.

VERDICT: CLEAR
