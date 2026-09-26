# Issue #101 spec review, pass 1

Reviewed: `docs/superpowers/specs/2026-09-26-issue-101-design.md` at `a09e0dba`. It was measured
against issue #101 (`gh issue view 101 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-26-issue-101-research.md`, and pass 0
(`docs/superpowers/reviews/issue-101-spec-review-0.md`).

## Prior-pass fixes, re-checked

- **B1 → A10 / d2 (load `WIFI_STORE` at boot).** The fix works and is fully applied.
  - `WIFI_STORE`'s only load is `WifiSelectionActivity.cpp:106` (`grep -rn 'loadFromFile()' src`).
    The hotspot path goes straight to `startAccessPoint()` (`CrossPointWebServerActivity.cpp:170-174`).
    `POST /api/wifi` add then reaches `addCredential` → `saveToFileAtomic()`
    (`CrossPointWebServer.cpp:1402`, `WifiCredentialStore.cpp:125`).
  - Update and remove look the entry up first (`:1387`, `:1434`), so they do nothing on an empty store. A10's parenthetical is correct.
  - A boot load puts every save behind a load. `SETTINGS`, `APP_STATE` and `RECENT_BOOKS` load at `main.cpp:411-413`, and every save in `main.cpp` (`:222,263,521,577`) runs from loop-time paths after `setup()`.
  - The boot load is also safe to do that early. The obfuscation key is the eFuse MAC (`ObfuscationUtils.cpp:23`, `esp_efuse_mac_get_default`), not `WiFi.macAddress()`, so decoding passwords before WiFi starts does not corrupt them and trigger a destructive resave.
  - The single-task claim holds. `handleClient()` runs from the activity loop (`CrossPointWebServer.cpp:279-299`, called from `CrossPointWebServerActivity.cpp`), and no `xTaskCreate` exists under `src/network`.
  - A2's reference to A10, the error table row, device-test step 7 and the d2 record all agree with one another.
- **m1.** The string-`"v"` case is now a planned test (`AStringVersionReadsAsTheDefault`). The `operator|` claim checks out against ArduinoJson 7.4.2: `VariantOperators.hpp:35-40` returns the default unless `is<T>()`.
- **m2.** `CrossPointSettings.h:427`, `SettingsSave.h:8-10` and `RecentBooksDoc.h:46` are now on the comment list. The nine hand-written keys are at `CrossPointSettings.cpp:86-104`.
- **m3.** The flag's placement is now unambiguous: `PersistableStoreBase`'s protected block, under `resaveRequested` (`PersistableStore.h:44-46`). `saveToFileAtomic() const` can read it under `storeMutex`, and `loadFromFile()` can write it.

## Checked and accurate

- The table of `toJson`/`fromJson` line ranges.
- All four `fromJson`s return `true` unconditionally.
- `loadFromFile`'s early return on a non-`Ok` read (`PersistableStore.h:186-188`), and the resave only when `ok` (`:195`).
- The `DeleteTempReportEmpty` → `Missing` mapping (`TempAdoption.h:53-55`).
- No `"v"` key collision (`grep -rn '"v"'` matches only `BookmarkDoc.cpp`).
- The error-handling citations: `SettingsSave.h:15-19`, `CrossPointWebServer.cpp:1292-1295`, and `sendCredentialEditFailure` at `:1339`.
- The `test/save_budget` model (`CMakeLists.txt` links only `crosspoint_test_common` + gtest).
- `18 + 9 + 10*52 = 547`, and `11421 + 6 = 11427 < 45000`.
- The completion section in `docs/file-formats.md` is at `:350-376`, and the next section starts at `:377`.
- Among the other branches that touch `src/main.cpp`, `fix/113` edits the hunk at `:402-407`, next to `:411-413`. The spec already requires a re-read and serialisation.
- Nothing else in `src` or `lib` writes or deletes the four files outside `PersistableStore` (`grep` for the four filenames).

The guard design is sound:

- A3/A9 is a pure transition function.
- The flag is set only on an `Ok` read that `fromJson` rejects, and cleared on `Missing`.
- It is checked under `storeMutex` in both save paths.

No BLOCKER or MAJOR findings.

## MINOR

### m1. A second pinned figure in `RecentBooksDocTest` moves, and the spec names only one

**Claim.** Testing item 2 says the worst-case test goes red and then green again once `DOC_WRAPPER_BYTES` is 18, and that "The pinned `11421u` (`:50`) moves to `11427u`." The architecture row says the same: "pinned budget 11421 -> 11427".

**Problem.** `DocumentOverheadMatchesTheMeasuredConstants` builds ten empty entries through the real `toJson`. It pins the sum a second time, as `EXPECT_EQ(expected, 541u)` (`test/recent_books_doc/RecentBooksDocTest.cpp:42`). `expected` is computed from `DOC_WRAPPER_BYTES` (`:38-39`). Once that constant is 18, the sum is `18 + 9 + 520 = 547`, and the test stays red after the fix the spec describes. Research §5 misses it too. The implementer finds out from a failing test, not from the plan.

**Evidence.** Run `grep -rn '541u' test` → `test/recent_books_doc/RecentBooksDocTest.cpp:42:  EXPECT_EQ(expected, 541u);`.

**Fix.** In testing item 2 and in the architecture row, add "the pinned `541u` (`:42`) moves to `547u`" next to the 11421 → 11427 move.

### m2. `CrossPointState.h:32-33` has a second count that becomes false

**Claim.** The comment rewrites include `CrossPointState.h:32`, "11 keys" → 12.

**Problem.** The same sentence goes on to say "two 16-element uint16_t arrays, seven scalars, and two SD path strings". The seven are `recentSleepPos`, `recentSleepFill`, `recentOverlaySleepPos`, `recentOverlaySleepFill`, `readerActivityLoadCount`, `lastSleepFromReader` and `showBootScreen` (`CrossPointState.cpp:48-56`). `"v"` makes eight. Changing only "11 keys" leaves the comment inconsistent with itself.

**Evidence.** `src/CrossPointState.h:32-33`; `src/CrossPointState.cpp:43-57`.

**Fix.** Rewrite the comment as "12 keys, … eight scalars …". The architecture row needs the same change.

### m3. The #99 dependency status is stale

**Claim.** Testing item 4 says: "As of this spec, #99 has only a research note on `origin/fix/99-halstorage-test-fake` (`beef81cd`). No fake exists."

**Problem.** That branch now carries a design spec: `git log origin/fix/99-halstorage-test-fake` → `c479e22e docs: design spec for issue #99 HalStorage host fake`, which adds `docs/superpowers/specs/2026-09-26-issue-99-design.md`. It has not landed and no fake exists yet, so the spec's conclusion holds. The design does resolve each of the three blockers item 4 names:
- a `String` in `test/stubs/Arduino.h` (its A-11);
- a link body for `deobfuscateFromBase64` (A-12);
- a `Storage` singleton in the rewritten `test/stubs/HalStorage.h` (A-1/A-2).

It also puts its suite in `test/storage_io`, not `test/persistable_store`, so the two suites do not collide.

**Fix.** Update the sentence to cite `c479e22e`. Say that #99's planned fake is the one item 4 targets, and that the "surface as a decision at `implement`" rule still applies if #99 has not merged by then.

### m4. Both new log lines say "newer" for versions that are not newer

**Claim.** The `fromJson` log is `"Refusing %s: format v%d is newer than v%d"`. The save guard's log is `"Refusing to save %s: its format is newer than this build"` (A3, Data and control flow).

**Problem.** A1 also refuses `0` and negatives, which are not newer. For `"v":0`, the first line would print "format v0 is newer than v1". The serial log is how the tester and the owner diagnose a refused store (error table row 2, device test step 7).

**Evidence.** Spec A1 (`version <= 0 || version > FORMAT_VERSION` is refused); the Load pseudo-code; A3's log string.

**Fix.** Use wording that is true for both cases. For example: `"Refusing %s: unknown format v%d (this build knows v1..v%d)"` and `"Refusing to save %s: its format is unknown to this build"`.

VERDICT: CLEAR
