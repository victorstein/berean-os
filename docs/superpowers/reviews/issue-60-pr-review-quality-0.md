# PR #70 — code quality review 0

Branch: `fix/60-input-layer-traps` · PR: #70 · Stage: quality (stage 2)
Scope reviewed: `git diff origin/main...HEAD -- src test` — 4 source files, 1 test
file, 75 insertions, 2 deletions. Intent was settled in
`docs/superpowers/reviews/issue-60-pr-review-intent-0.md` (CLEAR) and is not
re-opened here.

## What I verified, not just read

- `cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test -j`
  → **593/593 passed**, 0.39 s. The new case is `NavKeyGestures.APageTurnAtThe…`.
- `./bin/clang-format-fix` over the **whole tree** (not `-g`) → exit 0, no files
  changed, `git status --short` clean afterwards.
- `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` → 32 languages,
  420 keys, **1 unused** — identical to the figure the PR body claims for `main`,
  which confirms the "the `StrId` names stay in source text on purpose" comment in
  `src/SettingsList.h:349-353` is doing the job it says it is.
- No added line exceeds the 120-column limit in `.clang-format:133`.
- Every `file:line` cited inside the new comments was opened and matched:
  `BoardConfig.h:1396` (the four `PIN_UNASSIGNED` front pins),
  `InputManager.cpp:246` (`pin >= 0`), `HalGPIO.cpp:186-205` (`synthesisedEdge`),
  `HalGPIO.cpp:236-239` (`getHeldTime` substitution), `NavKeyGestures.cpp:29`
  (the release-time decision), `ReaderUtils.h:84-94` (swipe returns before
  `heldMs`), `EpubReaderActivity.cpp:451-459` (long press consumed outside the
  menu zone). No stale or invented citation.

## Findings

Three MINORs, all in the one new test. No BLOCKERs, no MAJORs.

### MINOR 1 — the new test's rationale comment states something the code contradicts

`test/nav_key_gestures/NavKeyGesturesTest.cpp:146-152` and the failure message at
`:158-159` both claim 701 ms is the *only* reachable duration:

    // ... 701 ms is the shortest hold a
    // user could aim at, and it still resolves to a page turn ...
    << "701 ms must still be a page turn: anything longer becomes Back, so this is the only "
       "duration at which a button could reach SKIP_HOLD_MS (700)";

`NavKeyGestures.cpp:29` resolves a release to `Synth` only at
`elapsed >= HOLD_MS`, and `HOLD_MS` is 850 (`NavKeyGestures.h:41`). So **every**
release in [701, 849] — a 149 ms band, not a single value — is still a page turn
whose real duration exceeds `SKIP_HOLD_MS` = 700. 701 ms is the *shortest* such
duration, not the only one, and "anything longer becomes Back" is false for the
other 148 ms of that band.

The test itself is correct and 701 is the right boundary to pin; only the prose
overstates. In a repo whose `CLAUDE.md` makes a cited claim the unit of evidence,
a comment that a reader will later trust and act on should not be wrong.
Suggested: "701 ms is the shortest hold that exceeds `SKIP_HOLD_MS`; every
release below `HOLD_MS` (850) resolves the same way, so this pins the boundary."

### MINOR 2 — the test asserts less than its own name and message promise

`:160` is the assertion carrying the conclusion:

    EXPECT_TRUE(g.reportingSyntheticHeldTime()) << "so the held time the reader sees is SYNTHETIC_HELD_MS, never 701";

`NavKeyGestures.cpp:60-62` returns true for **any** resolved event, `Page`
included, so this flag is already true for the plain 200 ms tap in
`ATapResolvesToAPageTurnOnRelease` (`:18-24`). On its own it does not show the
substituted value is *below* `SKIP_HOLD_MS` — which is the entire premise of the
gate this test exists to justify. That bound is only pinned transitively, by
`EXPECT_LT(SYNTHETIC_HELD_MS, 400u)` at `:138`, whose stated reason is a
different threshold (`BOOKMARK_HOLD_MS`); if that line is ever relaxed to 700+
for bookmark reasons, nothing fails and the gate quietly loses its evidence.

One line makes the test prove its own claim:

    EXPECT_LT(NavKeyGestures::SYNTHETIC_HELD_MS, 700u) << "below SKIP_HOLD_MS";

That is the same shape as `ThresholdsSitBetweenTheReadersOwnHolds` (`:141-144`)
immediately above it, so it costs nothing in consistency.

### MINOR 3 — the board-capability test is inlined where siblings use a named predicate

`src/activities/settings/ButtonRemapActivity.cpp:31-32`:

    const auto& frontPins = BoardConfig::ACTIVE.input;
    if (frontPins.back < 0 || frontPins.confirm < 0 || frontPins.left < 0 || frontPins.right < 0) {

The `< 0` spelling is fine and precedented in this repo's own code —
`HalGPIO.cpp:294` and `:345`, `HalPowerManager.cpp:26` all test pins that way, so
please do **not** "fix" it to `!= PIN_UNASSIGNED`.

What is slightly off-pattern is the shape. Board capability questions in this
tree are named predicates: `BoardConfig.h:1622-1625` and `:1690-1695`
(`hasTouch`, `hasHomeKey`, `hasPwmFrontlight`, `hasLeds`, …), two of which
`HalGPIO` re-exports at `HalGPIO.cpp:243` and `:245`. Here the same kind of
question is answered by a four-field expression inside an activity, and the
comment at `:28` sends the reader to a *different* file
(`MappedInputManager::getPressedFrontButton`) for the reasoning — so the fact and
its explanation now live apart, and the next caller of that scan has to re-derive
the identical four-field test. A `hasFrontButtonQuad()` on `MappedInputManager`
(beside the scan it guards) or on `HalGPIO` (beside `hasTouch()`/`hasHomeKey()`)
would keep both in one place.

This is a placement preference on a four-line guard with one caller, not a
defect; the code is correct either way.

## What is right, and worth recording so it is not "fixed" later

- **The gate mirrors its precedent exactly.** `src/CrossPointSettings.h:25-45`
  reproduces the `BEREAN_CAP_ROTATION` block directly above it (`:8-24`, from
  `d8e92208`) field for field: `#ifndef` guard, `#if FREEINK_DEVICE_X4PRO`,
  `#error` rather than a default, the same "a future board must answer this
  question for itself" sentence. `src/SettingsList.h:350-364` wraps the row in
  `#if` at column 0 exactly as the pre-existing `#if BEREAN_CAP_ROTATION` and
  `#if FREEINK_CAP_TOUCH` around it do. This is one pattern used twice, not two
  patterns.
- **The error shape is the established one.**
  `ButtonRemapActivity.cpp:22-36` is `Activity::onEnter()` → check → `LOG_ERR` →
  `finish()` → `return`, which is `ReaderActivity.cpp:41-48` verbatim in shape.
  Calling `finish()` from `onEnter` is explicitly supported —
  `ActivityManager.cpp:161-164` unlocks before `onEnter()` and comments that
  "onEnter may request another pending action, we will handle it in the next loop
  iteration". `grep -n NDEBUG platformio.ini` is empty, so `assert` would be live
  in the release env; `LOG_ERR` + `finish()` is the correct choice per
  `CLAUDE.md`'s error-handling case 1, and it is the choice made.
- **Comment length matches house style, not a deviation from it.** The long
  blocks at `CrossPointSettings.h:25-38` and `MappedInputManager.cpp:378-391`
  (including the ALL-CAPS lead) are the same register as `NavKeyGestures.h:1-26`
  and `BoardConfig.h:1390-1395`. They carry non-obvious mechanism and the reason a
  special case exists — what `CLAUDE.md` says a comment must earn its place with —
  not restatement of the next line.
- **The replaced comment is not a loss.** `MappedInputManager.cpp:378-379` keeps
  the substance of the two lines it deletes ("bypasses remapping so the remap
  activity can capture physical presses") in one sentence, then adds what was
  missing.
- **Nothing dead or commented-out was added.** The `#if`-disabled rows in
  `SettingsList.h` are kept for a stated, verified reason (`gen_i18n.py` scans raw
  text — confirmed above by the unchanged 420/1 figure), which is the same reason
  `d8e92208` gave.
- **No duplication.** The new test does not restate
  `ASynthesisedEventReportsAShortHeldTime` (`:132-139`): that one covers the
  `Synth` branch, this one covers the `Page` branch at the threshold. Naming,
  placement and the `run()` helper all follow the file.

## Verification for the human

The three MINORs above are text-and-one-assertion edits; after applying them,
`ctest --test-dir build/test -j` is the whole check (no firmware rebuild is
needed — nothing outside `test/` would change, and MINOR 3, if taken, is a
refactor that does need `pio run -e x4pro`). The device-side items the PR body
lists under "Not verified" remain the human's to confirm.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
