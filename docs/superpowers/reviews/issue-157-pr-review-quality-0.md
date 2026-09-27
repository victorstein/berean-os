Tier: heavy

# PR #163 — code quality review, pass 0

Scope: the code in `gh pr diff 163`, compared with an up-to-date `origin/main`.
That covers:

- `lib/Catalog/Catalog/{CatalogIndex,CatalogLabel,CatalogStamp}.{h,cpp}`
- `src/activities/catalog/CatalogSearchActivity.{h,cpp}`
- `src/network/{CatalogIndexStore,PubMediaJson,PublicationDownloader,WolWeekScan}.*`
- the two translation YAMLs
- `scripts/build_catalog_index.py` and its new tests
- `.github/workflows/{catalog-index,ci}.yml`
- the five touched host-test suites

The prose under `docs/superpowers/` is out of scope.

Verification:

- Host tests: I rebuilt `CatalogIndexTest`, `CatalogStampTest`,
  `CatalogLabelTest`, `PubMediaJsonTest` and `WolWeekScanTest` and ran them.
  They pass 24/21/10/15/15.
- Script tests: `python3 -m unittest discover -s scripts/tests` passes 26/26.
- Firmware: `pio run -e x4pro` succeeds with RAM at 19.9% and flash at 83.5%.
- Switch warnings: the framework's C++ flags have no `-Wall` or `-Wswitch`
  (`framework-arduinoespressif32-libs/esp32s3/flags/cpp_flags`). So a
  non-exhaustive `switch` builds without a warning either way.
- I made no jw.org requests and dispatched no workflows.

## Pattern fidelity: what is done well

- **The helpers were moved, not copied.** `wordAt`, `digitsToInt` and `copyOut`
  moved out of `CatalogStamp.cpp`'s anonymous namespace into `CatalogLabel`
  (`CatalogStamp.cpp` −37 lines). `formatIssueDate` reuses them, so the repo
  still has only one copy of each.
- **The new callback follows the repo's callback idiom.** `catalog::search`
  takes a plain function pointer plus a `void*` context (`CatalogIndex.h:188`),
  and `CatalogSearchActivity::collectHit` is a static trampoline. This follows
  CLAUDE.md's `struct Callback { void* ctx; void (*fn)(void*); }` guidance,
  where `std::function` would have been the easy choice.
- **The scan loop is now a tested function.** `runSearch` used to hold it
  inline and untested. It moved into `catalog::search` (`CatalogIndex.cpp:141`),
  so the rule "hidden rows are dropped before the cap" is host-tested
  (`CatalogSearchScan.HiddenRowsDoNotUseTheCap`) rather than living only in the
  activity.
- **The version rule matches the existing design.** Reading v1 and v2 while
  refusing newer versions is the same "refuse, don't reinterpret" rule as
  `persist::isKnownFormatVersion` (`lib/Serialization/FormatVersion.h:14`).
  `docs/file-formats.md` is updated alongside it.
- **Error handling follows the established shape.**
  - `Result::NoEpubEdition` sits next to `NoMediaLink`, and its message goes
    through the single `failureMessage` switch (`PublicationDownloader.cpp:262`).
  - Both failure branches `LOG_ERR` before they return.
  - The retry reuses one `DataCallback` and `media->reset()`, so the parser is
    not allocated a second time.
- **The code comments explain non-obvious reasons.** The ones on the unfiltered
  retry (`PublicationDownloader.cpp:661-665` in the diff), the
  `languages`-vs-`files` distinction, and the v1 asset name
  (`CatalogIndexStore.cpp:58-60`) explain a gotcha each. I found no narration
  and no commented-out code.
- **The tests are designed, not just present.**
  - The v2 fixture covers all three flag states in one index.
  - `EachBufferIsReadByItsOwnHeader` pins the in-session v1→v2 swap.
  - `TheLanguagesBlockIsNotTheFilesBlock` pins the one way `languagePresent`
    could be fooled.
  - The Python tests inject the clock, sleep and HTTP, so spacing, cap, budget
    and throttling are asserted deterministically without network access.
- **Test placement follows `test/number_grid`.** `CatalogLabelTest` is a second
  executable inside `test/catalog_stamp/CMakeLists.txt`, the same way
  `test/number_grid` holds two. That avoids a shared-file hand-off. The
  directory name is a slight misfit, but the choice is deliberate and
  documented.
- **The CI job mirrors its siblings.** `script-tests` copies `unit-tests`' job
  shape and its release-please skip guard, and is added to `test-status.needs`.

## Findings

### MINOR 1: the readable-version range is written twice

- `CatalogIndex.cpp:59` defines `readable(version)` as
  `version >= OLDEST_READABLE_VERSION && version <= FORMAT_VERSION`, in an
  anonymous namespace.
- `CatalogStamp.cpp:30` spells out the same range again inside
  `indexAcceptable`.

The two sites have to agree, or the store accepts an index that `search` then
reads as empty, or the reverse. Fix: declare `readable` (or
`versionReadable`) in `CatalogIndex.h` and call it from `indexAcceptable`.

### MINOR 2: the v1 row layout is keyed on the acceptance floor

`nextEntry` chooses the field count with
`version == OLDEST_READABLE_VERSION ? 5 : 6` (`CatalogIndex.cpp:93`). It then
branches on the bare literal `fieldCount == 5` (`:106`).

`OLDEST_READABLE_VERSION` names the oldest layout this build still accepts, not
the layout that has five fields. When v1 is retired (the follow-up the PR body
names), raising that constant to 2 would silently parse v2 rows as five
fields: the flag would become the title, and the title would be dropped.

Fix: name the layout itself, for example `constexpr int V1 = 1;` or a
`V1_FIELD_COUNT` / `V2_FIELD_COUNT` pair, and branch on that.

### MINOR 3: a second copy of the publication-language name ternary

- The new `publicationLanguageName()` (`CatalogSearchActivity.cpp:61-64`) is
  the same expression as `BibleDownloadActivity.cpp:64-65`:
  `SETTINGS.publicationLanguage == CrossPointSettings::PUB_LANG_ENGLISH ? tr(STR_LANG_ENGLISH) : tr(STR_LANG_SPANISH)`.
  That second copy landed in #161.
- The settings class already has the sibling for the language code:
  `CrossPointSettings::langWritten` (`CrossPointSettings.h:72`).

Fix: add a `StrId`-returning helper next to `langWritten` and call it from
both sites. Once a third publication language exists, two hand-written
ternaries will drift.

### MINOR 4: the exhaustive `Result` switch in `BibleDownloadActivity` is now silently non-exhaustive

`BibleDownloadActivity.cpp:252-278` lists every `publication::Result` value
explicitly. It has no `default`, and a trailing fallback after the switch.

The new `NoEpubEdition` is not listed, so it reaches that fallback. The
behaviour is right (`STR_BIBLE_DOWNLOAD_FAILED_HINT`), and the PR body says
so. But the next reader of that switch will think it covers every result.
Nothing will flag the gap either: the firmware builds without `-Wswitch`
(see Verification).

Fix: add `case publication::Result::NoEpubEdition:` to the
`NoMediaLink`/`DownloadFailed`/`OutOfMemory` group at `:272-274`. That keeps
the switch in the shape its author wrote it.

### MINOR 5: general-purpose parsing helpers are now exported from a header named for labels

`CatalogLabel.h:12-17` declares `wordAt`, `digitsToInt` and `copyOut`
publicly. `CatalogStamp.cpp:5` now includes `CatalogLabel.h` only to reach
them.

Deduplicating was right. The home is slightly off: these are catalog text
utilities, not label logic.

Fix: this is optional. Either leave them where they are with a one-line note
in `CatalogLabel.h`, or move them to a small `CatalogText.h`. Neither choice
blocks.

### MINOR 6: unused bindings in the script tests

`scripts/tests/test_build_catalog_index.py` has several unpacked variables
that are never read:

- `results` at `:117`
- `status` at `:245`, `:270`, `:276` and `:283`
- `api` at `:283` (bound as `_`, which is fine)

Fix: bind the unused ones to `_`, or assert on them. `:245` in particular
could assert `status == 0`.

## Not findings

- **`describeHit`'s `char date[48]` and its ignored return value**
  (`CatalogSearchActivity.cpp:51`) mirror the sibling `formatIndexDate` call at
  `:105-106`. The date code is at most 8 bytes, so the fallback always fits.
- **`render_index`'s `(cache or {}).get(key, ("", None))[0]`**
  (`build_catalog_index.py:180`) is terse but correct.

All six findings are MINOR and can be fixed inline. None of them reverses a
decision, changes scope, or needs the owner's judgment.

VERDICT: CLEAR
