# berean-o-27 design — clear the cppcheck style findings failing CI

Research: `docs/superpowers/research/2026-11-22-berean-o-27-research.md`.

## Problem

The CI `cppcheck` job runs `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
(`.github/workflows/ci.yml:76`) and now fails on `main` with five low-severity style findings. The local
cppcheck, `tool-cppcheck 2.11.0+230717`, reports the same five, so a local run reproduces CI exactly (research note,
"Tool versions"). The batch PR cannot go green until those five findings are gone.

## Goal

`pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` exits 0. Firmware
behaviour does not change.

## Non-goals

- No change to `platformio.ini` `check_flags` (`platformio.ini:24`) or to `.github/workflows/ci.yml`.
- No explanation of why the ubuntu-24.04 runner reported nothing (research note). It does not affect the fix.
- No sweep of other `doc["v"] | X` sites or other raw loops that cppcheck does not flag today.
- No new host tests for `PublicationsActivity` or `PreachingSession`. See Testing.

## Architecture

Four files change: three get a mechanical rewrite and one gets a single suppression comment. No type, interface, format or allocation pattern changes.

| # | Site | Change | Modelled on |
|---|---|---|---|
| 1 | `src/activities/catalog/PublicationsActivity.cpp:99` | `recentPaths.reserve(...)` stays; the loop becomes `std::transform(recents.begin(), recents.end(), std::back_inserter(recentPaths), [](const RecentBook& book) { return book.path; });` | `src/activities/reader/PassageLinksActivity.cpp:28-30` |
| 2 | `src/activities/catalog/PublicationsActivity.cpp:102` | Same shape over `entries_` → `listedPaths`, lambda `[](const Entry& entry) { return entry.path; }` | same |
| 3 | `src/activities/launcher/HomeVerseDoc.cpp:133` | The line above gets a one-line reason (ArduinoJson's `|` is a default-value operator, not a bitmask) and `// cppcheck-suppress badBitmaskCheck` | `src/activities/reader/EndOfBookOptions.cpp:99-101`, `src/activities/catalog/CatalogSearchActivity.cpp:401-403` |
| 4 | `src/activities/preaching/PreachingSession.cpp:124-127` | `activeTopics.reserve(picked.size())` stays; the loop becomes `std::copy_if(byName.begin(), byName.end(), std::back_inserter(activeTopics), [&picked](const study::TagId id) { return std::find(picked.begin(), picked.end(), id) != picked.end(); });` | `src/study/BookPathIndex.cpp:25-26` |
| 5 | `src/activities/reader/GridMarks.h:21-23` | `Bits::clear()` body becomes `std::fill(std::begin(bytes), std::end(bytes), uint8_t{0});` | — (the call cppcheck itself suggests) |

Include changes, following `PassageLinksActivity.cpp:6-7` and `BookPathIndex.cpp:7-8`, which include
`<algorithm>` and `<iterator>` directly:
- `PublicationsActivity.cpp` already has `<algorithm>` (`:11`). Add `<iterator>`.
- `PreachingSession.cpp` already has `<algorithm>` (`:11`). Add `<iterator>`.
- `GridMarks.h` has only `<cstdint>` (`:6`). Add `<algorithm>` and `<iterator>`.

## Data and control flow

None of these changes alters a control path:
- Sites 1 and 2 still produce the same vectors in the same order, with the same single `reserve`, before
  `MastheadLayout::coverCandidates` (`PublicationsActivity.cpp:103-104`).
- Site 4 still produces `activeTopics` in `byName` (`TagPalette::activeIdsByName`) order, filtered to the ids in
  `picked`. That is the order `PreachingSession.h:45` documents.
- Site 5 still zeroes all `(CAPACITY + 7) / 8` bytes.
- Site 3 does not change code at all.

## Assumptions (each is a reviewable decision)

- **A1 — The rewrites are at least as clear as the loops.** All four take the exact shape of an existing call
  site in this repo (table above), so a reader has seen each idiom here before. None of the four is suppressed instead.
- **A2 — Iteration order is preserved.** `std::transform` and `std::copy_if` with `std::back_inserter`
  visit and append in input order, which is the same order as the range-for they replace.
- **A3 — No new allocation.** Each `reserve` stays immediately before its algorithm call, and `back_inserter` appends
  through `push_back`, the same as the loop. The lambdas capture nothing or a reference, so they do not allocate.
  None of this is a `std::function`, so the template-bloat rule in CLAUDE.md does not apply: each instantiation sits in one TU, the same as `PassageLinksActivity`.
- **A4 — Site 3 is a false positive and stays as is.** `doc["v"]` is a `JsonVariantConst` and `| 0` is
  ArduinoJson's "value or default", so the call is correct. The rewrite `doc["v"].as<int>()` would also yield 0 on a missing key and
  would satisfy cppcheck. The brief asks for suppression, though, and the same `| default` idiom appears across the stores
  (`src/study/PreachingPassages.cpp:89`, `src/CrossPointSettings.cpp:110`). One spot spelled differently would be the less clear option.
- **A5 — Adding `<algorithm>`/`<iterator>` to `GridMarks.h` is acceptable.** It is a host-testable header
  (`test/number_grid/GridMarksTest.cpp:3`). Both are standard headers and need no Arduino.
- **A6 — `unmatchedSuppression` is suppressed globally** (`platformio.ini:24`). If a future cppcheck stops
  flagging site 3, the comment goes silent rather than failing CI.

## Error handling

There is no new failure path. No allocation is added (A3), and no I/O or parsing changes.

## Testing strategy

- **Red → green:** `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
  exits 1 with `Total 0 0 5` before the change (research note) and must exit 0 after it. This is the test that fails first.
- **Behaviour guards:** the existing host tests stay green. `test/number_grid/GridMarksTest.cpp:62` exercises `Bits::clear()`, and
  `test/home_layout/HomeVerseDocTest.cpp` exercises `fromJson`, the function that contains site 3. Run the full host suite.
- **No new host tests.** `PublicationsActivity` and `PreachingSession` depend on Arduino and FreeInkUI types (research
  note, "Test coverage"), and their rewrites are order- and allocation-equivalent under A2/A3. A harness built only to cover
  them would be more code than the change itself.
- **Build:** `pio run -e x4pro`. **Format:** `./bin/clang-format-fix`, the whole tree.
- **Device:** nothing to test on hardware. Behaviour is unchanged by construction.
