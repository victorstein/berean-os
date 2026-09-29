Tier: heavy

# Issue #188 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-29-issue-188-design.md` against issue #188
(`gh issue view 188 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-29-issue-188-research.md`. Base `7ff77cce` (the code is unchanged
from `a4e2288d`). Every cite below was read for this review. The real Spanish NWT is the unpacked
`nwt_S.epub` in the session scratchpad (`.../scratchpad/nwt/OEBPS/`).

The spec's direction holds up. `UnitTextScanner` as the single producer, the capture filter that
leaves counting alone, the v4 `"h"` flag, the "whole or skip" sleep ladder and the on-open repair
are all sound. I confirmed the research's NWT observations: footnotes in `<aside>` after the last
verse, `<a epub:type="noteref">`, `<strong><sup>N</sup></strong>` followed by `e2 80 af`, and
`<span class="w_ch">`. D1 is settled and this review does not reopen it. What follows is where the
decision and the repair are **applied** incompletely.

Summary: 0 BLOCKER, 6 MAJOR, 6 MINOR. None of the MAJORs reverses a decision, changes scope or needs
the owner's judgment. Each has a concrete fix that can go straight into the spec.

---

## MAJOR

### M1 — D1 is only half wired: the paths that actually copy the texts still use the default allocator

**Claim.** A24, "Transient copies": `PassageFile::load`/`save` and `offerFile` build their
`JsonDocument` on a PSRAM `ArduinoJson::Allocator`, "so parsing and serialising the whole texts does
not touch internal SRAM either". The firmware "installs" a PSRAM allocator for `PassageText`.

**Problem.** Three allocation paths are not covered, and one of them runs on every load and edit:

1. **`PassageDoc::measureBytes()` builds its own default `JsonDocument` and copies every text into
   it.** It runs inside `add` (`PassageDoc.cpp:129`), `linkPassages` (`:174`) and at the end of
   `fromJson` (`:284`), and the new `setWholeText` will call it too (A11: "`PassageDoc.cpp:129-132` is
   the pattern"). ArduinoJson copies each string into its own pool block through
   `DefaultAllocator::allocate` → `malloc` (`ArduinoJson/Memory/Allocator.hpp`). At
   `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (framework `sdkconfig:2154`, not overridden by
   `platformio.ini:94-114`) every per-text block below 4 KB goes to internal SRAM. So every load,
   capture, link and repaired row briefly holds a second copy of all texts (about 44 KB by A24's own
   estimate) in internal SRAM. During `fromJson` that copy sits alongside the load document. The
   repair calls it once per row, which also makes the pass O(n²) in serialisation against its
   3-second budget.
2. **`PassageFile::load` cannot choose its document's allocator.** It goes through
   `PersistableStoreBase::loadAdopting`, which declares `JsonDocument doc;` itself
   (`lib/Serialization/PersistableStore.cpp:217`) and passes only a `DocReader`/`DocAcceptor`
   (`PersistableStore.h:95,99,115`). Routing it to PSRAM means changing `lib/Serialization`. That
   library is shared by every adopting store, and it appears nowhere in the Architecture section.
3. **Nothing says how `fromJson`, the main producer of resident texts, gets the PSRAM allocator.**
   `Allocator.h` (the stated model) passes the allocator **per object**
   (`AllocatedBuffer(const BuildAllocator, size_t)`, `lib/BibleSearch/BibleSearch/Allocator.h:27`).
   `TaggedPassage`s are built by value in `PassageDoc::fromJson` (`PassageDoc.cpp:258`), in
   `StudyStore::addPassage` (`StudyStore.cpp:282`) and in `planMigration`
   (`MigrationPlanner.cpp:34`). If `PassageText` falls back to its host default (malloc) at any of
   these, the resident texts land in internal SRAM, which undoes D1 silently. Device check 7 would
   not catch the transient cases, because it samples only after each operation has returned.

**Fix.** In A24:

- (a) `PassageDoc` holds the `ArduinoJson::Allocator*` and the `PassageText` allocator, both injected
  once (host default: malloc). `measureBytes` builds its document on that allocator, or computes the
  size without a document. `fromJson` and `add` construct every `PassageText` with it.
- (b) Add a `loadAdopting` overload that takes an `ArduinoJson::Allocator*`, list
  `lib/Serialization/PersistableStore.{h,cpp}` in the Architecture section, and keep existing callers
  unchanged.
- (c) Remove the "does not touch internal SRAM" sentence unless (a) and (b) make it true.
- (d) Add to device check 7 a `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` reading across
  the add and the load, so a transient low shows up.

### M2 — Guardrail 2 ("refuse, never truncate") has no mechanism on two paths, and one of them is a silent truncated write

**Claim.** A24 guardrail 2: every allocation is null-checked, and a PSRAM OOM refuses the load, save
or add. Error table: "Refused, never truncated".

**Problem.**

1. **Save.** ArduinoJson 7 does not fail loudly when its allocator returns null during
   `toJson`/`add`. It drops the value and sets `JsonDocument::overflowed()`. `PassageFile::save`
   builds the document, checks only `measureJson(json) > SAVE_BYTE_BUDGET` and writes it
   (`src/study/PassageFile.cpp:27-36`). Nothing in the repo checks `overflowed()`
   (`grep -rn overflowed src lib` finds only `UiAppHost.cpp:24`, which is unrelated). So on a PSRAM
   OOM the new allocator produces a **smaller** document that passes the budget check and is written
   atomically over the good file. That is the exact failure CLAUDE.md's storage discipline exists to
   prevent. `measureBytes` has the same blind spot: an overflowed measure under-reports, so `add`
   would accept.
2. **Copies.** `PassageText` is specified as **copyable** (A24, "Mechanism"). A copy constructor
   cannot return false, so a failed copy can only yield an empty or partial buffer. Copies happen in
   `removePassage`'s backup (`StudyStore.cpp:305`), in the repair's per-row backups (A18), and in
   vector growth unless the move is `noexcept`. A backup that comes back empty, restored onto a row
   with `whole == true`, is written as `"h": true` with no `"w"`. Under A9 that row makes **every**
   build refuse the whole file on load ("a row with `"h": true` and an empty or non-string `"w"`
   refuses the load"). Saving then latches off, and the user's whole Bible passage file is locked
   until someone edits the card by hand.

**Fix.**

- `PassageFile::save` and `measureBytes` treat `json.overflowed()` as a refusal (`LOG_ERR`, return
  `TooLarge`/`WriteFailed`, or `SIZE_MAX` from `measureBytes`).
- `PassageText` becomes move-only (with a `noexcept` move) plus an explicit
  `bool copyFrom(const PassageText&)` that reports failure. Callers that back up a row check it and
  abort the edit before mutating.
- `toJson` asserts its own invariant: it writes `"h"` only when the text is non-empty, and it never
  writes a whole row whose text is empty.
- Add host tests: the allocator fails mid-`toJson`, and the save is refused, not written. A failed
  backup copy leaves the document unchanged.

### M3 — The repair rewrites passages from whatever edition is open, including another language, because it skips the fingerprint check

**Claim.** A19 "Rebuilt": the span resolves and extraction is non-empty, so `"w"` and `"x"` are
replaced. The Verse search finds rows "in another spine" (`locateUnit`).

**Problem.** The Bible's pubkey is **language-free** (`"bible"`, `PubKey.h:37`; `Unit.h:9-15`: "Salmos
119:145 and Psalm 119:145 are the same verse"). Suppose the owner opens an English or other-language
NWT, or a re-downloaded edition whose text changed, while any row is still unrepaired. The repair
then resolves each Verse address in that edition and replaces the Spanish `"w"` **and** `"x"` (A8)
with that edition's text. Once `"h"` is set the row is never repaired again (A18), so the change is
permanent. The list label changes language while `"r"` stays "Gálatas 5:4" (Non-goals).
Paragraph/DocumentOffset rows in a re-downloaded meeting publication have the same problem: their
offsets now point at different words.

The store already has a rule for exactly this case, on the paint path: a fingerprint mismatch means
"the text this was attached to is not the text that is there", so it does nothing
(`StudyStore.cpp:258-264`). The repair is the one path that writes, and it has no such guard. That
conflicts with the issue's "must never downgrade what is stored".

**Fix.** In A19, when `fingerprint.length != 0` and the current start unit's fingerprint
(`fingerprintOf(units_->unitText(spine, start))`, the same call as `StudyStore.cpp:262`) differs,
the outcome is **Unresolvable**: the row is left byte-identical and logged as
`fingerprint mismatch`. Add it to the `test/text_repair/` cases. Capture is unaffected, because a
new passage's fingerprint is taken from the text being extracted.

### M4 — The repair can stall forever behind one slow unresolvable row

**Claim.** A18 visits rows in stored order, skips `whole` rows, stops after 3000 ms of wall time, and
"the rest wait for the next open". A19 retries an unresolvable row on the next open.

**Problem.** An unresolvable row stays `!whole`, so on every open it comes up first again. Resolving a
Verse row whose hint is stale walks `spineIndicesForBook` with `unitsFor` on each candidate, which
can mean up to 150 documents, "each possibly a fresh index build" (`StudyStore.cpp:189-195`). An
extraction that fails does so after a full stream and possibly an inflate
(`SpineHtmlStream.h:21-33`). Because the budget is checked between rows, a single row that takes
more than 3 s by itself, or a few slow ones at the front, use up every open's budget. Every repairable
row stored after them then **never** gets its turn. "Bounded" holds, but the progress the spec
promises ("the rest wait for the next open") does not.

**Fix.** Make progress independent of order. Any one of these works:

- keep a session-scoped "attempted" set, plus a persisted-free rotating start cursor that begins the
  next pass after the last row visited;
- or run two phases: rows that resolve at their spine hint first, then book-search rows with a
  separate cap.

Add a host test to `test/text_repair/`: with an injected slow or unresolvable first row, a later
row is still repaired within N passes.

### M5 — A4's word-end extension counts codepoints in text the filter has already changed

**Claim.** A4: for Paragraph and DocumentOffset passages the span is
`[documentOffsetOf(start), documentOffsetOf(end))`, and the extracted text is extended to the end of
the word, by the pure `extendToWordEnd(text, codepoints)` (Architecture, `PassageSpan.h`). A5/A7:
the filter drops `<aside>` and noteref text, and appends an uncounted U+0020 at every block close.

**Problem.** Extending past `to` means capturing beyond `to` and then cutting at a codepoint
position. Once the filter is on, the captured text's codepoint count no longer equals the offset
distance. A5 says so itself ("Counting is untouched … The filter only decides what is appended"), and
A7 adds characters that were never counted. The meeting publications are exactly the Paragraph
documents, and they carry noterefs too (A5 applies the noteref and aside rules in every document).
So a post-hoc `extendToWordEnd(text, codepoints)` cuts at the wrong place: too late after a skipped
footnote marker, too early after an inserted block space. `end` is one codepoint into the last word
(`PassageSelectActivity.cpp:376-381`), so the cut is visible.

**Fix.** Do the extension inside `UnitTextScanner`, in document offsets. Give the filtered range
capture a `to` plus an "extend to word end" flag: after `counter.offset >= to`, keep capturing
visible, non-skipped codepoints until the first U+0020 or the unit end (`unitEndOffset`). Drop
`extendToWordEnd` from `PassageSpan.h`. Add a `test/unit_text/` case with a noteref and a
`</p><p>` inside a Paragraph range.

### M6 — Verse snapping is keyed on the start unit's kind, so some Bible passages are never snapped; and `end - 1` has no clamp

**Claim.** A2: "For a passage whose start is a `Verse` unit" the span snaps. Everything else goes
through A4.

**Problem.**

1. In a Verse document the start can resolve to `DocumentOffset`. `resolve` returns that kind for any
   offset before the first anchor (`UnitAnchors.cpp:88-94`). That covers the visible nav line
   `Gálatas 5 : 1 - 26` and Psalm superscriptions, for example `1001061123-split3.xhtml`: "Salmo de
   David, cuando huía de su hijo Absalón." sits before `chapter3_verse1`. Rows migrated without the
   EPUB are all `DocumentOffset` with `pendingUpgrade` (`MigrationPlanner.cpp:48-53`). All of these
   take the A4 path and store an end that stops mid-verse. That breaks "always the complete verse(s)
   its selection touches".
2. `lastVerse = resolve(documentOffsetOf(end) - 1)` assumes `end > start`. `fromJson` reads a
   missing `"e"` as the start (`PassageDoc.cpp:260`, `value_or(*start)`), and legacy `end` came
   through migration unconverted (`MigrationPlanner.cpp:58`). With `end == start`, `- 1` lands in the
   **previous** verse, or before the first anchor, or underflows at 0. The result is an empty or
   inverted span, which A19 then logs as Unresolvable on every open (see M4).

**Fix.** In A2:

- Snap by the **document's** kind (`units.kind == Verse`), not the start unit's. The start boundary
  is `anchor(start)` when the start is in a verse, or the start offset when it precedes the first
  anchor. The end is always `unitEndOffset` of the verse holding the last selected codepoint.
- Clamp that codepoint index to at least the start unit's anchor index.
- Add `test/passage_span/` cases: a start before the first anchor, `end == start`, and a
  `pendingUpgrade`-shaped `DocumentOffset` row in a Verse document.

---

## MINOR

### m1 — The capture filter keeps 249 non-verse paragraphs inside verse spans

A5 claims "The captured text leaves out what is not the verse". A survey of the real NWT's Bible
chapter documents, after each document's first verse anchor and before `groupFootnote`, finds
paragraphs of class `ss`/`sd` with no verse anchor. They are counted into the preceding verse's
unit:

- 162 in Salmos, 63 in Lamentaciones, 22 in Proverbios. These are the acrostic headings; for example
  Ps 119:8's unit ends "No me dejes completamente abandonado." and is followed by
  `<p class="p2731 ss">ב <em>[bet]</em></p>` (`1001061123-split119.xhtml`).
- 1 in Habacuc: "Al director; para mis instrumentos de cuerda."
- 1 in Malaquías: "(Aquí termina la traducción de las Escrituras Hebreoarameas…)", at the end of
  4:6.

**Fix:** in a Verse document, also skip `<p>` whose class list contains `ss` or `sd`. Add a fixture
line for the acrostic heading.

### m2 — The "suspect start" test will mostly flag rows that are fine

A19 compares the first 32 bytes of `utf8SafeSummary("x")` against the rebuilt text. The rebuilt text
drops verse numbers and noteref `*` and keeps U+00A0 (A5/A6). Old snippets differ on all three
points:

- Device-made snippets joined laid-out word boxes, and the reader lays `<sup>` out as words
  (`ChapterHtmlSlimParser.cpp:1106-1120`). So they carry "4" and "*", and a plain space where the
  source has U+00A0 (`UnitText.h:9-14`).
- The owner's migrated rows came from offline slicing, which keeps U+202F and U+00A0 (project memory
  "highlight-passage-extraction-differs-offline-vs-device").
- `utf8SafeSummary` folds ASCII whitespace only (`Utf8.cpp:185-197`).

Any snippet with a verse number, `*` or no-break space in its first 32 bytes is flagged, which buries
the real known-limit rows. **Fix:** before the containment test, normalise both sides the same way:
fold U+00A0, U+202F and U+2007/2009/200A/2002/2003 to U+0020, drop tokens made only of digits or
`*`, and collapse spaces. Add a test that a verse-number-led snippet is not flagged.

### m3 — The on-open repair's visible cost and its gaps are not stated

- **Opening stalls.** `loadBook` runs inside `ReaderActivity::onEnter` before the first
  `requestUpdate` (`ReaderActivity.cpp:53-61`). Every Bible open stalls for up to 3 s, plus one
  row's overrun and inflate popups, until every row is repaired.
- **Some rows are never repaired.** Rows of publications that are not reopened (past weeks' meeting
  publications, deleted EPUBs) stay without `"h"`. Under A10 they leave the sleep rotation for good.
- **The first sleeps show nothing studied.** Right after upgrade, before the Bible is opened, no row
  has `"h"`, so sleep falls back to the ordinary screen (`SleepActivity.cpp:549-550`).
- **The acceptance criterion needs more than the Bible.** "every passage on the owner's card"
  therefore holds only after every publication is opened. The "Bible holds almost all passages"
  basis in A17 is not cited.

**Fix:** state these in A17/A18. Log the pass duration and the rows left over. Log at sleep how many
rows were skipped for lacking `"h"`. Extend device check 3 to open each publication that has
passages. Consider running the pass after the first page render.

### m4 — Files touched but not listed

- Deleting `PassageLabel.h` and `test/passage_label/` also means removing
  `add_subdirectory(passage_label)` from `test/CMakeLists.txt:127`, a shared file per
  `.claude/agents/data-dev.md:22-27`. The Testing section mentions only new suites.
- `MigrationRunner.cpp:356` (`passages.add(*planned.passage)`) and `MigrationPlanner.cpp:34-62` build
  `TaggedPassage` rows. They must compile against `PassageText`, must never set `whole`, and have to
  go through the v4 `add` with a non-whole row. Neither appears in the Architecture section, and no
  test covers `add` of a non-whole row under v4.

### m5 — A22's "extraction reads SD, not the zip" is not guaranteed

`verseReference` tolerates a missing cache (`if (!section.hasHtmlCache()) return {};`,
`PassageSelectActivity.cpp:200`). `UnitIndexCache::unitText`, the model for `rangeText`, streams
with the default `WhenMissing::Inflate` (`UnitIndexCache.cpp:396`, `SpineHtmlStream.h:40-41`). The
behaviour is fine, because capture already calls `unitText` twice today (`StudyStore.cpp:290-292`).
The claim is what is wrong. **Fix:** say "normally SD; inflates with the existing popup when the
cache is gone".

### m6 — A16's `std::string` candidate conflicts with guardrail 2, and the fit gate's cost is not measured

- `Candidate::text` as `std::string` aborts on OOM under `-fno-exceptions`. That contradicts "every
  allocation is null-checked". It is acceptable only because `FIT_PREFILTER_BYTES` bounds it to
  4 KB, and the spec should say so.
- The gate runs a full greedy wrap per `"h"` row. `wrap` re-measures the growing line for each word
  (`StudySleepFit.h:67-86`), so every row costs O(words × line length) of `getTextWidth` on the way
  to sleep.

**Fix:** state the bound. Add the scan's elapsed milliseconds to the existing `Study pick` log line
(`StudySleepScreen.cpp:385-387`), and put it in device check 6.

---

## Checked and sound

- **Real NWT markup (A5).** Verified: `<strong><sup>4</sup></strong>` followed by `e2 80 af`;
  `<span class="w_ch"><strong>5</strong> </span>` (ASCII space); 10,338 noterefs and 10,338
  `<aside epub:type="footnote">` across the book; the footnotes follow `chapter5_verse26`. In the
  chapter documents, the only non-numeric `<sup>` are Spanish ordinals in appendix tables
  (`1001061207`–`1212`).
- **Format rules (A9).** `isKnownFormatVersion(4, 3)` is false (`FormatVersion.h:14-15`), so every
  1.19.x build refuses v4. The lowest-version rule (`PassageDoc.cpp:106-110`) extends cleanly.
- **Memory basis (A24).** `ESP.getFreeHeap()` is internal-only
  (`framework-arduinoespressif32/cores/esp32/Esp.cpp:163-165`), which matches the stated basis.
- **Sleep font (A13).** The Ubuntu 10 floor covers the Latin-1, Latin Extended-A and Hebrew ranges
  the text uses (`ubuntu_10_regular.h:2525-2540`).
- **Selection scope (A2).** Selections stay in one spine document (`PassageSelectActivity.cpp:131-145`).
- **Sleep fallback.** `render` returning false falls back to the default sleep screen
  (`SleepActivity.cpp:549-550`).

VERDICT: CLEAR
