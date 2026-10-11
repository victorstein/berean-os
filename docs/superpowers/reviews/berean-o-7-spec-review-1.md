Tier: heavy

# berean-o-7 (R22) spec review 1

Spec: `docs/superpowers/specs/2026-10-11-berean-o-7-design.md` (at `d72df7cf`)
Research: `docs/superpowers/research/2026-10-11-berean-o-7-research.md`
Brief: `hpipe brief --task t7`

No earlier pass wrote findings, so there is no prior fix to re-check.

## What was verified and holds

These are the claims the design depends on. I re-read each one, and each is correct:

- **The gate.** `versePending = !biblePath.empty() && !HOME_VERSE.isCurrent()` (`LauncherActivity.cpp:148`).
  `onEnter` paints only when nothing is pending (`:84`). `drawVerse` draws the pick whenever
  nothing is pending (`:288-306`). So a restored, current entry removes the scan and paint #2 with
  no edit to `LauncherActivity.cpp`, as the spec claims. `HOME_VERSE` is the only `HomeVerse` user
  outside its own files (grep: `LauncherActivity.cpp:148,190,290-299,404-405,442`).
- **Determinism (Goal 3).** `ensure` is a pure function of the date seed, the file, the card box and
  the build's fonts. Nothing in `StudySleepFit.*`, `StudyPassageScan.cpp` or `StudySleepPick.h`
  reads `SETTINGS` or `I18N` (grep finds nothing). The date stays valid across deep sleep because it
  comes from an external RTC (`LocalDate.cpp:7-21`, `HalClock.h:36-41`). The builtin card fonts are
  fixed per build: SD fonts get their own hashed IDs and never replace them
  (`main.cpp:308-321`, `SdCardFontManager.cpp:43-58`).
- **A4's mechanism, checked on a real image.** `esp_app_get_elf_sha256(buf, 9)` returns 8 hex
  characters (`esp_app_desc.c:78`, `:96-104`, `:113-115`, with `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9` in
  the libs' `sdkconfig:507`). `pioarduino-build.py:262` adds `--elf-sha256-offset 0xb0`. The main
  checkout's `.pio/build/x4pro/firmware.bin` holds `740a7304f6c17dbc…` at `0xb0`, which equals
  `shasum -a 256 firmware.elf`. The `0xb0` offset is right: `0x20` holds the desc magic `0xABCD5432`.
- **A2 path.** `pubKeyFromFileName` accepts any `*.json` (`StudySleepPick.h:63-68`), and the walk
  over `PASSAGES_DIR` uses it (`StudyPassageScan.cpp:48-52`). So the file must sit at the
  `/.berean/` root.
- **A12 single task.** Arduino's `setup()` and `loop()` share `loopTask`
  (`cores/esp32/main.cpp:52-82`). Every `HOME_VERSE` call runs from `onEnter`, `loop` or a result
  handler, all of which are on that task. The web server refuses dot-named components
  (`ProtectedPath.cpp:21-23`, `FileRoutes.cpp:124`, `:179`).
- **Storage discipline.** The read is `readDocFromFileAdopting` and the write is
  `writeDocToFileAtomic`. Both match `MeetingWeekCache.cpp:22`, `:50-55` and
  `PersistableStore.cpp:104-122`, `:200-211`. There is a budget check before the write, a version
  that refuses an absent `v` (`FormatVersion.h:10-16`, `PlacesDoc.cpp:124`), and a named owning task.
  CLAUDE.md's five store rules are all met.
- **Test wiring.** `test/CMakeLists.txt:129` already adds `home_layout`. `Unit.cpp` has no
  dependencies beyond std (`Unit.cpp:1-4`). `crosspoint_test_common` links ArduinoJson 7.4.2
  (`test/CMakeLists.txt:28-33`). No shared append point is touched.
- **Expected figure.** The 4,427 → ~3.1–3.2 s estimate follows from the baseline
  (`responsiveness-baseline.md:51-52`, `:75`). The scan logged at `[3539]` ends 413 ms after the
  3,126 ms first frame. Paint #2's 257 ms and a 566 ms FAST then reach about 4,362 ms, and the
  remaining ~65 ms is unattributed. The `firstPaint` and `mode` log fields match `:364-368`.

## Findings

### MINOR 1 — A4's "within one boot the card box cannot change" is false as stated

- **Claim.** A4, first bullet: "Within one boot the card box, the fonts and the fit algorithm
  cannot change, which is why the RAM key never needed them." On that basis the design drops the
  text box from the key.
- **Problem.** The card box depends on a theme metric, and a theme switch applies immediately,
  within the boot. The box is the same under both themes only because both happen to set
  `topPadding = 5`. Research said exactly that ("so the box does not vary today") and listed the
  text box as something the spec "has to add". The spec drops it without saying that it relies on
  this coincidence. Persistence carries any future divergence across reboots: a theme switch, then
  sleep, would restore lines fitted for the other width.
- **Evidence.**
  - `verseText` width = `hero.width − 2·PAD` (`HomeLayout.h:150`), and `hero` comes from
    `MastheadLayout::band(…, metrics.topPadding, …)` (`:125-126`).
  - `BaseTheme.h:134` and `lyra/LyraTheme.h:11` both set `topPadding = 5`.
  - `SettingsActivity.cpp:181-187` reloads the theme at once ("Theme changes take effect immediately").
  - Research, "What Home's pick depends on", lines 100-107.
- **Fix.**
  - Reword A4 to say that the box is theme-independent today by construction of the metrics, not
    because it cannot change.
  - Enforce it in `test/home_layout/HomeLayoutTest.cpp`, which already has
    `layoutFor(metrics)` (`:25`). Pin `layoutFor(BaseMetrics::values).verseText` equal to the Lyra
    one, width and height. A build that diverges the themes then fails on the host before it ships a
    wrong restore. The cost is one test and no key change.

### MINOR 2 — "A wrong type … falls back to the default, which then fails validation" is not true for most fields

- **Claim.** Under the format table: "A wrong type, such as `"n":"43864"`, falls back to the
  default, which then fails validation."
- **Problem.** For several fields the default is itself a valid value, so a wrong type passes
  validation:
  - `k.n` defaults to 0, and the only check is ≤ `UINT32_MAX`;
  - `k.dt` and `k.f` default to `false`, which is a valid bool;
  - `e` defaults to 0, a pick;
  - `p.s` defaults to 0;
  - `p.rung` defaults to 0.

  Most of these only produce a key that will not match, which costs a rescan and is harmless.
  `p.s` is different: it restores a wrong spine hint, which the verse tap passes to the reader
  (`LauncherActivity.cpp:404-405`). The plan would follow the spec's sentence and add no type
  checks.
- **Evidence.**
  - The format table rows for `k.n` and `p.s`.
  - `PlacesDoc.cpp:146-150` shows the same `| default` reads, which accept a default silently.
- **Fix.**
  - In `fromJson`, require `is<T>()` for every key and pick field, and return `Malformed`
    otherwise.
  - Add a wrong-type case (`"n":"43864"`, `"s":"1201"`) to test 7.
  - Add the cross-field check `e == 0 ⇒ k.f`. A pick never comes from a missing file, because
    `HomeVerse.cpp:97` scans only when `key.fileExists`.

### MINOR 3 — The shell's save rule has no host test, although the repo has the pattern for one

- **Claim.** Testing strategy: "The shell has no host test. `HomeVerse.cpp` uses `GfxRenderer` and
  `HalStorage`; it has none today either."
- **Problem.** A7 has to save on three of `ensure`'s return paths and on neither OOM path:
  - save on the no-pick return (`HomeVerse.cpp:115`), the "does not fit" return (`:125`) and the
    pick return (`:133`);
  - do not save on the OOM returns (`:79`, `:107`).

  The once-per-boot gate (A6) and the unusable-identity guard sit beside that rule. The device run
  exercises only the pick and no-pick paths, so a save on an OOM path, or one missing on the
  `TooLong` path, would ship unseen. `HalStorage` is no longer a reason to skip a host test: the
  in-memory fake exists, and `test/places_store/` runs a real store shell against it.
- **Evidence.**
  - `test/places_store/CMakeLists.txt:1-21` builds `PlacesStore.cpp` with
    `PersistableStore.cpp` and `test/stubs/HalStorageFake.cpp`.
  - `GfxRenderer` is needed only by the scan inside `ensure` (`HomeVerse.cpp:34-37`, `:82-91`).
- **Fix.**
  - Put `restore` and `save` in a renderer-free unit (for example
    `HomeVerseFile::load/save(Entry&, buildId)`) and test it, modelled on `PlacesStoreTest`.
    Useful cases: a round trip through `.tmp` adoption, no write when the identity is unusable, and
    no file after an over-budget refusal.
  - If the spec keeps the shell untested, it should give the real reason (the renderer inside
    `ensure`) and drop the stale `HalStorage` one.

### MINOR 4 — The non-goal "the first wake of a new day … cannot be known before that day starts" is false

- **Claim.** Non-goals: "The pick is seeded by the local date (`HomeVerse.cpp:68-73`), so it cannot
  be known before that day starts."
- **Problem.**
  - The seed is a pure function of the date. Tomorrow's pick can be computed today: it needs only
    tomorrow's date, the file and the renderer, all of which exist the evening before.
  - The real reason this is out of scope is the brief's acceptance criterion, "a new day … still
    rescans".
  - It matters to the owner's expectation. The first wake of each morning is a new-day wake, so it
    keeps the 4.4 s. The spec's predicted figure applies only to the second and later wakes of a
    day, and the spec does not say so.
- **Evidence.** `dailySeed` (`StudySleepPick.h:176-183`) hashes only the date's `yyyymmdd`.
- **Fix.**
  - Restate the non-goal on the brief's ground.
  - Say in the device section and the PR body that the morning wake is unchanged.
  - File "precompute tomorrow's Home pick" as a follow-up with `hpipe discover --task t7`, not in
    this PR.

### MINOR 5 — `isCurrent()`'s own contract comment goes stale, and the spec does not list it

- **Claim.** Architecture, `HomeVerse.{h,cpp}` row: "the class comment stops saying 'nothing
  stored' (`HomeVerse.h:12-17`)".
- **Problem.** `HomeVerse.h:30-31` documents `isCurrent()` as "Costs a clock read and a file stat,
  never a parse". After this change, its first call in a boot reads and parses the file.
- **Fix.** Add `HomeVerse.h:30-31` to the row. Say that the first call of a boot restores, and that
  every later call is a clock read and a stat.

### MINOR 6 — A6 overstates why the restore races nothing

- **Claim.** A6: "That first call comes from `resolveTargets`, which `onEnter` runs before it
  requests any paint … The render task … therefore cannot read the entry while restore fills it."
- **Problem.**
  - The first `isCurrent()` of a boot can come from a result handler instead. If Home first found
    no Bible, `isCurrent()` was short-circuited. After a Bible download or a Publications return,
    `resolveTargets()` runs from the handler (`LauncherActivity.cpp:424`, `:435`).
  - The Pop path releases the `RenderLock` before it calls the handler
    (`ActivityManager.cpp:126-127`), and `onEnter` also runs unlocked (`:159-160`).
  - So the guarantee is that no render has been requested, not that one cannot run. This is the
    same exposure that `biblePath` and `layout` have today, so the change adds no new class of race.
- **Fix.** Reword A6 to "no render is requested before `resolveTargets` returns, the same
  guarantee every other member it fills relies on", and name the result-handler entry.

### MINOR 7 — Wrong IDF version for the libraries (benign)

- **Claim.** A4: "That is the 5.5.2 source on disk; the libraries are built from 5.5.0". The
  research's "Installed versions" row and its RTC section say the same.
- **Problem.** `custom_sdkconfig` makes pioarduino rebuild the Arduino libs from `framework-espidf`
  5.5.2. The libs that link are 5.5.2, which makes the cited source authoritative rather than
  approximate. `versions.txt` is the original package's file, dated Feb 11, and it is stale.
- **Evidence.**
  - `platformio.ini:94-116` sets `custom_sdkconfig`, and `arduino.py:659-661`, `:912-913` call
    `call_compile_libs()` → `espidf.py`.
  - The libs' `esp32s3/sdkconfig` is headed "ESP-IDF 5.5.2 Project Configuration" and was rebuilt
    on Oct 11.
  - `esp_idf_version.h:18` reads `ESP_IDF_VERSION_PATCH 2`.
- **Fix.** Correct the sentence in A4 and the research row. Nothing in the design changes.

### MINOR 8 — The error table and the logging section disagree

- **Problem.**
  - The error table gives "File missing → Log: none". The Logging section, though, prints
    `Verse restore: none … ms` once per boot.
  - "Atomic write fails → … the next boot rescans" is not always true. If the rename fails after the
    old file was removed, the `.tmp` holds the new entry, and the next boot adopts and restores it
    (`PersistableStore.cpp:116-120`, `:177-195`). That behaviour is correct; only the description
    is wrong.
- **Fix.**
  - Change the first row's log cell to "the restore line only".
  - Change the last row to "the next boot restores the `.tmp`, or rescans".
  - Optionally, print both identities on `other build` (`… other build (file 1a2b3c4d, running
    5e6f7a8b)`). On the device run this is the only way to tell a real build change from a
    misread.

### MINOR 9 — Two integration seams with sibling tasks go unnamed

- **Problem.**
  - "Files outside the task's lock" says none of the files is shared. But `docs/file-formats.md` is
    in t3's registered lock. t3's spec says it makes no change to that file
    (`perf-r12-bible-open: …-berean-o-3-design.md:55`, `:200`), so a textual conflict is unlikely,
    but the orchestrator may still sequence on the lock.
  - t2's device check expects `[LAUNCH] Band replayed in N ms` "on the post-verse paint" after a
    wake (`…-berean-o-2-design.md` §12, step 1). That is the paint this spec removes on a same-day
    wake. Whichever task lands second sees its sibling's device step change.
- **Evidence.** `hpipe show --task t3` lists `files: … docs/file-formats.md …`. The t2 quote is from
  the t2 worktree's spec, §12.
- **Fix.** Name both in the spec's lock section and in the PR's "For the orchestrator" section. For
  the t2 seam, its replay line will appear only on a new-day wake.

## Assessment

The design is sound and the implementation can proceed. Option A follows `MeetingWeekCache`'s
storage shell and `PlacesDoc`'s format split. Each failure falls back to today's scan. Using the
build identity is the right answer to the research's finding that "day + size is not enough
across a reboot": it needs no manual version bump, and its mechanism is confirmed on a real image.

Every finding is an accuracy fix or a hardening the spec can take inline. None reverses a decision,
changes scope or needs the owner's judgment. The nearest to a judgment call is MINOR 4's
follow-up, and the brief's acceptance criterion already settles it for this PR.

VERDICT: CLEAR
