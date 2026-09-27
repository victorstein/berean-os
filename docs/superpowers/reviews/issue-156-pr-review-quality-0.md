Tier: heavy

# PR #161 — code quality review, pass 0

Scope: the code in `gh pr diff 161`. That is `LauncherBible.h`,
`LauncherActivity.{h,cpp}`, the new `BibleDownloadActivity.{h,cpp}`, the
registration hook in `EpubReaderActivity.cpp`, the two translation YAMLs, and
`test/launcher_bible/LauncherBibleTest.cpp`. The docs under
`docs/superpowers/` are out of scope for this pass.

Verification: I rebuilt `build/test/launcher_bible/LauncherBibleTest` on the
host (`cmake --build build/test --target LauncherBibleTest`), and all 18 tests
pass. I did not run `pio`, as the brief asked.

## Pattern fidelity

- **`BibleDownloadActivity` follows `MeetingDownloadActivity` on purpose.** The
  spec says so at `docs/superpowers/specs/2026-09-27-issue-156-design.md:65,242`.
  Several pieces match their siblings line for line:
  - The class shape: `Activity` plus `private UiAppHost`, an
    `optional<WifiSession>` declared last, and `emplace` in `onEnter`.
  - The hook trampolines.
  - The progress throttle constants.
  - The repaint-throttled `onDownloadProgress`
    (`BibleDownloadActivity.cpp:189`, compare `MeetingDownloadActivity.cpp:234-258`).
  - `screenHeader`, `buildProgressScreen` (`:337`, compare
    `MeetingDownloadActivity.cpp:332-375`), and `render`.

  `CatalogSearchActivity.cpp:33-34,431-460` also has its own copy of the
  progress hook. The repo's established approach is one copy per activity, and
  this PR does not introduce a second way of doing it.
- **`buildDialog` (`BibleDownloadActivity.cpp:302`) is `BibleSearchActivity::buildDialog`**
  (`BibleSearchActivity.cpp:744-782`) without the single-button branch. The spec
  names that function as its model.
- **The `Result` handling (`BibleDownloadActivity.cpp:242-268`) is the
  established shape.** It is an exhaustive `switch` with a trailing fallback,
  the same idiom as `publication::failureMessage` (`PublicationDownloader.cpp:246-261`).
  `Cancelled` uses the same `goHomeAfterCancel ? onGoHome() : finish()` branch
  as `MeetingDownloadActivity.cpp:293-299`.
- **The launcher side is consistent.** `openBible` uses `makeUniqueNoThrow` with
  a `LOG_ERR` on null (`LauncherActivity.cpp:576-580`). The result handler
  re-resolves and repaints. The reader hook reuses the file's existing `"ERS"`
  log tag (`EpubReaderActivity.cpp:139`).
- **The host-testable helpers keep firmware out of `LauncherBible.h`.** Their
  callers inject I/O as lambdas, the way `isCdnNamedCopyOf` already works.

## Findings

### MINOR 1 — Four comments narrate history instead of describing the merged state

The root `CLAUDE.md` asks for comments that are "written for the merged state"
and that "remove before/after narration, investigation measurements".

- `LauncherActivity.cpp:204-205`: "The existence check is what the pre-#104
  version lacked…"
- `LauncherActivity.h:76`: "The pre-#104 guess over recently opened books…"
- `LauncherBible.h:75`: "The pre-#104 recents match, kept exactly as it was…"
- `LauncherActivity.cpp:110,113`: "since it began registering on open" and
  "before registration existed"

Each one has a real "why" under the narration. For example: the recents match
can take a non-Bible titled "New World", so it runs last; a deleted file can
still be listed in `recent.json`, hence the `exists` check. Keep the reason and
drop the PR-history framing.

`BibleDownloadActivity.cpp:36` ("Measured 2026-09-27: 14,945,282 B…") is on the
edge. It is a measurement, but it is also the only justification for the
constant `15`, so it can stay.

### MINOR 2 — `std::make_unique` for the Wi-Fi picker, next to `makeUniqueNoThrow` in the same PR

`BibleDownloadActivity.cpp:145` builds `WifiSelectionActivity` with
`std::make_unique`. Under `-fno-exceptions`, `std::make_unique` aborts on OOM.
The `CLAUDE.md` rule says "Never write a bare `new` for any fallible
allocation". The same PR uses `makeUniqueNoThrow` for the activity one level up
(`LauncherActivity.cpp:576`).

The line is copied verbatim from `MeetingDownloadActivity.cpp:67`, so it
mirrors a sibling rather than inventing something. It is still the one place
where the new code falls below its own standard. The fix is small: build the
picker with `makeUniqueNoThrow`, and on null, call `fail(tr(STR_NO_WIFI_CONNECTION))`
or log and stay in Confirm.

### MINOR 3 — Two registration tests promise more than they check

- `ARefusedWriteIsReportedAndNotRetried` (`LauncherBibleTest.cpp:172`) calls
  `open` only once, so "not retried" is only true inside that single call. The
  real behaviour across opens is the opposite. A refused path is still
  unregistered, so the next open of that Bible calls `record` again. That retry
  is the correct behaviour, but the test name claims the reverse. A better name
  would be "…IsReportedAndLeavesNoEntry". Alternatively, add a second `open` and
  assert `recordCalls == 2` to pin the retry.
- `ANonBibleNeverTouchesTheRegistry` (`LauncherBibleTest.cpp:139`) creates a
  `FakeRegistry` only to borrow its `recordCalls` field, then passes its own
  lambdas. The fixture above (`:119`) goes unused. Either give `FakeRegistry` a
  `lookupCalls` counter and call `registry.open(false, …)`, or drop the fixture
  from this test and use two plain counters.

### MINOR 4 — `chooseFileRequested` can latch outside Confirm

At `BibleDownloadActivity.cpp:118`, the flag is cleared only when
`state == State::Confirm`. `ACTION_CHOOSE_FILE` is only ever wired to a button
in the Confirm dialog, so today the guard never fails. If it ever did, the flag
would stay set forever. Other flags in this loop, such as `dismissRequested`
and `startRequested`, are consumed without a state condition. Either drop the
redundant state check so this flag is consumed the same way, or clear the flag
before checking the state.

## Not findings

- **The fourth copy of the progress-hook and progress-screen code.** Moving it
  into a shared helper would touch three other activities. That is out of this
  PR's scope, and the spec's choice to copy rather than extract is deliberate.
- **`errorMessage` is a `const char*` here but a `std::string` in the
  sibling.** The header documents why (`BibleDownloadActivity.h`: "Always a
  tr() string, which outlives the activity"), and the pointer avoids a heap
  copy.
- **`registerBibleIfUnknown` lives in a header named `Launcher…`.** The header
  comment was updated to say that the reader uses it too, and the spec placed
  it there so the one host test target covers it.

VERDICT: CLEAR
