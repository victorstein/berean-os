Tier: standard

# Issue #206 spec review 0: typed Bible references in verse search

Reviewed: `docs/superpowers/specs/2026-09-30-issue-206-design.md`, checked against issue #206
(`gh issue view 206 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-30-issue-206-research.md`, on branch
`feature/206-typed-references` (HEAD `d2e0b871`).

## What was verified and holds

- **Canonical order of the verse table (the resolver's binary search depends on it).** The
  builder does not check it (`IndexBuilder.cpp:135-155`), so I checked the real publications.
  Both `nwt_E.epub` and `nwt_S.epub`, unzipped from an earlier session's scratchpad, have exactly
  1,189 verse documents. Each one holds a single chapter. Within every document the
  `chapterN_verseM` ids are sorted with no duplicates, and there are 66 book boundaries where
  the chapter resets to 1. `spineIndicesForBook` yields ascending spines (`UnitIndexCache.cpp:366-385`),
  so a lower-bound search by (book, chapter, verse) is sound. A10 ("each chapter is its own spine
  document") also holds.
- **A5 against the real names.** I simulated A4/A5 in Python over the real TOC-joined names and
  nav abbreviations of both EPUBs. No normalised candidate belongs to two books, so the
  "more than one book exact" branch never fires on real data. The issue's examples all resolve
  correctly: `Isa 40:31` (EN, exact via `Isa.`; ES, prefix of `Isaías`), `Gén 1` (both),
  `Juan 3:16` (ES), `Salmo 23`, `Gene 1`, `Cant 2`, `1Juan 1`, `Mat 24:14`. Common word queries
  with a number (`amor 3`, `Dios 3`, `ley 5`, `paz 1`, `vida 3`) produce no match. The English
  abbreviation set, which the research left unverified, matches the spec's fixture shape: `Gen.`,
  `Isa.`, `John`, `1 Chron.`, `Song of Sol.`. The longest abbreviation is 12 B (`Song of Sol.`),
  so `ABBREV_BYTES = 16` never truncates a real one. Neither publication has NBSP in a name or
  label.
- **Hand-off contract.** An empty `offsetJump` lands on page 0 of the spine. `navigateTo` only
  uses the offset when present (`EpubReaderActivity.cpp:1679-1691`). `ChapterResult` is unchanged
  (`ActivityResult.h:30-37`).
- **Placement and cost claims.** `BibleBookNameTable` is used only by the two activities
  (`BibleSearchActivity.h:110`, `BibleNavigationActivity.h:58`). The grid calls `joinToc` directly
  (`BibleNavigationActivity.cpp:82`), so A14 does not disturb it. The PSRAM routing claim matches
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` in the pinned framework sdkconfig. `:` is on the
  symbol layer (`FreeInkUI.cpp:168-170`). `test/CMakeLists.txt:102` is `add_subdirectory(ui_layout)`,
  so the new test touches no shared file. The mockup artifact linked from the issue does not draw
  the Go-to row, so "ordinary `ListItem`, no subtitle" contradicts no design.

## Findings

### MAJOR 1: The row-offset site list misses `ensureRows`, so the top visible hit renders blank whenever a Go-to row is present and the list is scrolled

- **Claim.** Spec §3: "`firstResultRow()` … replaces the literal `1` / `- 1` row arithmetic in
  `listCount()` (`:542-544`), `activateIndex()` (`:668-676`), `buildResults()` (`:885-899`) and
  `nav.reset` (`:532`)."
- **Problem.** `ensureRows` hard-codes the same "row 0 is Edit search, row N is result N-1"
  mapping, and the spec does not list it. With a Go-to row, list row `t` shows result `t - 2`.
  But `ensureRows(t, v)` loads results starting at `t - 1`. `buildResults` then finds result `t - 2`
  outside the cache and draws `label = ""`. The `covered` check keeps passing, so that row is never
  loaded. The list is correct only at `top == 0`. After any scroll or page, the top hit row stays
  blank, and at the end of the list the last visible hit is also dropped from the loaded window.
  Every `loop()`, `moveTo` and `scrollPage` call goes through this function.
- **Evidence.** `BibleSearchActivity.cpp:549`: `const int first = std::max(listTop, 1) - 1;` and
  `:551`: `const int end = std::min({listTop + std::max(visibleRows, 1) - 1, total, first + ROW_CACHE});`.
  The consumers are `:161-162` (`loop`), `:618-630` (`moveTo`), `:632-646` (`scrollPage`) and
  `:538` (`runSearch`). `buildResults` blanks rows outside the cache at `:892-899`.
- **Fix.** Add `ensureRows` to the list. It should use `first = std::max(listTop, firstResultRow()) - firstResultRow()`
  and `end = std::min({listTop + visibleRows - firstResultRow(), total, first + ROW_CACHE})`,
  keeping the existing `std::max(visibleRows, 1)` guard. Also state the rule outright: every
  row↔result conversion goes through `firstResultRow()`, and a grep for `- 1`/`, 1)` in the file
  is part of the plan. The device test list should gain a step: "`Gen 1` (many hits) → scroll one
  page → the top hit row shows its reference and snippet."

### MINOR 1: The resolver cost claim is wrong: `verse()` does not go through the 4 KB page

- **Claim.** Spec §1 Resolving: "About 15 probes of 9 bytes each … served by the reader's
  existing 4 KB page (`IndexReader.h:36-38`) — no allocation." Research line 68 says the same.
- **Problem.** `IndexReader::verse` calls `readAt`, which goes straight to the `ByteSource`. The
  page serves only `byteAt`, which is used for postings decoding. On the device, each probe is a
  separate `seek` plus `read` of 9 bytes under the storage mutex. The conclusion ("no allocation")
  still holds, but the mechanism given for the cost is false, and CLAUDE.md requires the
  mechanism to be right.
- **Evidence.** `IndexReader.cpp:95-103` (`verse` → `readAt`), `:61-64` (`readAt` →
  `source_.readAt`), `:77-92` (the page is used only in `byteAt`), and `BibleSearchStore.cpp:29-38`
  (`readFileAt` does `seek` + `read` per call).
- **Fix.** Reword the claim: "≤ ~45 direct 9-byte `readAt`s (seek + read through `HalFile`; SdFat's
  sector cache absorbs neighbouring probes), no allocation." Also ask the implementer to log
  the resolve time beside the existing `LOG_INF` query timing (`:521-522`).

### MINOR 2: The resolver's "false only on an I/O failure" collides with `verse()` returning false for an out-of-range index

- **Claim.** Spec §1: `resolveTypedReference … // false only on an I/O failure`. The error table
  says an I/O failure gives `LOG_ERR`.
- **Problem.** `verse(n)` returns false when `n >= verseCount` as well as on a read failure. A
  lower bound for a key past the last entry lands at `verseCount`. That happens for any reference
  past Revelation's last verse (`Apoc 23`, `Rev 22:22`). If the implementation reads the entry at
  the lower bound, it reports an I/O failure and logs `LOG_ERR` for what is really A9's "not in
  the index". The row outcome is the same, but the log and the contract are wrong.
- **Evidence.** `IndexReader.cpp:96`: `if (n >= header_.verseCount) return false;`.
- **Fix.** State that the resolver checks `lowerBound == verseCount` before calling `verse` and
  treats it as not-found. Add a resolver case: "a reference past the last verse of the last book →
  `true`, `found = false`".

### MINOR 3: The "exact beats prefix" example and test do not exercise the rule

- **Claim.** A5.1: "`Jud` → Jude via `Jud.`, even though it prefixes `Judas`, `Jueces`…". The
  test list includes "Exact beats prefix: `Jud 5` → Jude, not ambiguous with Jueces/Judas".
- **Problem.** `jud` does not prefix `jueces`, whose abbreviation is `Juec.`. `Judas` is Jude's own
  Spanish full name. So `Jud 5` resolves to Jude under the prefix rule alone, and the test passes
  with rule 1 deleted. On the real data, the only place rule 1 decides anything is English `Phil`.
  It is exact for Philippians (`Phil.`) and a prefix of `Philemon`/`Philem.`.
- **Evidence.** A simulation over the real nav labels and TOC names printed
  `e phil -> Philippians also prefixes ['Philemon']`, and no other case in either language. The
  Spanish nav labels are `Juec.` (book 7) and `Jud.` (book 65), and the TOC names are `Jueces` and
  `Judas`.
- **Fix.** Replace the example and the test with `Phil 4:13` → Philippians (English fixture with
  `Phil.`, `Philippians`, `Philem.`, `Philemon`) and keep `Philem 1` → Philemon. `Jud 5` can stay
  as a plain Spanish abbreviation case. Correct "Jueces…" in A5.1.

### MINOR 4: A8's rewrite rule does not say "chapter-only", so `Jude 3:1` would silently become Jude 1:3

- **Claim.** A8: "a lone number is a verse … if `(book, N)` has no chapter entries, `N > 1`, and the
  book's only chapter is 1, the reference becomes `(book, 1, N)`."
- **Problem.** The prose says "lone number", but the implementation rule does not require
  `verse == 0`. Read as written, `Jude 3:1` meets every condition, gets rewritten to Jude 1:3, and
  the typed `:1` is thrown away. That is a jump to a place the reader did not ask for, which A9
  exists to prevent. `Jude 3-5` is not in the grammar, so it is unaffected.
- **Evidence.** Spec lines 68-72. The grammar is A2 (line 39).
- **Fix.** Add "and the parsed reference has no verse (`verse == 0`)" to the A8 condition. Add a
  resolver case: `Jude 3:1` → not found.

### MINOR 5: Building `BookNameSource` as two pointer arrays puts ~528 B on the loop stack

- **Claim.** §1 `BookNameSource { const char* const* names; const char* const* abbreviations; int count; }`,
  "built from `bookNames`" in `runSearch()`.
- **Problem.** `BibleBookNameTable` stores `char names[66][48]` (and, per A14,
  `char abbreviations[66][16]`), not pointer arrays. Filling a `BookNameSource` means two local
  `const char*[66]` arrays, 2 × 264 B on the ESP32. That breaks the CLAUDE.md rule that local
  variables stay under 256 B. The 8 KB loop stack (`CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`) makes
  this safe in practice, but it still breaks the protocol.
- **Evidence.** `BibleBookNameTable.h:41`. CLAUDE.md "The resource protocol" §1.
- **Fix.** Make the view a stride over the fixed arrays, e.g.
  `struct BookNameSource { const char* names; size_t nameStride; const char* abbreviations; size_t abbrevStride; int count; };`,
  filled from `&names[0][0]` / `NAME_BYTES`. Alternatively, expose them as members of the table
  and build the source once, not per query. Either way it stays host-constructible without
  `Epub.h`.

### MINOR 6: Two-letter function words become references by unique prefix

- **Claim.** A5.3: the two-byte minimum makes "`a 3` or `1 3` … never references". The issue:
  "no false positives on ordinary words".
- **Problem.** On the real names, `de 3` → Deuteronomy (both languages), `la 3` → Lamentations
  (both), and `el 3` → El Cantar de los Cantares (Spanish), all by unique prefix. Each still needs
  a trailing number, so real exposure is small, and the harm is only an extra top row. Still, the
  spec's claim about false positives is broader than what the rules deliver, and the test list
  does not record these cases.
- **Evidence.** Simulation output: `s 'de 3' -> Deuteronomio prefix`, `s 'el 3' -> El Cantar de
  los Cantares prefix`, `e 'la 3' -> Lamentations prefix`.
- **Fix.** Either accept it explicitly in A5 and add `de 3` → Deuteronomy as a documented outcome
  in the tests, or require ≥ 3 normalised bytes for a prefix-only match. Exact two-byte
  abbreviations such as `Is.`, `Os.` and `Ps.` would be unaffected, but `Ro 12`, `Ez 1` and `Ap 21`
  would lose their row. Record whichever is chosen.

### MINOR 7: The header still reads "Results: 0" above a Go-to row

- **Claim.** A11 drops the empty-results line when a Go-to row is present, because "the line would
  contradict the row under it".
- **Problem.** `drawChrome` prints `STR_BIBLE_SEARCH_RESULTS` (`"Results: %d"`) from
  `results.size()` whatever the list holds. A `Gen 51`-style miss is excluded, but a typed
  reference with no word hits (common: `Isa 40:31` tokenises to `isa`, `40`, `31`) shows
  "Results: 0" over a working "Go to" row. That is the same contradiction A11 removes one line
  lower.
- **Evidence.** `BibleSearchActivity.cpp:937-944`, `english.yaml:47`.
- **Fix.** Pick one and state it in A11: count the Go-to row in the header, or leave the header
  alone and accept that it counts only word hits. The first needs no new string.

### MINOR 8: A14's clearing rule reads as though `joinToc` would wipe what `load()` fills

- **Claim.** "`joinToc` clears the abbreviations it does not fill, so a grid-side table reports `""`."
- **Problem.** `joinToc` fills no abbreviations, so "clears the abbreviations it does not fill"
  means it clears all of them. That is correct only if `load()` copies `page.labels` after calling
  `joinToc`. The spec gives no order.
- **Evidence.** `BibleBookNameTable.cpp:40-44` (`load` calls `joinToc` last), `:47-57`.
- **Fix.** Say: "`joinToc` clears every abbreviation; `load()` copies `page.labels` after `joinToc`
  returns."

VERDICT: CLEAR
