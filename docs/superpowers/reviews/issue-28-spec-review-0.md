# Adversarial review — `2026-09-16-issue-28-design.md` (pass 0)

**Reviewed:** `docs/superpowers/specs/2026-09-16-issue-28-design.md` @ `07527bcd`
**Against:** issue #28 (`gh issue view 28`), `docs/superpowers/research/2026-09-16-issue-28-research.md`
**Branch state:** docs only — `git diff main --stat` shows the two markdown files and no code, so
there is no prior implementation pass to check for a half-applied fix.

The spec is unusually well-evidenced: every `file:line` I spot-checked in the Architecture,
Control-flow and Rejected-alternatives sections resolved to the code it claims (`HighlightFile.cpp:23-70`,
`HighlightFileAction.h:34-56`, `PassageDoc.cpp:101-103,116-117,131`, `StudyStore.cpp:37-47,56-62,208-241`,
`PassageFile.cpp:8,15-49,72-97`, `PersistableStore.cpp:23,35-42,50-59`, `TagPaletteFile.cpp:70`,
`HighlightDocTest.cpp:167-193`, `english.yaml:201,389,390,398,400`). `STR_HIGHLIGHTS_TOO_LARGE` really
is unreferenced (`grep -rn STR_HIGHLIGHTS_TOO_LARGE src lib test` → one hit, the YAML). `docs/file-formats.md`
really does mention none of highlights/passages/palette/bookmarks. A-8's "both call sites" is exactly
right — `BookmarkFile::` appears at `EpubReaderActivity.cpp:1747,1801` and
`EpubReaderBookmarksActivity.cpp:33,175` and nowhere else.

The problems are in the two assumptions the spec nominates as its real choices — A-2, A-3 and A-5 —
and in one integration seam it copies from the wrong donor.

---

## BLOCKER 1 — A-5's "recoverable by deleting" is false; the design freezes a legacy 219–242-bookmark file read-only, a regression on today's behaviour

**Claim.** §Load, A-5: *"The two states are genuinely different: unreadable is unrecoverable and must
never be written over; **too big but read perfectly is recoverable by deleting**, and the save guard
already refuses the write."* A-3 rests on the same promise: *"Had the count been checked on save,
every delete would be refused and the set could never be brought back under the cap — an
unrecoverable dead end reachable from existing data."*

**Problem.** The byte guard reproduces that dead end exactly, for a band the research says is
reachable. Work the numbers through the spec's own control flow with a legacy file of 230 typical
records (research §1: 206 B/record → ≈47,400 serialised bytes — under `SDCardManager`'s 50,000-byte
cap at `SDCardManager.cpp:202`, over `SAVE_BYTE_BUDGET` at 45,000):

1. `readDocFromFileChecked` → `Ok`; `highlightLoadAction` → `UseLoaded` (`HighlightFileAction.h:37-38`).
2. `BookmarkDoc::fromJson` never drops for budget (A-5) → `LoadResult::Loaded`, 230 entries, no latch.
3. User deletes one in `EpubReaderBookmarksActivity::deleteSelectedBookmark`. The set is now 229 ≈
   47,200 B.
4. §Save step 2: `highlightSaveAction(47200, 45000)` → `RefuseTooLarge` → `SaveResult::TooLarge`.
5. §EpubReaderBookmarksActivity: *"on a non-`Ok` save re-insert it at its index"* → the entry comes
   back. Net effect: nothing deleted, and the next attempt is identical.

The set can only become saveable after ~23 deletions, and no single deletion can be persisted, so it
never shrinks. `EpubReaderActivity::addBookmark()`'s remove path is the same — §Reader step 5 rolls
the erase back on anything but `Ok`, so the user cannot un-bookmark the current page either.
The file is permanently read-only through every UI surface.

This is worse than the status quo. Today `BookmarkFile.cpp:62` writes unconditionally, so the same
user can delete down freely. A-5 deliberately chooses `Loaded` over `PassageDoc::fromJson`'s
`Failed` (`PassageDoc.cpp:131`) *in order to* keep deletion working, and then the save guard takes it
away — so the design pays A-5's cost (a store that loads without a latch and can be written over)
and gets none of its benefit. `HighlightFile` and `PassageFile` do not have this problem because
`HighlightDoc::addHighlight` (`HighlightDoc.cpp:56-57`) and `PassageDoc::add` (`PassageDoc.cpp:33-36`)
refuse at the budget on the way in, so neither can ever hold a document its own save would refuse.
Bookmarks inherit unbudgeted files written by CrossPoint, so they can and will.

**Evidence.**
- Reachable band, from the research the spec calls settled: `research §1` — typical record 206 B,
  218 records to 45,000, 242 records to the 50,000 cap. 219–242 records is loadable-and-unsaveable.
- Refusal is unconditional on measured bytes: `HighlightFileAction.h:54-55`
  `return measuredBytes > budget ? RefuseTooLarge : Write;` — no notion of "smaller than before".
- Rollback is unconditional on non-`Ok`: spec §EpubReaderBookmarksActivity and §Reader step 5.
- Today's behaviour: `src/util/BookmarkFile.cpp:62`, no measurement, delete always persists.

**Concrete fix (needs a decision, hence BLOCKER).** Pick one and write it into A-3/A-5 explicitly:
- **(a) Shrink exception.** Have `BookmarkFile::save` accept a document that is over budget but
  *strictly smaller than the bytes currently on disk* and still under
  `persist::SD_READ_TRUNCATION_CAP` (`SaveBudget.h:19`). This is the only option that keeps A-5's
  promise. It needs a second, load-time "bytes as read" figure threaded into `save`, or a
  `Storage.size()` probe before the write, and a new `HighlightSaveAction` arm (or a bookmark-local
  decision function — do not widen the shared one used by three stores).
- **(b) Drop A-5.** Make over-budget-on-load `Failed`, exactly like `PassageDoc.cpp:131`, latch
  saving off, and state plainly in the Error-handling table that a 219+ bookmark file is read-only
  until the user removes it over the web server. That is honest and costs nothing to implement, but
  it is a deliberate data-accessibility regression for existing users and the human must sign it off.
- **(c) Raise `SAVE_BYTE_BUDGET` to just under the read cap** (e.g. 49,000) so the loadable and the
  saveable bands coincide, and rely on A-2 to bound growth. This makes the two invariants agree at
  the cost of the headroom `HighlightFile.h:39-41` deliberately keeps.

Whichever is chosen, add the case to §Testing as a host test on `BookmarkDoc`: a document of 230
typical records minus one entry still measures over `SAVE_BYTE_BUDGET`.

---

## BLOCKER 2 — A-2 introduces a user-facing 64-bookmark limit that neither the issue nor the brief asks for, on an internal-SRAM premise that is wrong for this build

**Claim.** A-1/A-2: the 45,000-byte budget is *"a number the device cannot afford to reach"* because
*"a document at this budget costs ≈99 KB of internal SRAM concurrently (45,129-byte `String` alive
across `deserializeJson` … plus a 53,892-byte `JsonDocument` pool)"*, and *"the RAM argument is
answered by A-2"* — `MAX_BOOKMARKS = 64`. The research states the premise outright (§5): *"ArduinoJson's
default allocator is `malloc`, so this is internal SRAM, not PSRAM."*

**Problem — the premise is false on this target for the largest of those two allocations.** This
firmware builds with `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so
`malloc`/`realloc` of **more than 4096 bytes prefers the PSRAM heap**, with internal SRAM only as a
fallback. The 45,129-byte Arduino `String` — the single biggest line in research §5's table — does
not land in internal SRAM at all.

**Evidence.**
```
$ grep -n "CONFIG_SPIRAM_USE_MALLOC\|CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL" \
    ~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig
2259:CONFIG_SPIRAM_USE_MALLOC=y
2260:CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
```
(identical at `sdkconfig.x4pro:2146-2147` in a sibling worktree of this repo, `CONFIG_IDF_TARGET="esp32s3"`
at `:393`; `platformio.ini:91-113`'s `custom_sdkconfig` does not touch any `SPIRAM_*` key.)

- `framework-espidf/components/esp_psram/system_layer/esp_psram.c:135` —
  `heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);` under `#if CONFIG_SPIRAM_USE_MALLOC`.
- `framework-espidf/components/heap/heap_caps.c:117-121` — `heap_caps_malloc_default`:
  `size <= malloc_alwaysinternal_limit` → `MALLOC_CAP_INTERNAL`, **else `MALLOC_CAP_SPIRAM`**.
  `:150-154` is the same routing for `heap_caps_realloc_default`.
- The `String` gets there by `realloc`: `framework-arduinoespressif32/cores/esp32/WString.cpp:212`,
  `realloc(isSSO() ? nullptr : wbuffer(), newSize)`, called from `changeBuffer`/`reserve` as
  `SDCardManager.cpp:206` appends byte by byte.
- The pool, by contrast, *does* stay internal: ArduinoJson 7.4.2 sizes each variant pool at exactly
  `ARDUINOJSON_POOL_CAPACITY 256 // 4096 bytes` on 32-bit (`ArduinoJson/Configuration.hpp:118`), and
  `4096 <= 4096` takes the internal branch. String nodes are individually small and also internal.

So the corrected figure is **≈54 KB internal + ≈45 KB PSRAM**, not ≈99 KB internal. Three knock-on
corrections the spec needs regardless of the decision below:

1. **A-1's prose** — "≈99 KB of internal SRAM concurrently" is ~45 KB too high.
2. **Rejected alternatives, "Stream the read"** — *"would drop the load peak from ≈99 KB to the pool
   alone (≈54 KB)"* is wrong in the direction that **strengthens** the rejection: the peak is already
   the pool alone in internal SRAM, so streaming buys essentially nothing there. Keep the rejection,
   fix the reason.
3. **Human-tester item 5** — *"`ESP.getFreeHeap()` before and after opening a book with 64 bookmarks
   — research §5's ≈35 KB transient figure"*. `EspClass::getFreeHeap` is
   `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` (`framework-arduinoespressif32/cores/esp32/Esp.cpp:162-164`),
   internal only. As written the test asks the human to confirm a number the instrument cannot see,
   and a smaller reading will read as "the model is wrong" when it is only mis-attributed. The step
   must pair `ESP.getFreeHeap()` with `ESP.getFreePsram()` (`Esp.cpp:182-187`).

**Problem — the scope decision.** With the premise corrected, `MAX_BOOKMARKS = 64` costs ≈16 KB of
internal SRAM at the cap versus ≈54 KB at 219 records, so the RAM case for *64 specifically* is much
weaker than A-2 argues. And neither issue #28 nor the brief's five acceptance criteria asks for a
record cap at all — the brief asks for a measured budget, an atomic write, a surfaced refusal and a
load latch. A-2 adds a hard, user-visible product limit on a device whose central book is a single
EPUB containing all 66 Bible books (`LauncherActivity.cpp:94-98`), where the research itself argues
*"219 bookmarks in 'the Bible' is one per 140 chapters, which a study device is built to invite."*
64 is roughly one bookmark per 18 Bible books. The spec anticipates this — *"**This is the number
most worth arguing about.**"* — which is precisely why it cannot be settled by a reviewer.

**Concrete fix.** Correct the three factual points above, then put the cap to the human with the
corrected numbers: 64 → ≈13 KB JSON / ≈16 KB internal; 128 → ≈26 KB / ≈32 KB; 256 → ≈53 KB JSON,
which exceeds `SAVE_BYTE_BUDGET` and is therefore the hard upper bound the byte guard already
imposes. If the answer is "no new cap", A-2 collapses into A-1 and the byte guard alone does the job
(at which point BLOCKER 1's remedy matters more, not less).

---

## MAJOR 3 — the `EpubReaderBookmarksActivity::onEnter` bail copies `PassageSelectActivity`'s shape but not its result contract, and aborts the firmware

**Claim.** §EpubReaderBookmarksActivity: *"`onEnter` (`:33`) — on `Failed`, show the message and
`finish()`, mirroring `PassageSelectActivity.cpp:29-33`."*

**Problem.** The two activities are launched with different result handlers, and the difference is
load-bearing. `PassageSelectActivity` is launched at `EpubReaderActivity.cpp:338-343` with
`[this](const ActivityResult&) { requestUpdate(); }` — the result is ignored, so finishing without
calling `setResult` is safe. `EpubReaderBookmarksActivity` is launched at
`EpubReaderActivity.cpp:871-873` with `progressChangeResultHandler`, which does:

```cpp
// src/activities/reader/EpubReaderActivity.cpp:690-693
if (result.isCancelled) {
  openReaderMenu();
} else {
  const auto& sync = std::get<ProgressChangeResult>(result.data);
```

`Activity::result` is a default-constructed `ActivityResult` (`Activity.cpp:22`, `ActivityResult.h:92-94`):
`isCancelled == false`, `data == std::monostate`. `ActivityManager::loop` moves it out at
`ActivityManager.cpp:105` and calls the handler at `:129` regardless. So a `finish()` with no
`setResult` takes the `else` branch and `std::get<ProgressChangeResult>` on a `std::monostate`
variant → `std::bad_variant_access` → with `-fno-exceptions` (`platformio.ini:72`), `std::terminate`
→ abort. Every existing exit from this activity sets a result precisely for this reason
(`EpubReaderBookmarksActivity.cpp:91-92`, `:134-137`, `:185-188`); the spec's new exit is the only
one that would not.

`finish()` from inside `onEnter` is otherwise fine — `ActivityManager.cpp:162-165` runs `onEnter`
then `continue`s back into the `while (pendingAction != None)` loop, which processes the pop.

**Reachability is low but not zero.** `EpubReaderActivity.cpp:283` gates the Bookmarks menu entry on
`!cachedBookmarks.empty()`, and a `Failed` reader-side load leaves that vector empty, so in the
common case the list cannot be opened at all. The window is a transient SD failure between the
reader's load at `:1747` and the list's own independent load at `EpubReaderBookmarksActivity.cpp:33`.
Consequence is a hard panic, fix is one line, so it should not ship either way.

**Concrete fix.** In §EpubReaderBookmarksActivity, spell the bail out as:

```cpp
ActivityResult result;
result.isCancelled = true;
setResult(std::move(result));
finish();
```

and drop the "mirroring `PassageSelectActivity.cpp:29-33`" claim, or qualify it — that donor's
handler discards the result, this one destructures it. While there, note that the activity does not
currently include `ReaderUtils.h` (`grep -n showMessage src/activities/reader/EpubReaderBookmarksActivity.cpp`
→ no hits); the toast needs `ReaderUtils::showMessage` (`ReaderUtils.h:233`, which is
`GUI.drawPopup` + `renderer.displayBuffer()`) or an equivalent, and the spec should say so.

---

## MINOR 4 — A-4's prose and A-4's code disagree about `"v": 0`

**Claim.** A-4 heading: *"A format version is added (`"v": 1`); **absent or `0` is read as 1**, not
refused."* The code immediately below it:

```cpp
const int version = doc["v"] | BookmarkDoc::FORMAT_VERSION;   // absent == legacy v1
if (version <= 0 || version > BookmarkDoc::FORMAT_VERSION) return false;
```

**Problem.** ArduinoJson's `operator|` returns the default only when the variant is unbound or not
convertible. A present `"v": 0` *is* convertible, so `version == 0`, the guard fires, `fromJson`
returns false, `load` reports `Failed` and the session latches saving off — the opposite of what the
sentence promises. Only *absent* is read as 1. (A stray `"v": "1"` is also read as 1 by the same
operator, which is the lenient behaviour the prose wants but not the one it describes.)

**Concrete fix.** Either drop "or `0`" from A-4 and keep the guard as written (recommended — it
matches `PassageDoc.cpp:101-103` and nothing in the field emits `"v": 0`), or change the guard to
`if (version > BookmarkDoc::FORMAT_VERSION) return false;` and say so. Test 3 in §Testing should
then pin whichever is chosen, alongside the existing `"v": 2` case.

---

## MINOR 5 — `MAX_BOOKMARKS` is not "enforced the same way" as `HighlightDoc`'s cap; it is a guard in one activity

**Claim.** A-2: *"Mirrors `HighlightDoc::MAX_HIGHLIGHTS = 400` (`HighlightDoc.h:23`), enforced the
same way: `HighlightDoc::addHighlight` refuses at the cap and returns false (`HighlightDoc.cpp:56`)."*

**Problem.** `HighlightDoc` is a class that *owns* `highlights_`, so its cap is an invariant no
caller can bypass. The spec's `BookmarkDoc` is not that shape: §Control flow has
`BookmarkDoc::toJson(bookmarks, json)` and `BookmarkDoc::fromJson` filling a caller-owned
`std::vector<BookmarkEntry>&`, and the Error-handling table says the cap is *"never reached — refused
at the call site"* — i.e. one `if` in `EpubReaderActivity::addBookmark()` (§Reader step 3). Nothing
between the activity and `BookmarkFile::save` enforces it, so `MAX_BOOKMARKS` is advisory, and a
second writer added later inherits none of it. This is a defensible choice (moving
`BookmarkEntry` ownership into a doc class is the relocation the Non-goals section rightly rejects),
but "enforced the same way" is not true and an implementer reading it will look for the wrong shape.

**Concrete fix.** Say it plainly in A-2: the constant lives on `BookmarkDoc` for the host test, but
the cap is a call-site precondition in `EpubReaderActivity::addBookmark()`, and any future writer
must repeat it. One sentence.

---

## MINOR 6 — §Testing item 5 inverts its donor's point

**Claim.** *"The genuine worst case … is measured and recorded, mirroring `HighlightDocTest.cpp:167-193`,
which asserts its worst case does **not** fit precisely so the byte guard is understood as the safety
mechanism."*

**Problem.** `HighlightDocTest.cpp:190-193` asserts `EXPECT_GT(bytes, 50000u)` with the comment *"if
this ever fits, the caps changed — revisit the guard's necessity."* For bookmarks at
`MAX_BOOKMARKS = 64` the worst case comfortably **does** fit: a 16-deep xpath
(`MAX_XPATH_DEPTH`, `ProgressMapper.cpp:141`) with four-digit indices is ~290 B, a fully
JSON-escaped 72-byte summary is ≤144 B, plus ~60 B of keys and scalars → ≲500 B/record × 64 ≈ 32 KB,
against a 45,000-byte budget. By the donor test's own logic that means the count cap, not the byte
guard, is the binding constraint for files this build writes — which sits awkwardly beside the spec's
quotation of `HighlightDoc.h:15-19` (*"Bytes, not entry counts, are the safety invariant"*). The byte
guard is still needed, but only for the legacy over-count sets of BLOCKER 1, and the spec should say
that is its job.

**Concrete fix.** Reword item 5 to "records the worst case and asserts it fits, with the margin, so a
new field on `BookmarkEntry` or a raised cap fails here first", and add one sentence to §Budget
noting the byte guard's remaining role is legacy files, not files this build writes.

---

## MINOR 7 — citation drift

Small, but this spec's whole authority is its citations, and the implementer will follow them.

| Spec says | Actual |
|---|---|
| §Reader: *"Current shape: … (`:1771-1803`)"* | the toggle begins at `EpubReaderActivity.cpp:1765` (`bookmarkCountBeforeToggle`); `:1771` is mid-`erase` |
| A-1: *"`SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET` … same value and same reason as `src/util/HighlightFile.h:38-42`"* | `HighlightFile.h:42` hardcodes `45000`; it does not reference `persist::DEFAULT_SAVE_BUDGET`. `TagPaletteFile.cpp:70` is the one that does |
| A-5: *"(`PassageDoc.cpp:128-137`)"* / *"as `PassageDoc.cpp:120-127`"* | the budget return is `PassageDoc.cpp:131`; the never-drop-on-load comment is `:125-128` |
| §Testing: *"`test/highlight_file/CMakeLists.txt:11-14`"* | the `target_include_directories` block with `${REPO_ROOT}/src` is `:9-12` |
| §Architecture: *"`highlightLoadAction()` (`src/util/HighlightFileAction.h:34-46`)"* | the function spans `:34-47` |
| Error table: *"Unreadable / unparseable → … a refusal toast on every attempt"* | after `Failed`, `cachedBookmarks` is empty, so `EpubReaderActivity.cpp:283` hides the Bookmarks menu entry entirely — the only remaining attempt surface is Toggle Bookmark |

---

## Verified and not a finding

- `BookmarkDoc` in `src/util/` is host-buildable as described: `src/BookmarkEntry.h` pulls only
  `<cstdint>`/`<string>`, and `test/highlight_file/CMakeLists.txt:9-12` is a live precedent for
  putting `${REPO_ROOT}/src` on a test's include path. `test/CMakeLists.txt:67-68` is where the new
  `add_subdirectory` goes.
- Not bounding `xpath` on the load path is safe against the one parser that consumes it:
  `parseXPathSteps` rejects any element name that would overflow `XPathStep::tag[12]`
  (`ProgressMapper.cpp:176`, `if (nameLen == 0 || nameLen >= sizeof(step.tag)) return 0;`).
- The `.tmp` arm really is required by `writeDocToFileAtomic`: `PersistableStore.cpp:38-39` removes
  the destination before the rename.
- A-7's reasoning holds. `STR_HIGHLIGHTS_TOO_LARGE` ("This book already has too many highlights",
  `english.yaml:390`) genuinely does not fit a bookmark toggle, it is genuinely unreferenced, and
  routing the three refusals through one `bookmarkToastString()` pointed at
  `STR_ERROR_GENERAL_FAILURE` (`english.yaml:201`) satisfies criterion 3 without touching the YAML
  the brief reserves. Shipping a vague message beats shipping a silent refusal.
- The Non-goals are each backed: `docs/file-formats.md` mentions none of the annotation stores;
  relocating `BookmarkEntry` would indeed touch `EpubReaderActivity.h:13` and `ActivityResult.h`.

---

## What I could not verify

- Nothing was built or run. No `pio run`, and no host test suite was configured — the spec adds no
  code, so there was nothing to compile.
- The internal/PSRAM split in BLOCKER 2 is derived from the framework config, the IDF allocator
  source and the ArduinoJson pool constant, not measured on hardware. The device confirmation is the
  human's, and it needs `ESP.getFreePsram()` alongside `ESP.getFreeHeap()` to be meaningful.
- Whether a 219-record bookmark file exists in the field at all is unknown; BLOCKER 1 is an argument
  about reachability from the research's own numbers, not an observation.

VERDICT: BLOCKER
BLOCKERS: 2
MAJORS: 1
