Tier: heavy

# Issue #114 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-114-design.md` (the spec), against issue #114
(`gh issue view 114 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-27-issue-114-research.md`, on `bc1da077` (the code is unchanged
from `af119732`).

Decision d1 (A15, omit the reference line when `r` is empty), the unchanged default sleep mode, and
the read-only status of `src/study/` and `lib/StudyStore/` are settled and are not reopened here.

## What checked out

These claims were read at the cited lines and hold:

- A1: the enum is at `CrossPointSettings.h:55-65`, and the ENUM clamp to field default is at
  `CrossPointSettings.cpp:165-166`. Label slots are indexed by value (`SettingsList.h:233-241`). The
  web page hardcodes only `QUICK_RESUME: 6` (`SettingsPage.html:316-318`). No other consumer of
  `sleepScreen` needs a case: `main.cpp:259`, `SettingsActivity.cpp:368`, `BmpViewerActivity.cpp:70,197,227`,
  `SleepActivity.cpp:807`.
- A2: the popup-then-switch shape is at `SleepActivity.cpp:525-549`, and `default:` goes to
  `renderDefaultSleepScreen`.
- A3: all three loaders go through `loadAdopting` (`PassageFile.cpp:18-24`,
  `ChapterCompletionFile.cpp:18-26`, `TagPaletteFile.cpp:18-23`). `readDocFromFileStreamed` and
  `readDocFromFileChecked` are public and never rename (`PersistableStore.h:76,81`;
  `PersistableStore.cpp:124-162`).
- A8: `random(long)` is `esp_random() % howbig` unless `randomSeed` was called (Arduino-ESP32 3.3.7
  `cores/esp32/WMath.cpp`, `package.json` `"version": "3.3.7"`).
- A12: `getDate` is fresh and `getTime` is cached for 10 s (`HalClock.cpp:15-52`). The
  `floor(... / 1440)` range works out to {-1, 0, +1}, since −720 ≤ total ≤ 2279.
- A13: `Rtc::DateTime::weekday` counts from 0 = Sunday (`Rtc.h:26`). `STR_MONTHS_SHORT` is at
  `english.yaml:439` and `spanish.yaml:381`. `wordAt` is file-local (`CatalogStamp.cpp:21`), so the
  pure header has to re-implement it.
- Test vectors: FNV-1a-32 of `""` is `0x811c9dc5` and of `"a"` is `0xe40c292c`. 2026-09-27 is a
  Sunday (python `strftime('%A')`).
- A10 budget: 16 × 10 digits, commas and three keys come to ≈ 250 B. On top of the ~1,190 B worst
  case that stays well under `SAVE_BUDGET = 2048`.

## Findings

### MAJOR 1: the ring key `pubkey/u` is not unique per passage, which breaks A9's no-repeat guarantee

**Claim.** A9 says: "with ≥ 2 passages, the just-shown passage is never shown next". A10 says: "A
hash collision can only suppress one passage for a few sleeps."

**Problem.** Two passages in the same publication can share a start unit `u` without any hash
collision. Tagging verse 16 and later verses 16–17 is enough. `PassageDoc::add` does not dedupe on
start (`lib/StudyStore/StudyStore/PassageDoc.cpp:91-107`). Every row carries its own `u` and `e`
(`PassageDoc.cpp:194-195`).

Both passages then get the same key, so after either one is shown, both have ring age 0. Take a
store where every candidate is recent, for example two passages A and B that share `u`. The "oldest
recent" rule faces a tie that A9 never breaks:

- If the tie keeps the first passage seen, A is shown on every sleep and B is never shown.
- Either way, A can follow A, which breaks the stated invariant and the issue's "no immediate
  repeats".

A9 also never says which occurrence `ageOf` reports when a key appears more than once in the ring.
That happens routinely in small stores, where the ring holds the same key several times.

**Evidence.** `PassageDoc.cpp:91-107` (no dedupe on `start`). `PassageDoc.cpp:194-195` (`u` and `e`
written per row). Spec A9 at `:99-107` and A10 at `:109-120`. Test list items 4 and 5 (`:301-302`)
cover neither ties nor duplicate keys.

**Fix.**
- Key on `pubkey + '/' + u + '/' + e`, using the stored `e` or, when it is absent, `u`, the same way
  `fromJson` defaults `e` at `PassageDoc.cpp:228`.
- Specify that `ageOf` returns the newest occurrence, which is the smallest age, walking newest-first
  as `isRecentIndex` does (`CrossPointState.cpp:11-20`).
- Specify the tie-break for equal ages, for example keeping the first candidate seen.
- Add host tests for "two candidates with equal age" and "a key present twice in the ring".
- Change the A10 wording from "hash collision" to cover both genuine key sharing and hash collisions.

### MAJOR 2: A4's memory mechanism is wrong, because ArduinoJson 7 never makes a 4 KB allocation

**Claim.** A4 (`:65-68`): "Allocations over 4 KB route to PSRAM on this build … so the pick does not
pressure internal SRAM."

**Problem.** A parsed `JsonDocument` in ArduinoJson 7.4.2 is built from nothing but small blocks:

- On a 32-bit target, `ARDUINOJSON_SLOT_ID_SIZE` is 2, so each variant pool is 128 slots, which the
  library's own comment puts at "1024 bytes" (`ArduinoJson/Configuration.hpp:102-116`).
- Every distinct string is a separate `StringNode` allocation (`Memory/StringNode.hpp:41`). Strings
  are deduplicated by a linear scan in `StringPool::add` (`Memory/StringPool.hpp:44-62`).

All of these blocks are under the `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` threshold
(`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2153-2154`). IDF's `heap_caps_malloc_default`
therefore places them in **internal** SRAM, and uses PSRAM only after internal SRAM is exhausted
(`framework-espidf/components/heap/heap_caps.c:117-124`, the "less picky" retry).

The worst case the spec budgets for, one file of `MAX_FILE_BYTES ≈ 204 KB`, can drain internal SRAM
before anything spills to PSRAM. That happens on the sleep path, and WiFi is torn down only after
`goToSleep` returns (`main.cpp:271-285`).

The device check at `:318` compounds the error. `ESP.getFreeHeap()` reports internal heap only, so
the check cannot show where the bytes went. The project memory note
`esp32s3-psram-auto-routing-threshold` warns about exactly this.

The realistic cost for the real store, roughly one 63-passage file, is tens of KB of transient
internal heap. That is the same cost `PassageFile::load` already pays in the UI. It is acceptable,
but it has to be stated correctly.

**Evidence.** As cited above. ArduinoJson was read from a sibling worktree's install of the pinned
7.4.2 (`version.hpp:7`).

**Fix.** Pick one of the following and write the true mechanism into A4:

- (a) Give the per-file `JsonDocument` a PSRAM-backed `ArduinoJson::Allocator`, built from
  `heap_caps_malloc(…, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` as `src/study/BibleSearchStore.cpp:25-27`
  already does. `readDocFromFileStreamed` takes any `JsonDocument&`, so `lib/Serialization` needs no
  change, and the "does not pressure internal SRAM" claim becomes true.
- (b) Keep the default allocator, state the internal-SRAM peak as "≈ the parsed size of the largest
  file, the same as `PassageFile::load`", and lower `MAX_FILE_BYTES` to a figure justified against
  free internal heap.

In both cases, the device check should log `ESP.getFreeHeap()` **and** `ESP.getFreePsram()` before
and after the pick.

### MAJOR 3: the A7 caps silently and systematically exclude the same files, and the headroom claim is unmeasured

**Claim.** A7 (`:85-91`): "a partial scan is still a uniform draw over the files it read … The
figures are sized to the real store (63 passages in a handful of files), leaving roughly 30×
headroom."

**Problem.** The scan always starts at the first directory entry, so after a cap it covers the same
leading files every time. The rest are never eligible, on any sleep, and the only trace is a
`LOG_INF`.

The directory order is also not random. `writeDocToFileAtomic` writes `<path>.tmp`, removes the
primary and renames the temp file (`PersistableStore.cpp:111-117`), so each save moves that
publication's entry into whatever free slot the rename takes. Which publications are excluded
therefore depends on save history, not on chance.

"Uniform over the files it read" is true but hides this bias. Neither the research nor the spec
measures the file count or byte total, so the "30×" figure is asserted, not measured. `MAX_FILES = 64`
is also reachable in ordinary use: one file per publication tagged, and each monthly Watchtower or
bimonthly workbook is a separate publication.

**Evidence.** `PersistableStore.cpp:111-117`. `SleepActivity.cpp:386-412` (the model loop starts at
entry 0). Research §4 (`:112`) gives a passage count (63), not a file count or byte total.

**Fix.**
- Make a cheap first pass that counts the `.json` entries without opening or parsing them.
- Draw a random start `s = rng(count)`, then run the parsing pass cyclically from `s`, using
  `rewindDirectory()` as `SleepActivity.cpp:438` does.
- With that in place, a capped scan covers a random window, and every file has the same chance of
  being included.
- Alternatively, or as well, drop `MAX_FILES` as a separate cap and let `MAX_TOTAL_BYTES` bound the
  work, since that is the real cost.
- Either way, replace the "30×" sentence with a measured figure or remove it.

### MINOR 1: `ageOf` has two homes, and the tested one may not be the one used

A10 (`:122-124`) puts `ageOf` "in `CrossPointState` beside `isRecentIndex`". The Architecture
(`:228`) and data flow (`:257,261`) instead use a `RingView`/`ageOf` in the pure header, called as
`ring.ageOf(key)`.

If both exist, the host tests cover a copy that the device might not call. Putting it in
`CrossPointState` would also make `src/CrossPointState.cpp` depend on a header under
`src/activities/boot_sleep/`.

**Fix.** Keep a single `ageOf` in `StudySleepPick.h`. `CrossPointState` only exposes the raw ring
plus `pushRecentStudySleep`, and the screen code builds the view.

### MINOR 2: `StudySleepPick.h` claims to be ArduinoJson-free, but its slot sizes come from `PassageDoc.h`

`:245` says the header "includes nothing from … ArduinoJson". The slot struct in A19 (`:209-213`),
which the sampler fills, is sized by `MAX_SNIPPET_BYTES` and `MAX_REFERENCE_BYTES`. Those live in
`PassageDoc.h`, and that header includes `<ArduinoJson.h>` (`lib/StudyStore/StudyStore/PassageDoc.h:3`).

The `pubKey[64]` field is also unnecessary. A19 says it is "only used for the hash", but the key is
computed before the slot is filled, so the field can go. The stem can then be hashed in full instead
of in a truncated form.

**Fix.** Either give the header its own `constexpr` sizes with a `static_assert` against the
`PassageDoc` constants in `StudySleepScreen.cpp`, or template the slot on them. Drop `pubKey` from
the slot.

### MINOR 3: the entry point has two names, and the painter never says `clearScreen()`

A2 (`:40`) names a member `renderStudySleepScreen()`. The data flow (`:255`) names a free function
`study_sleep::render(renderer)`.

A18 never says that the painter clears the framebuffer. The "Entering sleep" popup drawn at
`SleepActivity.cpp:526-532` is still in it, and `renderDefaultSleepScreen` clears first (`:641`).

**Fix.** Name one entry point, and add `renderer.clearScreen()` as the first draw step in A18.

### MINOR 4: A5 accepts rows that `PassageDoc::fromJson` drops

A5 requires only a non-empty `u` string and takes "the first non-zero" tag. `fromJson` is stricter:

- It skips a row whose `u` fails `unitFromCompact` (`PassageDoc.cpp:227-228`).
- It drops tag ids over `UINT16_MAX` (`:240-243`).

So a row that is invisible everywhere else on the device could still appear on the sleep screen.

**Fix.** Require `unitFromCompact(u)` to succeed. It is a pure read from `StudyStore/Unit.h:47`, not
an edit. Take the first tag in `1..UINT16_MAX`.

### MINOR 5: with 16 passages or fewer, A9 settles into a fixed cycle

The ring holds 16 entries (`CrossPointState.h:15`). Once every passage is recent, "oldest recent
wins" replays the order of the first pass forever, so the display stops being random. This matches
what the image picker does (window `min(fill, fileCount-1)`, `SleepActivity.cpp:429`), and the
user's store of 63 passages is unaffected. It is still visible behaviour the spec does not state.

**Fix.** Add one line to the PR body's "Known limits".

### MINOR 6: `STR_BOOKS_FINISHED` gives "1 books finished"

`"%u books finished"` and `"%u libros terminados"` (`:152`) are wrong for a count of 1.

**Fix.** Use a count-neutral form: "Books finished: %u" / "Libros terminados: %u".

## Verdict

None of the findings reverses a decision, changes scope, or needs a judgment only the human can
make. Each MAJOR has a concrete fix that stays within the existing surfaces (`src/` and the new
files only). They should be fixed inline before the plan.

Findings: 0 BLOCKER, 3 MAJOR, 6 MINOR.

VERDICT: CLEAR
