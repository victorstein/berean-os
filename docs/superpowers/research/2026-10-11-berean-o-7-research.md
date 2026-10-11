# berean-o-7 (R22) — research: parse the verse file once per sleep cycle

Branch `perf/r22-verse-once-per-sleep`, fast-forwarded to `478cf1ed` (release 1.29.9). Between
the baseline build `1ba1cd34` and `478cf1ed` only docs, the changelog and the version changed
(`git diff --name-only 1ba1cd34 478cf1ed`), so every source line cited here is the code the
baseline measured. Every claim cites a line read in this worktree or a command run in it.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| Home's verse: the RAM singleton, its key, the scan, the fit | `src/activities/launcher/HomeVerse.{h,cpp}` |
| The cache's pure half: `Key`, `Pick`, `Entry`, `validFor`, `packLines` | `src/activities/launcher/HomeVerseCache.h` |
| When Home scans, and the two paints | `src/activities/launcher/LauncherActivity.cpp:76-85`, `:148`, `:282-307`, `:438-447` |
| The shared passage scan (both callers) | `src/activities/boot_sleep/StudyPassageScan.{h,cpp}` |
| Sampler, daily seed, splitmix | `src/activities/boot_sleep/StudySleepPick.h:108-193` |
| The sleep screen's own pick | `src/activities/boot_sleep/StudySleepScreen.cpp:227-287` |
| Wake → Home routing | `src/main.cpp:467-476`, `:522-560`, `:591-592` |
| Host tests of the cache rules | `test/home_layout/HomeVerseCacheTest.cpp`, built by `test/home_layout/CMakeLists.txt:5-21` into `HomeLayoutTest` |
| Nearest persisted-cache example | `src/network/MeetingWeekCache.{h,cpp}` (`/.berean/meeting-weeks.json`) |

## Current control flow

### Sleep

`SleepActivity` calls `study_sleep_screen::render` only when the sleep screen setting is
`STUDY` (`SleepActivity.cpp:548-550`). `render` (`StudySleepScreen.cpp:227-287`):

- builds a `Sampler` on `hardwareRandom` (`:229`) — a random pick, not a daily one;
- gates rows at the sleep screen's floor rung, at the sleep screen's width
  (`:235-240`), with the default `FIT_PREFILTER_BYTES = 4096` (`StudyPassageScan.h:23`,
  `StudySleepPick.h:30`);
- scans **every** passage file from a random start, wrapping, under a 262,144-byte total budget
  (`StudyPassageScan.cpp:31`, `:141-179`), skipping recently shown passages through the ring in
  `CrossPointState` (`StudySleepScreen.cpp:250-252`);
- logs `Study pick: … ms` (`:253-257`) — the baseline's 445 ms;
- draws, pushes a HALF (`:222`), records the key in the ring and saves `state.json`
  (`:284-285`).

The sleep path documents that it "Reads /.berean/ and never writes to it"
(`StudySleepScreen.h:7-8`, `StudyPassageScan.h:9-12`). `enterDeepSleep` then calls
`powerManager.startDeepSleep` (`main.cpp:280`), which on this board ends in
`esp_deep_sleep_start()` (`freeink-sdk/libs/hardware/PowerManager/src/PowerManager.cpp:93-96`).
Waking is a chip reset: `main.cpp:270` says so, and nothing in `.bss` survives it.

### Wake

A power-button wake resolves `BootResume` (`main.cpp:472-473`). With no Quick Resume frame on the
card, `presentBoot` sets `needsWakeRefresh` (`:550-554`) and `routeFirstActivity` calls
`activityManager.goHome(boot.needsWakeRefresh)` (`:591-592`), which constructs
`LauncherActivity(…, cleanInitialRefresh)` (`ActivityManager.cpp:242-244`). The first paint of
that entry is therefore a HALF (`LauncherRefresh.h:15-17`, `LauncherActivity.cpp:364-368`).

### Home entry

`onEnter` → `resolveTargets` (`LauncherActivity.cpp:76-85`, `:104-149`). Its last line is

```cpp
versePending = !biblePath.empty() && !HOME_VERSE.isCurrent();   // :148
```

`isCurrent()` builds a `Key` — the local date from `readLocalDate` plus `bible.json`'s
existence and byte size (`HomeVerse.cpp:39-53`) — and compares it with the held entry
(`:62`, `HomeVerseCache.h:40-43`, `:65`). After any reset the singleton is default-constructed
(`HomeVerse.cpp:57-60`, `HomeVerseCache.h:57-58`, `valid = false`), so on every wake
`isCurrent()` is false and `versePending` is true.

With the verse pending, `onEnter` does not request a paint (`:82-84`). `loop()` then
(`:438-447`):

1. `requestUpdateAndWait()` — paint #1, verse card label only (`drawVerse` returns early when
   pending, `:287-288`); on wake this is the HALF;
2. `HOME_VERSE.ensure(…)` — the scan (`HomeVerse.cpp:64-134`), logging `Verse scan: … ms`
   (`:98-101`) — the baseline's 405 ms;
3. `requestUpdate()` — paint #2, a FAST, now with the verse.

That is the baseline's "verse scan 405 ms + second paint 257 ms" plus a ~566 ms FAST
(`responsiveness-baseline.md:51-52`): first frame at 3,126 ms after reset, settled at 4,427 ms,
from **one** clean capture.

If `isCurrent()` is true on entry, `versePending` is false, `onEnter`'s `requestUpdate()`
(`:84`) is the only paint, and `drawVerse` draws the pick in it (`:290-306`). The second paint
needs no code change to disappear: it follows from `isCurrent()` alone.

## What Home's pick depends on

`ensure` is a pure function of its inputs; nothing random enters it:

| Input | Where | In today's `Key`? |
|---|---|---|
| Local date → seed (`dailySeed`, or `FALLBACK_SEED` undated) | `HomeVerse.cpp:68-73`, `StudySleepPick.h:178-183`, `HomeVerseCache.h:29` | yes (`dated`, `day`) |
| `bible.json`'s contents, in file order | `StudyPassageScan.cpp:181-199` | only its **size** (`fileExists`, `fileBytes`) |
| Card text box, width and height | `HomeVerse.cpp:82-90`, `LauncherActivity.cpp:442` | no |
| Fonts and their metrics (`NOTOSERIF_14/12`, `UI_10`) | `HomeVerse.h:21-24` | no |
| `PREFILTER_BYTES`, the sampler and fit algorithms | `HomeVerseCache.h:22`, `StudySleepPick.h:108-193` | no |

Within one boot the last three cannot change, which is why the RAM key never needed them.
Across a reboot they can: an OTA or cable flash ships new fonts or layout. A persisted pick
keyed only on day + size would then draw lines fitted for another build's metrics. The card
box today is `screenWidth − insets − 2·topPadding − 2·PAD` wide (`HomeLayout.h:125-150`,
`MastheadLayout.h:22-27`) and `VERSE_TEXT_LINES × serif12` tall (`HomeLayout.h:99`).
`topPadding` is a theme metric and the theme is a user setting (`SettingsList.h:279-280`), but
both themes set it to 5 (`BaseTheme.h:134`, `LyraTheme.h:11`), so the box does not vary today.

So the brief's "same key `HomeVerse` uses now" is necessary but not sufficient for a
persisted pick. The spec has to add the text box and something that changes with the build,
or re-check the stored lines against the current fonts on restore.

`HomeVerse.h:12-14` currently says the date seed means "every wake of the same day shows the
same verse with nothing stored". Persisting the pick changes that comment, not the visible
behaviour: on an unchanged file the stored pick is the pick a rescan would make.

## The three ways to skip the wake scan

### A. Persist Home's own result to the SD card (recommended for the spec)

Write the `Entry` after `ensure` scans; restore it on the first `isCurrent()`/`ensure()` of a
boot. The nearest example is `MeetingWeekCache` — a cache, not study data, under `/.berean/`
(`MeetingWeekCache.h:9-19`):

- `load` reads with `readDocFromFileAdopting` and refuses an unknown `"v"`
  (`MeetingWeekCache.cpp:18-36`, `FormatVersion.h:14-16`);
- `save` checks `measureJson` against a budget, then `writeDocToFileAtomic` (`:38-56`);
- both go through `HalStorage`: `writeDocToFileAtomic` streams to `<path>.tmp` and renames
  (`PersistableStore.h:62-68`, `PersistableStore.cpp:113-118`), and `readDocFromFileChecked`
  uses `Storage.exists` / `Storage.readFile` (`PersistableStore.cpp:124-136`). A file this size
  is far under the 50,000-byte `readFile` cap (`SaveBudget.h:19`).

Its pure format half would mirror `PlacesDoc` (`src/util/PlacesDoc.h:14-48`: format version,
caps and a byte budget derived from them, no storage access), tested on the host like
`test/places_doc`, which links ArduinoJson (`test/places_doc/CMakeLists.txt:28-32`;
`test/CMakeLists.txt:28-33` pins v7.4.2). A new path goes in `SdPaths.h` next to
`MEETING_WEEKS_FILE` (`lib/Serialization/SdPaths.h:24`, `:50`) and is pinned by hand in
`test/sd_paths/SdPathsTest.cpp:24-37`. `docs/file-formats.md` documents `/.berean/places.json`
(`:487-511`) but not `meeting-weeks.json`; the new file gets a section.

Owning task: the loop task, which already owns `HomeVerse` (`HomeVerse.h:17`) and runs
`ensure` (`LauncherActivity.cpp:442`). The write happens once per real scan — a new day, a
tag edit that changes the file size, or a new build — never on a same-day wake.

### B. Have the sleep pass produce Home's pick (rejected)

- The sleep pass offers each row to one sampler through one gate (`StudyPassageScan.cpp:68-74`,
  `:97`). Home needs a different seed (`HomeVerse.cpp:68-74`), a different width and floor rung
  (`:82-90`) and an eighth of the prefilter (512 against 4096, `HomeVerseCache.h:22`), so the
  shared scan's interface would have to take two samplers and two gates.
- `pickFromAll` starts at a random file and stops at a 256 KB budget (`StudyPassageScan.cpp:31`,
  `:109-112`, `:160`); `bible.json` is not guaranteed to be reached.
- It breaks the documented "never writes to /.berean/" contract of the sleep path
  (`StudySleepScreen.h:7-8`), and it would still need A's file, or C, to reach the wake.
- It computes the pick for the **sleep** date. A wake on another day rescans anyway, so it
  saves nothing A does not, and it adds Home's measuring to the 445 ms sleep pass.

### C. Keep the entry in RTC memory across deep sleep (considered; not proposed)

This would need no SD I/O and no on-disk format. The evidence that it could work:

- On the X4 Pro sleep is `esp_deep_sleep_start()` (`PowerManager.cpp:93-96`); the GPIO13
  battery cut is compiled only when `!SOC_PM_SUPPORT_EXT1_WAKEUP` (`HalPowerManager.cpp:78-90`),
  and the S3 defines it as 1
  (`framework-arduinoespressif32-libs/esp32s3/include/soc/esp32s3/include/soc/soc_caps.h:484`).
- ESP-IDF keeps RTC slow memory powered in deep sleep when any RTC data is linked
  (`framework-espidf/components/esp_hw_support/sleep_modes.c:2585-2594`). `_rtc_slow_length`
  spans `.rtc_noinit` (`esp32s3/qio_opi/sections.ld:91`, `:121-125`, `:140`, `:166-168`), the
  RTC data region is slow memory (`esp32s3/sdkconfig:2215`, `esp32s3/ld/memory.ld:91-92`, `:106`),
  and this firmware already links `RTC_NOINIT_ATTR` data (`main.cpp:138-139`,
  `HalSystem.cpp:17-21`, `Logging.cpp:12-19`).
- The bootloader skips reloading RTC segments after a deep-sleep reset
  (`framework-espidf/components/bootloader_support/src/esp_image_format.c:905`, `:919-936`).
  That is the 5.5.2 source on disk; the Arduino libraries were built from `release/v5.5
  87912cd291` (`esp32s3/versions.txt:2`).

It is not proposed because no code here relies on RTC data **surviving deep sleep**. Every
existing `RTC_NOINIT_ATTR` user relies on surviving `ESP.restart()` or a panic (`main.cpp:137`).
The RTC slow region is 7,680 bytes (`memory.ld:91-92`), and this repo's own RTC data already
takes about 5.5 KB of it: logs 16 × 256 (`Logging.cpp:8-12`), panic text 256 and 32 × 36-byte
frames (`HalSystem.cpp:14-18`, `HalSystem.h:7-10`), plus small counters. The framework's share
was not measured. Retention can only be proven on the device. Choosing C would set a new
pattern, which is the owner's call, so it is recorded here only as an alternative.

## Expected effect, and the lines that show it

For a same-day wake under A, `resolveTargets` sees `isCurrent()` true, so there is one paint
(HALF) with the verse in it, no `Verse scan`, and no second paint. By the baseline's arithmetic,
settled ≈ first frame ≈ 3.13 s, against 4,427 ms. The ~1.3 s saved is the 405 ms scan, the
257 ms second paint, the ~566 ms FAST and ~70 ms between them that the baseline does not
attribute. Two costs move into the first paint and were not measured: the restore (one file
under 2 KB, read and parsed) and drawing up to four lines of serif italic. The 4,427 ms figure
is n = 1, so variance is unknown.

Lines on the serial log (`LOG_LEVEL=2` on `x4pro`, `platformio.ini:174`):

- before: `[HOMEV] Verse scan: … ms` (`HomeVerse.cpp:98`), then a second
  `[LAUNCH] Paint: clean=1 firstPaint=0 mode=FAST` (`LauncherActivity.cpp:366-367`);
- after: no `Verse scan`, a single `[LAUNCH] Paint: clean=1 firstPaint=1 mode=HALF`, and its
  `  Wait complete:  8179_DRF (… ms)` (`freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:293`,
  `:375`) as the settled timestamp. A restore log line with its own `ms` is the spec's to add.

The first wake of a new day, and the first Home entry after a tag edit that changes the file's
size, still scan and still paint twice. That is unchanged.

## Installed versions

| Tool / package | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| Platform | pioarduino espressif32 55.03.37 | `platformio.ini:15`, `~/.platformio/platforms/espressif32/platform.json` |
| Arduino core | 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json` |
| Arduino libs (ESP-IDF) | 5.5.0, `release/v5.5 87912cd291` | `framework-arduinoespressif32-libs/package.json`, `esp32s3/versions.txt:2` |
| ESP-IDF source on disk | 5.5.2 (`framework-espidf` 3.50502) | its `package.json` |
| ArduinoJson | 7.4.2 (device and host) | `platformio.ini:155`, `test/CMakeLists.txt:28-33` |
| googletest | v1.17.0 | `test/CMakeLists.txt:14-18` |
| Host toolchain | CMake 4.4.2, Apple clang 21.0.0 | `cmake --version`, `c++ --version` |

## For the spec

- **Files beyond the registered lock.** The task registered `src/activities/launcher/HomeVerse`,
  `src/activities/boot_sleep/StudySleepScreen` and `test/study_sleep_pick/` (`hpipe show --task
  t7`). Option A also touches `test/home_layout/` (the suite with the existing verse tests),
  `lib/Serialization/SdPaths.h`, `test/sd_paths/SdPathsTest.cpp` and `docs/file-formats.md`.
  Neither `StudySleepScreen` nor `test/study_sleep_pick/` needs to change. Keeping the restore
  inside `HomeVerse` leaves `LauncherActivity.cpp` untouched, which siblings t2 (R9, cover band)
  and t3 (R12, Home ↔ Bible) are likelier to edit.
- **Restore point.** `isCurrent()` is `const` and is the first call on entry (`:148`). The
  render task paints from a different task (`ActivityManager.h:52-54`, `:108-112`), but
  `resolveTargets` runs before `onEnter` requests any paint (`:79-84`), so filling the entry
  there races nothing.
- **Write timing.** Writing inside `ensure` delays paint #2 by the write. Paint #2 also streams
  the cover from SD (`CoverBand::draw`, `LauncherActivity.cpp:248`) under the same
  `storageMutex`.
- **The undated case.** An undated key matches any day (`HomeVerseCache.h:40-43`,
  `HomeVerseCacheTest.cpp:53-61`). Persisting it shows the same pick until the file or build
  changes, which is also what today's deterministic fallback seed would rescan to.

## Review tier

Raised from `standard` to `heavy`. Option A adds a persisted file under `/.berean/` — data-dev's
surface (`.claude/agents/data-dev.md`: "anything under /.berean/"), with a new `SdPaths` constant
and a `docs/file-formats.md` section. It also adds a format version that future builds must refuse,
which is an on-disk contract, and research showed the key has to grow beyond what the brief named.
