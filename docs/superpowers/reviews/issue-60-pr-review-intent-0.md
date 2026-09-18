# PR #70 — intent review 0

Branch: `fix/60-input-layer-traps` · PR: #70 · Issue: #60
Spec: `docs/superpowers/specs/2026-09-17-issue-60-design.md`
Plan: `docs/superpowers/plans/2026-09-17-issue-60-plan.md`
Diff reviewed: `git diff origin/main...HEAD` (2 code commits, `9ce40fc1` and `ab7dc7c4`)

Reviewed for intent only: issue coverage, spec coverage, scope drift in either
direction, test honesty, and unexplained divergence from the plan.

---

## Findings

None at BLOCKER, MAJOR or MINOR.

---

## What was checked, and against what

### Issue #60 acceptance

**Finding 1 — `STR_LONG_PRESS_BEHAVIOR` promises a button it cannot use.** The
issue offered two remedies ("either reword it, or route the synthesised path").
The PR does neither: it compiles the row out. That is a deviation from the
issue's own menu, and it is *not* silent — spec §0 records it as a human
ratification on 2026-09-17, states the two alternatives as rejected rather than
deferred, and the PR body carries the same reasoning. The premise that forced
it is verifiable in the tree: `NavKeyGestures.cpp:29` resolves a release into
`Page` or `Synth` and nothing else, and `HalGPIO.cpp:236-238` substitutes
`SYNTHETIC_HELD_MS` whenever `reportingSyntheticHeldTime()` is true — which
`NavKeyGestures.cpp:60-62` makes true for `Page` as well as `Synth`. So no nav
key can page *and* report over `SKIP_HOLD_MS = 700`
(`src/activities/reader/ReaderUtils.h:18`). The touch half holds too:
`ReaderUtils.h:84-94` returns before `heldMs` is assigned in swipe mode, and
`EpubReaderActivity.cpp:451-458` consumes a long press anywhere outside the
centre third before any page-turn handling runs. The issue's own "the feature is
not dead — a touch page-turn substitutes `touch.heldMs`" is the claim the spec
disproves, and the disproof is correct on the code as it stands.

**Finding 2 — `getPressedFrontButton()` can never return LEFT or RIGHT.** The
issue asked for "a comment at the function saying which arms are reachable on
which boards", and noted "a board-aware assert would be better and is more
work." The PR delivers the comment (`src/MappedInputManager.cpp:378-392`) plus
the stronger option, relocated: a board-aware refusal in
`ButtonRemapActivity::onEnter` (`src/activities/settings/ButtonRemapActivity.cpp:25-36`)
rather than an `assert`. That is the issue's "better" option, with the rationale
for not using `assert` literally (release builds define no `NDEBUG`) stated in
spec A9 and in the PR body. Within what the issue authorises, not beyond it.

### Spec coverage — all five files, nothing dropped

Spec §9 lists exactly five source files. The diff touches exactly those five and
no others (plus the design/plan/research/review docs):

| Spec §9 row | Landed |
|---|---|
| `src/CrossPointSettings.h` — new `BEREAN_CAP_LONG_PRESS_PAGE_TURN` beside `BEREAN_CAP_ROTATION` | `:25-45`, same `#ifndef` / `#if FREEINK_DEVICE_X4PRO` / `#error` shape as `:8-23` |
| `src/SettingsList.h` — `#if` around the entry, rotation split nested inside | `:349-364`; the `#if BEREAN_CAP_ROTATION` / `#else` / `#endif` pair is intact inside the new gate, not collapsed (spec §4a) |
| `test/nav_key_gestures/NavKeyGesturesTest.cpp` — one new `TEST` | `:146-159` |
| `src/MappedInputManager.cpp` — block comment above the scan | `:378-392`, replacing the two-line in-body comment |
| `src/activities/settings/ButtonRemapActivity.cpp` — `<Logging.h>` + refusal | `:3,6` and `:25-36` |

Non-goals honoured: `lib/I18n/translations/` untouched (32 YAMLs, `git diff
--stat` shows none); `test/CMakeLists.txt` untouched; the reader's
`CHAPTER_SKIP` and `ORIENTATION_CHANGE` branches (`EpubReaderActivity.cpp:613`,
`:619`) and the `usePress` derivation (`ReaderUtils.h:53`) all left in place per
A4; no migration written per A6; `lib/hal/`, `lib/Input/` and `freeink-sdk/`
untouched.

Spec A12 (two commits, host test inside commit 1) is honoured exactly:
`9ce40fc1` carries `CrossPointSettings.h` + `SettingsList.h` + the test;
`ab7dc7c4` carries `MappedInputManager.cpp` + `ButtonRemapActivity.cpp`.

### Plan divergence

Steps 3, 4, 6 and 7 landed byte-identical to the plan's quoted blocks. The only
difference anywhere is that the new test's second `EXPECT_TRUE` message sits on
one line rather than two — a `clang-format` wrap, not a change. Nothing in the
plan is skipped and nothing extra is added.

### The one consequence that could have been buried, and is not

Gating the row removes `longPressButtonBehavior` from the persistence schema,
because `toJson` (`src/CrossPointSettings.cpp:66`) and `fromJson` (`:113`) both
iterate `getSettingsList()` and the key has no manual line among the ones at
`:85-101` that do. I verified this rather than taking it from the spec: the key
appears in `toJson`/`fromJson` only via the loop, and the member
(`src/CrossPointSettings.h:316`) keeps its `OFF` initializer. The PR body states
this under its own heading, names the three save triggers, and says the row also
leaves the web settings API. Both web-API call sites
(`src/network/CrossPointWebServer.cpp:1159,1263`) enumerate the same list and
key off `s.key`, and `grep -rn "longPress" data/ src/network/` returns nothing —
so no hand-written HTML form field is left pointing at a key the API no longer
serves. `SettingsActivity.cpp:49` likewise iterates; no consumer indexes the
list positionally, so removing an element cannot shift anything.

The behavioural claim that makes the drop benign — pinning the value at `OFF`
sets `usePress = true`, with no observable effect — also checks out at the
source. `HalGPIO::wasPressed` and `HalGPIO::wasReleased` both return
`synthesisedEdge(buttonIndex)` for `BTN_UP`/`BTN_DOWN` (`lib/hal/HalGPIO.cpp:209-225`),
so both edges genuinely land on the same tick; `BTN_LEFT`/`BTN_RIGHT` fall
through to `InputManager::isDigitalPressed`, which is `pin >= 0 && …`
(`InputManager.cpp:246`) against `PIN_UNASSIGNED` pins (`BoardConfig.h:1396`);
and `powerTurn` sits outside the `usePress` ternary (`ReaderUtils.h:62-66`).

No other UI surface writes the member — `grep -rn "longPressButtonBehavior" src
lib` returns only `SettingsList.h`, the member declaration, and three read sites
— so unlike the rotation precedent there is no second (reader-menu) row left
stranded. `buildLongPressMenuSetting()` is `longPressMenuFunction`, a different
setting, and is untouched.

### Test honesty

`APageTurnAtTheChapterSkipThresholdStillReportsASyntheticHeldTime`
(`test/nav_key_gestures/NavKeyGesturesTest.cpp:153-159`) drives the real state
machine — press held 0→700 ms on 50 ms ticks, release at 701 — and asserts the
outcome, not the implementation. It does not restate `HOLD_MS`; it hardcodes
701 against the reader's `SKIP_HOLD_MS`, which is the point (spec A8), and it is
sensitive to `HOLD_MS` in the right direction: at `HOLD_MS = 700`, 701 ≥ 700
takes the `Synth` arm at `NavKeyGestures.cpp:29` and the `EXPECT_EQ` fails on
its own terms, exactly as the PR's red-run transcript reports. The second
assertion (`reportingSyntheticHeldTime()`) is true for any event and so is weak
alone, but paired with the `EXPECT_EQ(…, Page)` above it, it pins the thing that
matters: a page turn whose duration the reader will never see.

The test pins the invariant that *justifies* the gate rather than the gate
itself. That is a limitation the spec states outright (§7a, §7b) with the reason
— `ReaderUtils.h` and the activity layer are not host-includable — rather than
papering over it, and the compensating checks are named and were run.

### Verification claims re-run here

| PR claim | Result |
|---|---|
| Host suite 593/593 | Reproduced: `ctest --test-dir build/test -j` → `100% tests passed out of 593` |
| i18n unchanged: 32 languages, 420 keys, 1 unused | Reproduced exactly |
| `./bin/clang-format-fix` whole tree, no files changed | Reproduced: no output, `git status --short` empty afterwards |
| Citations in the new comments | Spot-checked all of them — `BoardConfig.h:1396`, `InputManager.cpp:246,257-258`, `HalGPIO.cpp:186-205`, `HalGPIO.cpp:236-239`, `NavKeyGestures.cpp:29`, `EpubReaderActivity.cpp:451-459`, `ReaderUtils.h:84-94`, `ReaderUtils.h:18` — every one lands on the line it claims |

`pio run -e x4pro` was not re-run; the host suite, the preprocessor shape and
the include audit cover what this review needed, and the PR records the build as
green before and after the rebase.

### Things that would have been findings, and were not

- **A settings row removed while some other surface still writes the key.** No
  such surface exists; the grep above is exhaustive.
- **A positional assumption broken by shortening the settings vector.** All five
  `getSettingsList()` consumers iterate.
- **The web settings page left referencing a dropped key.** It is fully data
  driven; no `longPress` string in `data/` or `src/network/`.
- **`finish()` from `onEnter()` rendering a half-initialised screen.**
  `ActivityManager.cpp:163-166` calls `onEnter()` and then `continue`s, so the
  pop is serviced on the next iteration, and every `ButtonRemapActivity` member
  has an in-class initializer (`ButtonRemapActivity.h:21-26`), so nothing
  garbage could be drawn even if it were.
- **A doc left describing the removed setting.** `grep` over `docs/`, `README.md`
  and `SCOPE.md` finds no mention outside the `superpowers/` set this PR adds.

---

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
