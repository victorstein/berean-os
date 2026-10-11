# berean-o-7 (R22) — design: keep Home's verse across deep sleep

Research: `docs/superpowers/research/2026-10-11-berean-o-7-research.md`. Branch
`perf/r22-verse-once-per-sleep`, on `478cf1ed`. Review tier: heavy.

**Modelled on:** `src/network/MeetingWeekCache.{h,cpp}` for the shell — a cache, not study data,
under `/.berean/`, refusing an unknown version and writing atomically after a budget check.
`src/util/PlacesDoc.{h,cpp}` for the pure format half — a format version, a byte budget derived
from caps, and `toJson`/`fromJson` with no storage access. The host tests are modelled on
`test/home_layout/HomeVerseCacheTest.cpp` and `test/places_doc/`.

## Problem

Every wake that lands on Home scans `/.berean/passages/bible.json` again and paints Home twice.
Home's verse lives in a RAM singleton (`HomeVerse.cpp:57-60`) keyed by the local date and the
file's size (`HomeVerse.cpp:39-53`, `HomeVerseCache.h:33-43`). Waking from deep sleep is a chip
reset (`main.cpp:270`), so after any wake the entry is invalid (`HomeVerseCache.h:57-58`). Three
things follow:

1. `resolveTargets` sets `versePending` (`LauncherActivity.cpp:148`).
2. `onEnter` skips its paint (`:82-84`), so `loop()` paints Home without the verse, waits, scans,
   and paints again (`:438-447`).
3. The scan logs `Verse scan: … ms` (`HomeVerse.cpp:98-101`).

Baseline, from one clean capture: the scan takes 405 ms and the second paint 257 ms of CPU plus a
~566 ms FAST. First frame at 3,126 ms after reset, settled at 4,427 ms
(`docs/superpowers/notes/responsiveness-baseline.md:51-52`, `:75`).

## Goal

- A wake on the same day, with `bible.json` the same size and the same firmware, logs no
  `Verse scan`, and Home paints once with the verse already in it.
- A new day, a changed file size, a new firmware build, a missing or unreadable cache, or an
  out-of-memory scan behaves exactly as today: a scan and two paints.
- The pick shown is the one a rescan would have made. Persistence remembers a deterministic
  result; it never changes which verse Home shows.

## Non-goals

- **The sleep-time `Study pick` (445 ms).** It is a different, random pick over every passage file
  (`StudySleepScreen.cpp:229`, `:250-252`) and stays as it is. `StudySleepScreen` and
  `test/study_sleep_pick/` are not edited, although both are in the task's file lock.
- **The first wake of a new day.** The pick is seeded by the local date (`HomeVerse.cpp:68-73`),
  so it cannot be known before that day starts.
- **Same-size edits to `bible.json`.** The file identity stays byte size only. That gap exists
  today within one boot; persistence extends it to the rest of the same day (A5).
- **RTC memory and the sleep pass producing Home's pick.** Both were rejected in research
  (`…-research.md`, "The three ways to skip the wake scan", B and C).
- **`LauncherActivity.cpp`.** It is not edited; the second paint goes away through
  `HOME_VERSE.isCurrent()` alone.
- **Panel waveforms.** The wake paint stays a HALF (`LauncherRefresh.h:15-17`); only the second
  FAST paint is removed. Nothing here is controller-specific, so SSD1677 and UC8179 behave the same.

## Assumptions (each is a decision the review can attack)

**A1 — The medium is a JSON file on the SD card.** `/.berean/home-verse.json`, read and written
through `PersistableStoreBase`'s static helpers, as `MeetingWeekCache` does
(`MeetingWeekCache.cpp:18-56`). RTC memory was rejected: nothing in this repo relies on RTC data
surviving deep sleep. The sleep pass was rejected: it would need a second sampler and gate in the
shared scan, and it computes the pick for the date of sleep.

**A2 — Path and name.** A new `sdpaths::HOME_VERSE_FILE[] = "/.berean/home-verse.json"` goes beside
`MEETING_WEEKS_FILE` (`SdPaths.h:24`), with its `static_assert(isUnder(…, BEREAN_DIR))` (`:50`).
The literal is pinned in `test/sd_paths/SdPathsTest.cpp:24-37`. It sits at the `/.berean/` root,
not under `passages/`. `pickFromAll` treats every `*.json` in `PASSAGES_DIR` as a passage file
(`StudyPassageScan.cpp:48-52`, `StudySleepPick.h:63-68`), so a file there would be scanned as one.

**A3 — The file holds the whole `Entry`.** That is the key, the empty reason and the pick with its
lines already fitted (`HomeVerseCache.h:45-63`), so restoring needs no renderer and no
re-measuring. The cost is that the lines were fitted for one build's fonts and card. A4 handles
that.

**A4 — An entry from another firmware build is never restored.** The file records a build
identity. Restore requires it to equal the running build's.

- Within one boot the card box, the fonts and the fit algorithm cannot change, which is why the
  RAM key never needed them (research, "What Home's pick depends on").
- The identity is `esp_app_get_elf_sha256(buf, 9)` (`esp_app_desc.h:64`). With
  `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9` (`esp32s3/sdkconfig:507`) it returns the first 8 hex
  characters of the ELF's SHA-256
  (`framework-espidf/components/esp_app_format/esp_app_desc.c:107-116`). That is the 5.5.2 source
  on disk; the libraries are built from 5.5.0 (research, "Installed versions"). It is the same
  prefix IDF prints at boot (`:143-144`).
- The field is filled in because `framework-arduinoespressif32/tools/pioarduino-build.py:262`
  adds `--elf-sha256-offset 0xb0` to the ELF-to-bin step.
- **Rejected:** `BEREAN_VERSION`. `scripts/git_branch.py` builds it from the branch and short SHA
  (`:3-6`, `:50-66`) and has no dirty-tree marker, so a rebuild from an uncommitted tree keeps the
  same string.
- **Rejected:** re-fitting the stored text on restore. `isCurrent()` gets no renderer or card box,
  so `LauncherActivity.cpp:148` would have to change.
- **Guard:** an empty identity, or one that is all `0`, disables both restore and save, with
  `LOG_ERR`. An unfilled SHA would otherwise make every build look the same.
- The file reserves up to 64 characters for the identity, so a later build can store more of the
  hash; a longer identity simply does not match.

**A5 — The key keeps today's meaning.** `sameKey` (`HomeVerseCache.h:40-43`) decides currency, as
today: `dated`, the local `day`, `fileExists` and `fileBytes`. The build identity is checked when
the file is read (A4), not added to `Key`, so `HomeVerseCacheTest` keeps passing unchanged.

**A6 — Restore happens once per boot, on the first `isCurrent()` or `ensure()` call.** A new
private `bool restoreAttempted` on the singleton makes the second call a no-op.

- `isCurrent()` loses its `const` (`HomeVerse.h:32`). Its only caller holds the singleton by
  non-const reference (`LauncherActivity.cpp:148`, `HomeVerse.h:49`), so the call site is unchanged.
- That first call comes from `resolveTargets`, which `onEnter` runs before it requests any paint
  (`LauncherActivity.cpp:79-84`). The render task, a different task (`ActivityManager.h:52-54`),
  therefore cannot read the entry while restore fills it.
- `isCurrent()` is not called when no Bible is found (`LauncherActivity.cpp:148`
  short-circuits), so a boot that never finds a Bible never reads the file.
- **Rejected:** restoring in `setup()`. `src/main.cpp` is a shared append point, and every boot
  would pay for it, including the ones that resume straight into the reader.

**A7 — Every completed scan is saved, and nothing else is.**

- "Completed" means every path on which `ensure` sets `entry.valid = true` (`HomeVerse.cpp:111-133`):
  a pick, `NoPassages`, `TooLong`, a missing `bible.json`, and the "does not fit the card" fallback
  (`:120-126`).
- The two out-of-memory paths (`:74-80`, `:102-108`) leave the entry invalid and are not saved,
  exactly as they are not cached today.
- A scan runs only when the key or the build changed, so the result always differs from the file
  and there is no redundant write to guard against.

**A8 — The save happens inside `ensure()`, on the loop task, before it returns.**

- It delays paint #2 by one small atomic write, on scan paths only.
- **Rejected:** saving after `requestUpdate()` in `LauncherActivity::loop`. It would edit
  `LauncherActivity.cpp`, and it would contend with paint #2's cover stream from SD
  (`LauncherActivity.cpp:248`) for `storageMutex` anyway.
- The write's duration is logged (see Logging), so the owner sees what it costs.

**A9 — An unknown format is discarded, then overwritten.**

- A file with an absent, zero, negative or newer `"v"` is not restored. It logs `LOG_ERR` and the
  scan runs. That scan's save replaces the file. This is cache behaviour, as in
  `MeetingWeekCache.h:17-19` and `MeetingWeekCache.cpp:23-27`.
- The version is read with `doc["v"] | 0`, so a file with no `"v"` is refused (`FormatVersion.h:10-13`).
  This file never existed unversioned, which is `PlacesDoc.cpp:124`'s reasoning too.
- CLAUDE.md's rule — refuse rather than reinterpret — is met. A future format is never read
  as this one. Losing it costs one scan and never a user's work.

**A10 — A malformed entry is refused whole, never repaired.** Any field that fails validation
refuses the entry. It is not restored, and the next scan's save overwrites it. The checks are
listed under the format.

**A11 — The budget is derived from caps, as `PlacesDoc` derives its own.** `HomeVerseDoc::SAVE_BUDGET
= worstCaseBytes()` comes from the format's fixed overhead plus a 64-character build identity, the
key's maximal numbers, a 28-byte unit (`PlacesDoc.h:31`), a 48-byte reference and `LINE_BUFFER_BYTES`
of line text (`HomeVerseCache.h:25`, `:27`). Every string byte is counted at an escape factor of 6:
`\u00XX` is the longest escape JSON has, so no argument about ArduinoJson's escaping is needed. The
result is around 4 KB.

The shell checks two things before writing, and refuses with `LOG_ERR` on either, never
truncating:

- `measureJson(doc)` against the budget, as `MeetingWeekCache.cpp:50-53` does.
- `doc.overflowed()`, so a document that ran out of memory while being built is never written.

**A12 — The read is `readDocFromFileAdopting`.** It is what `MeetingWeekCache.cpp:22` uses: a
`.tmp` left by an interrupted rename is promoted (`PersistableStore.h:83-91`). The file is far
under the 50,000-byte `readFile` cap (`SaveBudget.h:19`), so streaming is not needed. Because the
adopting read renames, its single-task rule applies (`PersistableStore.h:106-114`):

- Every reader and writer of `home-verse.json` runs on the loop task. `HomeVerse` is owned by it
  (`HomeVerse.h:17`).
- `onEnter` and `loop` run from `ActivityManager::loop` and `replaceActivity`
  (`ActivityManager.cpp:73-174`, `:184-196`). Both are called from Arduino's `setup()`/`loop()`,
  which share one task (`framework-arduinoespressif32/cores/esp32/main.cpp:52-82`).
- The web server cannot touch the file. It refuses any path with a dot-named component
  (`ProtectedPath.h:5-8`, `ProtectedPath.cpp:21-23`, `FileRoutes.cpp:124`, `:179`).

`home-verse.json` is added to the list of files in that `PersistableStore.h` comment that rely on
the single-task property.

**A13 — Both JSON documents allocate from `PsramJsonAllocator::json()`.** The pick's lines are
passage text, and the passage scan keeps passage text out of internal SRAM the same way
(`StudyPassageScan.cpp:115-118`, `PsramJsonAllocator.h:8-15`). Both documents are transient, a few
KB, and freed when the restore or save returns. No new resident allocation is made. The resident
cache stays the existing static `Entry`, under 640 bytes of internal SRAM
(`HomeVerseCache.h:95`). An allocation failure shows up as `overflowed()` or a parse error, which
the restore and save paths already treat as failures.

**A14 — Undated entries are saved like dated ones.** An undated key matches any day
(`HomeVerseCache.h:40-43`, `HomeVerseCacheTest.cpp:53-61`), and an undated scan always uses
`FALLBACK_SEED` (`HomeVerse.cpp:68-73`). A restored undated pick is therefore the one a rescan
would make, for as long as the file size and the build hold.

**A15 — The JSON uses short keys, as `places.json` does** (`PlacesDoc.cpp:109-120`). The empty
reason is written as an explicit code, not the enum's underlying value, so reordering
`home_verse::Empty` (`HomeVerseCache.h:31`) cannot silently change the file's meaning.

## Architecture

| Piece | Kind | Role |
|---|---|---|
| `src/activities/launcher/HomeVerseDoc.{h,cpp}` | new, pure | `FORMAT_VERSION`, caps, `worstCaseBytes()`, `SAVE_BUDGET`, `toJson`, `fromJson`, `isUsableBuildId`. Includes ArduinoJson, `HomeVerseCache.h`, `StudyStore/Unit.h`, `FormatVersion.h`, `util/CivilDate.h`; no Arduino, HAL or renderer. Never measures or serialises, as `PlacesDoc.h:16-18` |
| `src/activities/launcher/HomeVerse.{h,cpp}` | changed shell | `restoreOnce()` and `save()`; `isCurrent()` loses `const`; `ensure()` calls `restoreOnce()` first and `save()` after a completed scan; the class comment stops saying "nothing stored" (`HomeVerse.h:12-17`) |
| `lib/Serialization/SdPaths.h` | one constant | `HOME_VERSE_FILE` plus its `static_assert` (A2) |
| `lib/Serialization/PersistableStore.h:106-114` | comment | adds `home-verse.json` to the single-task list (A12) |
| `test/home_layout/HomeVerseDocTest.cpp`, `test/home_layout/CMakeLists.txt` | new test and new executable | see Testing |
| `test/sd_paths/SdPathsTest.cpp` | one pin | A2 |
| `docs/file-formats.md` | new section | `/.berean/home-verse.json`, after `places.json` (`:487-511`), in that section's shape |

`HomeVerseDoc`'s interface, as the spec fixes it (the plan names the details):

```cpp
namespace HomeVerseDoc {
inline constexpr int FORMAT_VERSION = 1;
inline constexpr size_t MAX_BUILD_ID_BYTES = 64;
constexpr size_t worstCaseBytes();
inline constexpr size_t SAVE_BUDGET = worstCaseBytes();

enum class Restore : uint8_t { Restored, UnknownFormat, OtherBuild, Malformed };

// Non-empty, at most MAX_BUILD_ID_BYTES, and not all '0'.
bool isUsableBuildId(std::string_view buildId);

// `entry` must be valid.
void toJson(const home_verse::Entry& entry, std::string_view buildId, JsonDocument& doc);

// Fills `out` in place: an Entry is too large for the stack (HomeVerse.cpp:110). `out.valid` is
// true only on Restored. `rungCount` bounds the stored rung.
Restore fromJson(JsonVariantConst doc, std::string_view buildId, uint8_t rungCount, home_verse::Entry& out);
}
```

`fromJson` returns the stored key with the entry. Whether it answers for today is still
`home_verse::validFor(entry, currentKey())` (`HomeVerse.cpp:62`, `:66`). A restored entry then
behaves exactly as an entry from earlier in the same boot would.

### Format, version 1

```json
{"v":1,"b":"1a2b3c4d",
 "k":{"dt":true,"y":2026,"m":10,"d":11,"f":true,"n":43864},
 "e":0,
 "p":{"u":"v:43:3:16:0","s":1201,"r":"Juan 3:16","rung":1,
      "l":["Porque tanto amó Dios al mundo","que dio a su Hijo unigénito"]}}
```

| Key | Meaning | Validation in `fromJson` |
|---|---|---|
| `v` | format version | `persist::isKnownFormatVersion(doc["v"] \| 0, 1)`, else `UnknownFormat` |
| `b` | build identity (A4) | equals `buildId`, else `OtherBuild` |
| `k.dt` | dated | bool |
| `k.y`, `k.m`, `k.d` | local date | when `dt`: `isValidCivilDate` (`CivilDate.h:45-49`); when not dated, written as 0 and ignored, as `sameKey` ignores them (`HomeVerseCache.h:42`) |
| `k.f` | `bible.json` existed | bool |
| `k.n` | its byte size | ≤ `UINT32_MAX` |
| `e` | empty reason: `0` a pick, `1` no passages, `2` too long | one of those codes |
| `p` | present exactly when `e` is `0` | present with `e != 0`, or absent with `e == 0`: `Malformed` |
| `p.u` | start unit, `study::unitToCompact` (`Unit.h:46`) | `unitFromCompact` parses (`Unit.h:47`) |
| `p.s` | spine hint | ≤ `UINT16_MAX` |
| `p.r` | reference | at most `REFERENCE_BYTES - 1` = 48 bytes (`HomeVerseCache.h:27`) |
| `p.rung` | font rung | `< rungCount` (`HomeVerse::RUNG_COUNT` = 3, `HomeVerse.h:21`) |
| `p.l` | fitted lines | an array that `home_verse::packLines` accepts: 1–`MAX_LINES` strings whose bytes plus terminators fit `LINE_BUFFER_BYTES` (`HomeVerseCache.h:75-91`) |

Every string is read as `const char*` (`obj["r"] | ""`), never as `std::string` (`PersistableStore.h:144-147`).
A wrong type, such as `"n":"43864"`, falls back to the default, which then fails validation.

## Data and control flow

**Same-day wake, unchanged file, same build** — the case this exists for:

1. The wake routes to `goHome(true)` (`main.cpp:550-554`, `:591-592`).
2. `resolveTargets` calls `HOME_VERSE.isCurrent()` → `restoreOnce()`. That reads the file
   (A12) and gets `Restored`, with `entry.valid` set.
3. `validFor(entry, currentKey())` is true, so `versePending` is false (`LauncherActivity.cpp:148`).
4. `onEnter` calls `requestUpdate()` (`:84`): one paint, a HALF (`:364-368`), with the verse drawn
   (`:290-306`).
5. `loop()` never takes the `versePending` branch (`:439`). No `Verse scan`, no second paint.

**New day, or `bible.json` changed size:** restore succeeds, but `validFor` is false. The flow is
today's: paint, `ensure()`, scan. Then `save()` overwrites the file with the new key and pick, and
paint #2 follows.

**First boot of a new build:** `Restore::OtherBuild`, then `LOG_DBG` and `entry.valid = false`.
Then today's flow, and the save records the new identity.

**No file, an unreadable file, a parse error, an unknown version or a malformed entry:**
`entry.valid = false`, then today's flow. The save replaces the file.

**Later Home entries in the same boot:** `restoreAttempted` is set, so `restoreOnce()` returns at
once. The RAM entry answers, as today.

**A tag edit during the boot:** the next Home entry finds the size changed (`HomeVerse.cpp:49`),
scans, and saves.

**Out of memory during a scan:** not cached and not saved (A7). The next entry retries, as today
(`HomeVerse.cpp:102-108`).

## Error handling

| Case | Behaviour | Log |
|---|---|---|
| File missing | not restored; scan | none (expected on first boot; `readDocFromFileChecked` returns `Missing` without logging, `PersistableStore.cpp:125-126`) |
| Unreadable or parse error, including PSRAM exhaustion | not restored; scan; overwritten on save | the read helper's own `LOG_ERR`, plus the restore line |
| Unknown format | not restored; scan; overwritten | `LOG_ERR`, as `MeetingWeekCache.cpp:25` |
| Another build | not restored; scan; overwritten | `LOG_DBG`, an expected case after every update |
| Malformed field | not restored; scan; overwritten | `LOG_ERR` |
| Unusable build identity | neither restored nor saved; today's behaviour | `LOG_ERR`, once per boot |
| Over budget, or `doc.overflowed()` while building | not written; the RAM entry stays valid for this boot | `LOG_ERR` |
| Atomic write fails | the old file or the `.tmp` is left as `writeDocToFileAtomic` leaves it, never a torn file (`PersistableStore.h:62-68`); the RAM entry stays valid; the next boot rescans | `writeDocToFileAtomic`'s `LOG_ERR`, plus `Verse save failed` |

No failure in either path leaves Home worse off than it is today: every one of them falls back
to the scan the device already does.

### Logging

The timings use `LOG_DBG`, per the brief. The existing `Verse scan` line keeps its `LOG_INF`
(`HomeVerse.cpp:98`) because the acceptance check looks for it.

- `[HOMEV] Verse restore: <restored|none|unknown format|other build|malformed|unreadable>, current <0|1>, <ms> ms`
  — once per boot, from `restoreOnce()`. `current` is `validFor` against the key just built.
- `[HOMEV] Verse saved: <bytes> bytes, <ms> ms`, or `LOG_ERR` `Verse save failed`.

## Resource justification

- **Internal SRAM:** one `bool` on the static singleton. The resident `Entry` is unchanged.
- **Stack:** a 9-byte identity buffer and the existing ~34-byte path buffer (`HomeVerse.cpp:32`).
  `JsonDocument` objects are handles; their pools are on the heap (A13). Every frame stays under
  256 bytes.
- **Heap:** transient only — a PSRAM document of a few KB, plus `readFile`'s `String` of the file's
  size (under the 4,096-byte internal threshold, freed on return). There are no allocations in a
  loop.
- **Flash:** one new translation unit using ArduinoJson accessors, plus a `measureJson` in
  `HomeVerse.cpp`. The plan's `pio run` reports the size delta. The serializer and parser stay
  instantiated in `PersistableStore.cpp` alone (`PersistableStore.h:15-23`).
- **SD:** one read of a file under 4 KB per boot that reaches Home with a Bible. One atomic write
  per scan — a new day, a size change or a new build — never on a same-day wake.

## Testing strategy

TDD, host first. Each test is written, run red, and only then its code is written.

**`test/home_layout/HomeVerseDocTest.cpp`**, a new `HomeVerseDocTest` executable in
`test/home_layout/CMakeLists.txt`. It is modelled on the `PlacesDocTest` target
(`test/places_doc/CMakeLists.txt:1-34`): sources `HomeVerseDoc.cpp` and
`lib/StudyStore/StudyStore/Unit.cpp`, include directories `src`, `lib/StudyStore` and
`lib/Serialization`, and it links `ArduinoJson`. It is a separate executable so `HomeLayoutTest`
keeps its "std headers only" scope (`test/home_layout/CMakeLists.txt:1-4`).
`test/CMakeLists.txt:129` already adds the suite, so the shared file is not touched.

1. A pick round-trips: `toJson`, then `fromJson`, gives back the key, the empty reason, the rung,
   every line, the reference, the unit and the spine, with `valid` true.
2. `NoPassages`, `TooLong`, a missing file (`f:false`) and an undated key each round-trip.
3. The restored key still answers through `home_verse::validFor` for the same key, and not for the
   next day or a changed size. This pins A5's "the key keeps today's meaning".
4. A golden document — the exact JSON a fixed entry produces — pins the on-card field names and
   the empty-reason codes (A15).
5. `UnknownFormat` for `v` absent, 0, negative and 2.
6. `OtherBuild` when `b` differs or is missing.
7. `Malformed`, each with `valid` false:
   - `p` present with `e` 1;
   - `p` absent with `e` 0;
   - `e` 3;
   - `rung` equal to `rungCount`;
   - zero lines, and five lines;
   - lines too long for the buffer;
   - a 49-byte reference;
   - an unparseable unit;
   - spine 65536;
   - a dated key on 2026-02-30.
8. A maximal entry — 48-byte reference, `LINE_BUFFER_BYTES` of `"` characters, a 64-character
   identity, maximal numbers — measures within `SAVE_BUDGET`. `SAVE_BUDGET` equals
   `worstCaseBytes()`, pinned to its number as `PlacesDocTest` pins its own.
9. `isUsableBuildId` rejects `""`, `"00000000"` and 65 characters, and accepts `"1a2b3c4d"`.

**`test/sd_paths/SdPathsTest.cpp`:** pins `HOME_VERSE_FILE` to `"/.berean/home-verse.json"`.

**Unchanged and still green:** `HomeVerseCacheTest` (`test/home_layout/HomeVerseCacheTest.cpp`),
which A5 must not break.

**The shell has no host test.** `HomeVerse.cpp` uses `GfxRenderer` and `HalStorage`; it has none
today either, and the logic it gains is a call to the tested `fromJson`, `toJson` and
`isUsableBuildId`. It is verified by `pio run -e x4pro` and on the device.

**Device — the owner's run.** Flash the `x4pro` dev build and capture serial with
`cat /dev/cu.usbmodem*`.

1. Go to Home. Expect `Verse restore: other build` (or `none`), then `Verse scan`, then
   `Verse saved`.
2. Sleep and wake on the same day. Expect `Verse restore: restored, current 1`, no `Verse scan`,
   and a single `[LAUNCH] Paint: clean=1 firstPaint=1 mode=HALF`.
3. The settled time is that paint's following `Wait complete:  8179_DRF (… ms)` timestamp.
   Against 4,427 ms, arithmetic predicts about 3.1–3.2 s. The restore's own `ms` and the extra
   glyphs in the first paint are not yet measured.
4. Tag a passage, go Home: expect `Verse scan` and `Verse saved`.
5. Optionally set the clock forward a day and wake: expect `current 0` and a scan.

## Files outside the task's lock

The task registered `src/activities/launcher/HomeVerse`, `src/activities/boot_sleep/StudySleepScreen`
and `test/study_sleep_pick/`. The `HomeVerse` prefix covers `HomeVerse.{h,cpp}` and the new
`HomeVerseDoc.{h,cpp}`. These are outside the lock:

- `lib/Serialization/SdPaths.h`
- `lib/Serialization/PersistableStore.h` (a comment)
- `test/home_layout/`
- `test/sd_paths/SdPathsTest.cpp`
- `docs/file-formats.md`

None of them is a shared append point, and nothing is needed in `test/CMakeLists.txt` or
`src/main.cpp`.
