# Review — `2026-09-16-issue-30-design.md` (pass 0)

**Reviewed:** `docs/superpowers/specs/2026-09-16-issue-30-design.md`
**Against:** issue #30 (`gh issue view 30 --repo victorstein/berean-os`),
`docs/superpowers/research/2026-09-16-issue-30-research.md`
**Tree:** `fix/30-launcher-wake-refresh` @ `8d0408e5`, `freeink-sdk` submodule initialised.

## What checked out

Every load-bearing claim in the research note was re-derived from source and holds:

- The flag's path and its discard — `src/main.cpp:538-541,571`,
  `src/activities/ActivityManager.cpp:247-254`, `src/activities/launcher/LauncherActivity.cpp:473`.
- The driver analysis. SSD1677 re-arms the one-shot in `initController`
  (`Ssd1677Driver.cpp:220`) and forces `Half` on the first paint
  (`Ssd1677Driver.cpp:439`, with `halfSeqOverride = 0xD7` at `Ssd1677Driver.cpp:62` and the
  X4 Pro taking `ssd1677DefaultConfig()` at `Ssd1677Driver.cpp:717`). `Ssd1677Driver` has
  no `skipInitialResync` override, so `HalDisplay.cpp:25` reaches the base no-op
  (`PanelDriver.h:161`) — confirmed by `grep -rn skipInitialResync`, which lists
  Uc8179/Uc8279X4/Uc8279/Uc8253X3/PaperMono and not Ssd1677.
- `Uc8179Driver.cpp:293` and `Uc8279X4Driver.cpp:179` both gate the partial path on an
  `_oldPlaneValid` that is `false` at construction (`Uc8179Driver.h:137`,
  `Uc8279X4Driver.h:112`); `skipInitialResync` clears only `_needFullClear`
  (`Uc8179Driver.cpp:391`, `Uc8279X4Driver.cpp:261`).
- The launcher's render really is the first panel paint after `begin()` on that path.
  `GfxRenderer::begin()` (`GfxRenderer.cpp:121-132`) does not paint; `Activity::onEnter`
  does not paint (`src/activities/Activity.cpp:5`); `grep -rn "displayBuffer\|requestUpdate" lib/Epub`
  returns nothing, so the thumbnail path in `resolveTargets` paints nothing and the comment at
  `LauncherActivity.cpp:133-137` is stale, as research §1 says; the second
  `setupDisplayAndFonts` call (`src/main.cpp:404`) is on the SD-failure path, which `return`s.
- The five `goHome` call sites and the 20 argument-less `Activity::onGoHome` calls
  (`grep -rn "onGoHome(" src` → 22 hits, minus the definition at `Activity.cpp:13` and the
  declaration at `Activity.h:69`). Research §5's correction of the issue's "~20 call sites" is right.
- A11. `FreeInkDisplay.cpp:573-575` promotes FAST→HALF on a polarity change, and
  `_inversionDirty` is cleared unconditionally at `FreeInkDisplay.cpp:598`, so requesting HALF
  explicitly cannot strand the dirty bit and cost a second promoted refresh. No interaction.
- Formatting. `.clang-format:133` is `ColumnLimit: 120`; the three proposed statements measure
  117, 96 and 114 columns. `.venv/bin/clang-format --version` → `clang-format version 21.1.8`,
  and `bin/clang-format-fix:3-12` resolves the binary from `PATH` only, so the spec's
  `PATH="$PWD/.venv/bin:$PATH"` prefix is required, not decoration.
- `LOG_DBG` is compiled out at `LOG_LEVEL=1` (`lib/Logging/Logging.h:57-61`), which is the
  release env (`platformio.ini:188`); dev is `LOG_LEVEL=2` (`platformio.ini:170`). A8 costs
  nothing shipped.

A3 is correctly identified as the trap: `HomeActivity.cpp:308-311` does call `requestUpdate()`
behind `!firstRenderDone`, and the launcher resolves everything in `onEnter`
(`LauncherActivity.cpp:65-72`) so it must not.

---

## Findings

### MAJOR 1 — `HALF_REFRESH` is not unconditionally non-differential on the UC8279 batch, so the spec's central justification is overstated

**Claim.** Goal, lines 57-60: the change makes the clean "honoured on every panel batch, and
does not depend on undocumented behaviour of a pinned dependency". A1, lines 84-85: "on
UC8179/UC8279 a `HALF` and a `FULL` run the same OTP waveform — only the OLD-plane seed differs".

**Problem.** That equivalence holds for UC8179 and is false for UC8279_X4. On the UC8279 X4
driver a `Half` request is *eligible for the partial path*: the only mode excluded is `Full`.
So on that batch `HALF_REFRESH` is a clean exactly when `_oldPlaneValid` is false — i.e. on the
first paint after a chip-reset wake — which is precisely the undocumented driver internal that
Problem §3 (lines 48-53) says this change stops relying on. The fix works today, but its
correctness on one of the three batches still lives in the pinned submodule, and A1's rejection
of `FULL_REFRESH` rests on a premise that is wrong for that batch.

**Evidence.**

```
Uc8279X4Driver.cpp:179  const bool fast = (mode != RefreshMode::Full) && !_needFullClear && _oldPlaneValid;
Uc8279X4Driver.cpp:208    bus.data(fast ? _cfg.tssetFast : _cfg.tsset);  // DU 0x5A / GC 0x1E
Uc8279X4Driver.cpp:221    if (fast) bus.cmd(CMD_PARTIAL_IN);
Uc8279X4Driver.cpp:246    _oldPlaneValid = true;        // displayFinish, after every refresh
```

Contrast the sibling, where `Half` is explicitly carved out of `fast`:

```
Uc8179Driver.cpp:292    const bool scrub = (mode == RefreshMode::Half);
Uc8179Driver.cpp:293    const bool fast = (mode == RefreshMode::Fast) && !scrub && !_needFullClear && _oldPlaneValid;
```

UC8279_X4 is linked for this target (`BoardConfig.h:128-130`, `FREEINK_DRIVER_UC8279_X4 1` under
`FREEINK_DEVICE_X4PRO`) and selectable at boot on LUT_VER `0x02/0x68/0x69`
(`XteinkDetect.cpp:342-344`), so it is a real batch, not a theoretical one.

**Fix (inline, no decision reversal needed).** Keep `HALF_REFRESH` — it is correct on the wake
path on all three drivers, it matches the reference, and `FULL_REFRESH` buys nothing there
(`Ssd1677Driver.cpp:439` collapses a requested `Full` to `Half` anyway, and both UC drivers
white-seed identically when `_oldPlaneValid` is false). But correct the text:

- A1: qualify the equivalence — it holds on UC8179 for every paint, and on UC8279_X4 only while
  `_oldPlaneValid` is false. Cite `Uc8279X4Driver.cpp:179`.
- Goal (57-60) and Problem §3: drop "does not depend on undocumented behaviour of a pinned
  dependency" in favour of the accurate, still-sufficient version — the *request* becomes local
  and explicit, and on SSD1677 with `fadingFix` it is what makes the paint clean at all. Note
  under "The SDK moving underneath us" (269-272) that `HALF` on UC8279_X4 is a clean only for a
  first paint, so a future SDK bump that seeds `_oldPlaneValid` across a wake would need
  `FULL_REFRESH` here.

### MAJOR 2 — the silent-reboot route into the launcher is in exactly the state the flag describes, and the spec asserts it needs no change

**Claim.** Lines 241-245: "Every other route to the launcher (`src/main.cpp:566`, the home
gesture and the pop-to-home at `ActivityManager.cpp:81,114`) constructs it with the default
`false` and is unchanged."

**Problem.** `src/main.cpp:566` is the `BootResume::Silent` route, and the code's own comment two
branches earlier says the panel is still showing a frame the launcher did not draw. That is the
proposed header comment's definition of the flag verbatim (spec lines 174-176: "the panel is
still showing a frame this activity did not draw"). Worse, the sibling branch of the same
`if`/`else if` chain already gets the safe treatment: `goToReader(APP_STATE.openEpubPath)` at
`main.cpp:560` passes `allowFastInitialRefresh` defaulted to `false`, which leaves
`pagesUntilFullRefresh` at 0 and so gives the reader a clean first paint. After this change the
launcher is the only route out of that block that paints differentially over a stale frame — the
opposite polarity of default from its neighbour, decided three lines apart.

**Evidence.**

```
src/main.cpp:512-514   case BootResume::Silent:
                         // Splash skipped: the routing block below picks the target activity; the
                         // panel keeps showing the pre-reboot popup until that first paint lands.
src/main.cpp:559-566   } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER && ...
                           activityManager.goToReader(APP_STATE.openEpubPath);   // :560, clean first paint
                         } else if (resume == BootResume::Silent) {
                           activityManager.goHome();                             // :566, FAST first paint
ReaderActivity.cpp:19-22  if (allowFastInitialRefresh) { pagesUntilFullRefresh = ... }   // default: clean
ActivityManager.h:88      void goToReader(std::string path, bool allowFastInitialRefresh = false);
```

The observable consequence is the same one the spec attributes to the wake path: on an SSD1677
unit with `fadingFix` persisted to 1, a silent reboot to the launcher paints a DU over the
pre-reboot popup and cannot clear it.

**Fix (inline).** Add an explicit Non-goal naming this route and why it is deferred, mirroring
A7's treatment of the stale `main.cpp` comment — e.g. "the `BootResume::Silent` route
(`src/main.cpp:566`) leaves the same stale-frame condition unflagged; `src/main.cpp` is not
edited by this change (A7), and the one-token fix belongs with that follow-up." Then soften line
241 from "unchanged" (which reads as "correct") to "unchanged, with the silent-reboot exception
above". If the human would rather close it here, passing `true` at `main.cpp:566` is a one-token
change that reuses this change's mechanism exactly — but that is a scope call, not a review fix.

### MINOR 1 — A8's log line prints `first=` with an inverted expression, and every Testing expectation depends on the inversion

**Claim.** Lines 189-190 and 297-304.

**Problem.** The field is labelled `first` but the argument is `firstRenderDone ? 0 : 1` — it
prints "is this the first render", the negation of the member it names. An implementer writing
the obvious `firstRenderDone ? 1 : 0` inverts the second field of every expectation in the
Testing section (`clean=1 first=1 mode=HALF`, `clean=1 first=0 mode=FAST`, …) and the human
tester reads a correct build as broken.

**Fix.** Rename the field to what is printed — `firstPaint=%d` — or print
`!firstRenderDone ? 1 : 0` and say so. Either way add one clause to A8 stating that the value is
"this paint is the first", not "the latch is set".

### MINOR 2 — Testing step 1's "nothing else from `LAUNCH`" is false

**Claim.** Lines 297-299: "Expect `[DBG] LAUNCH Paint: clean=1 first=1 mode=HALF` once, and
nothing else from `LAUNCH` until an input."

**Problem.** `resolveTargets` emits `LAUNCH`-tagged lines on every entry, before the render:

```
LauncherActivity.cpp:124  LOG_INF(MODULE, "Meeting publication: %s", ...);
LauncherActivity.cpp:136  if (generatedAny) LOG_INF(MODULE, "Generated a missing cover thumbnail");
LauncherActivity.cpp:219  LOG_DBG(MODULE, "No cover thumbnail for %s", bookPath.c_str());
```

`MODULE` is `"LAUNCH"` (`LauncherActivity.cpp:39`), and `LOG_INF` survives at both log levels
(`Logging.h:51-55`). A tester following the step literally will report a failure.

**Fix.** Re-word to the property actually being checked: "exactly one `Paint:` line per launcher
entry" — which is also what step 5 wants.

### MINOR 3 — A5's replacement comment substitutes one inaccuracy for another

**Claim.** A5 (lines 109-114) criticises the existing comment for repeating the "~20 call sites"
miscount, then proposes (lines 203-205) "it stays in the signature for the callers that pass it,
and is ignored."

**Problem.** Research §5 establishes that nothing passes `initialMenuItem` a value other than
`HomeMenuItem::NONE`: all 20 `Activity::onGoHome` calls use the default (`Activity.h:69`), and of
`goHome`'s five call sites only `src/main.cpp:571` names the parameter at all, with
`HomeMenuItem::NONE`. "The callers that pass it" implies callers that pass something meaningful;
there are none. The new comment is a weaker version of the claim A5 removed.

**Fix.** State the verified fact: "no caller passes anything but `HomeMenuItem::NONE`; the
parameter stays in the signature so `Activity::onGoHome`'s 20 call sites and the default argument
at `ActivityManager.h:93` need not change."

### MINOR 4 — two citation slips

- Line 42 and research §3 cite `Ssd1677Driver.cpp:427` for the `if (!turnOff)` gate. Line 427 is
  a comment; the statement is line 428 (`if (!turnOff) {`). The reasoning is unaffected.
- Line 143 cites `test/CMakeLists.txt:51-89` for "every suite under `test/`". The
  `add_subdirectory` list runs past that — `number_grid` is at line 92 and the block continues.
  Use `test/CMakeLists.txt:51-` or the real end line, otherwise the "none constructs an activity"
  claim reads as checked over a truncated list.

---

## On the spec's own open questions

**Open question 1 (A7).** Agreed: follow-up. The comment at `src/main.cpp:453-456` is genuinely
wrong for all three X4 Pro drivers, but correcting it means importing the whole research note into
`main.cpp`, and the brief scopes this change to the launcher and the manager. MAJOR 2 above should
ride in that same follow-up.

**Open question 2 (scope).** Implement, do not close #30. Three reasons, in order of weight:
`goHome`'s `cleanInitialRefresh` is currently a parameter that lies about its effect, and an API
that states an intent it discards is a defect independent of any panel; the symptom *is* reachable
today on an SSD1677 batch with `fadingFix` persisted through the web settings API
(`SettingsList.h:277` keeps the `"fadingFix"` key in the shared list even though
`SettingsActivity.cpp:54-56` hides the row on X4 Pro); and the cost is six lines with no
allocation. What the spec should add — and the PR description should say — is that #30's stated
symptom ("so the sleep screen ghosts") was written from code archaeology and is not reproducible
on a default unit, so the change is a correctness and robustness fix rather than a repair of an
observed ghost. The spec already says this in its Testing section (308-315); it belongs in the
issue too, so nobody later "verifies" it optically and concludes the fix did nothing.

**A3, A2, A4, A6, A9, A10, A11** — attacked and sound as written. A10 in particular: nothing under
`test/` constructs an activity, and extracting a two-boolean ternary into a free function to test
it would invent a pattern the reference implementation does not have. The build plus A8's serial
line is the right verification budget for this change.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 2
