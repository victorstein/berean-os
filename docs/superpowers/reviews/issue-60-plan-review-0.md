# Issue #60 — implementation plan review 0

Plan: `docs/superpowers/plans/2026-09-17-issue-60-plan.md`
Spec: `docs/superpowers/specs/2026-09-17-issue-60-design.md` (`VERDICT: CLEAR`,
`reviews/issue-60-spec-review-2.md`)
Reviewed: 2026-09-18 · worktree `fix/60-input-layer-traps` @ `22a024bc`

---

## What was verified, not assumed

Every `file:line` the plan writes *into the codebase* (comments and commit
bodies, which outlive the plan) was opened and checked. All of them are correct:

| Plan citation | Checked |
|---|---|
| `SettingsList.h:349-358` is the entry, both `#if BEREAN_CAP_ROTATION` arms | exact match, `src/SettingsList.h:349-358` |
| `CrossPointSettings.h:17-23` is the `BEREAN_CAP_ROTATION` block ending `#endif` at `:23` | exact, and `SettingsList.h:15` includes it, so the new macro is in scope |
| `MappedInputManager.cpp:379-380` are the two comment lines inside the function; `:378` is the signature | exact |
| `ButtonRemapActivity.cpp:1-9` include block, `:20-32` `onEnter()` | exact |
| `BoardConfig.h:1396` all four front pins `PIN_UNASSIGNED`; `:406` `PIN_UNASSIGNED = -1` | exact |
| `InputManager.cpp:246` `pin >= 0 && digitalRead(pin) == LOW`; `:257-258` left/right | exact |
| `HalGPIO.cpp:186-205` `synthesisedEdge` has no `BTN_LEFT`/`BTN_RIGHT` case; `:236-239` substitutes `SYNTHETIC_HELD_MS` | exact |
| `Activity.cpp:24` `finish()` → `popActivity()`; `ActivityManager.cpp:161-165` `continue`s after `onEnter()`; `ReaderActivity.cpp:44-47` precedent | exact |
| `EpubReaderActivity.cpp:613` `CHAPTER_SKIP`, `:619` `ORIENTATION_CHANGE`, `:451-459` long-press consume | exact |
| `ReaderUtils.h:18` `SKIP_HOLD_MS = 700`, `:53` `usePress`, `:84-94` swipe returns before `heldMs` | exact |
| `InputManager.h:399` `TOUCH_LONG_PRESS_MS = 500` | exact |
| `SettingsList.h:254` is a `std::vector<SettingInfo>` initializer | exact — removing an element shifts nothing |
| `SettingsList.h:467-474` erases `STR_FRONT_BTN_FOLLOW_ORIENTATION` on touch boards | exact, so §7c.1's neighbour warning is right |
| `gen_i18n.py:267` raw-text regex, `:873-881` `CRITICAL` + `sys.exit(1)` | exact |

Three claims were executed rather than read:

1. **`gen_i18n.py` baseline.** Ran
   `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` on a clean
   tree: `Languages: 32`, `String keys: 420`, `Unused keys: 1`. Step 5's
   expected output is exact, and `tail -4` does frame those three lines.
2. **`settings_snapshot.py` comparison is well-formed.** `scripts/settings_snapshot.py:12`
   prints `sorted(set(re.findall(r"StrId::(STR_[A-Z0-9_]+)", ...)))` joined by
   newline — byte-identical in shape to the inline `python3 -c` the plan runs
   against `git show HEAD:src/SettingsList.h`. The `diff` at plan `:261` really
   does produce no output on a correct change, and neither of the two comment
   blocks the plan inserts contains an `StrId::STR_` token that would perturb it.
3. **The red-first step reddens for its own reason.** Compiled the plan's test
   body (plan `:90-99`) against the real
   `lib/Input/Input/NavKeyGestures.{h,cpp}` in a scratch dir, once at
   `HOLD_MS = 850` and once at `700`:

   ```
   HOLD_MS=850  event=Page   reportingSyntheticHeldTime=true
   HOLD_MS=700  event=Synth  reportingSyntheticHeldTime=true
   ```

   So `EXPECT_EQ(..., NavEvent::Page)` is what fails under the temporary edit —
   not the `EXPECT_TRUE`, and not only the pre-existing
   `ThresholdsSitBetweenTheReadersOwnHolds`. Step 1's "two should fail" is also
   exactly right: I traced all 14 existing tests at `HOLD_MS = 700` and only
   `:141-144` breaks. 14 + 1 = 15, matching Step 2's "15/15".

Two structural risks that would have been blockers if true were checked and are
not:

- **False refusal from the four-pin predicate.** Enumerated every `InputPins`
  initializer in `BoardConfig.h` (`:784, :833, :865, :890, :947, :987, :1024,
  :1027, :1090, :1181, :1311, :1389, :1396`). No profile wires `left`/`right`
  while leaving `back`/`confirm` unassigned, and the four profiles with all four
  wired (`:784, :833, :865, :1090`) all use `InputStyle::XteinkAdcLadder`, not a
  hold-derived style. Spec A11 holds; Step 7's guard refuses nothing it should
  not.
- **Uninitialised state if `loop()` ran after the early `finish()`.**
  `ButtonRemapActivity.h:22-27` gives every member an in-class initialiser, and
  `ActivityManager.cpp:165` `continue`s so the pending pop is serviced before the
  next `loop()`. No use-of-uninitialised path.

Include ordering in Step 7 also survives clang-format: `.clang-format`
`IncludeBlocks: Regroup` puts all four of `<BoardConfig.h> <GfxRenderer.h>
<I18n.h> <Logging.h>` in Priority 1, sorted — which is the order the plan
already writes. `ColumnLimit: 120` accommodates the longest new line (89 chars).
`build/` is gitignored (`.gitignore:13`), so Step 9's `git status --short  #
expect: empty` is achievable after a host build.

This is a materially accurate plan. The findings below are all inline fixes.

---

## MAJOR 1 — The plan produces three commits; the spec and the plan's own header both say two

**Claim.** Plan `:8`: "Two independent findings, two commits, one PR."

**Problem.** The plan then lays out three commits, and the spec's ratified commit
shape says the test belongs *inside* commit 1. An implementer executing the plan
literally lands a branch whose shape contradicts the design doc the PR will link.

**Evidence.**

- Spec `:522-524` (A12): "**A12 — Two commits, one PR.** … Commit 1 is the
  capability gate **plus its host test**; commit 2 is the comment plus the
  refusal."
- Plan `:145-151`, Step 2: `git add test/nav_key_gestures/NavKeyGesturesTest.cpp`
  then `git commit -m "test: pin that a nav key cannot reach the chapter-skip
  threshold …"` — commit A, test only.
- Plan `:271-289`, Step 5: `git add src/CrossPointSettings.h src/SettingsList.h`
  then `git commit -m "fix: gate off a long-press setting no input can reach"` —
  commit B, gate only.
- Plan `:421-435`, Step 8: commit C, the comment plus the refusal.
- Step 2's own message even narrates the split — "The next commit gates the
  Controls entry that promised this" (`:149-150`) — so it is deliberate, not a
  slip, which is why the header at `:8` is now simply wrong.
- The same sentence also miscounts: `:8` says "Nine steps", and the plan has ten
  (Step 0 at `:41` through Step 9 at `:440`).

**Concrete fix.** Take A12 as authoritative and fold the test into commit 1:

1. In Step 2 (`:142-151`), keep the revert, the rebuild and the 15/15 run, keep
   `./bin/clang-format-fix -g`, and **delete the `git add` and `git commit`**.
   Replace the closing line with: "Leave the test uncommitted — Step 5 commits it
   together with the gate it justifies (spec A12)."
2. In Step 5 (`:271`), extend the stage line to
   `git add test/nav_key_gestures/NavKeyGesturesTest.cpp src/CrossPointSettings.h src/SettingsList.h`,
   and add one paragraph to that commit body carrying Step 2's rationale, e.g.
   "A host test pins the constant relationship this rests on: at 701 ms — the
   shortest hold that could clear `SKIP_HOLD_MS` (700) — a nav key still resolves
   to a page turn, so the reader sees `SYNTHETIC_HELD_MS`, not 701."
3. Fix `:8` to read "two commits, one PR. Ten steps (0–9)."

The Rollback section (`:534-546`) then becomes correct as written: reverting
commit 1 takes the test with the gate, rather than stranding a test whose commit
message refers to "the next commit" that no longer follows it.

*(If the preference is instead to keep the red/green commit separate, that is a
spec edit — A12 has to change — and should be raised rather than done silently.)*

---

## MINOR 1 — The spec's third gating consequence, the web settings API, appears nowhere in the plan

**Claim.** Plan `:33-37` enumerates the persistence consequence of gating the
row, and Step 9's PR-body checklist (`:500-504`) restates it for the reviewer.

**Problem.** The spec lists three consequences of removing the row from
`getSettingsList()`; the plan carries two. The third — the row vanishing from the
device web server's settings API — is not mentioned in the plan's preamble, in
the commit body, or in the PR body bullets. It is benign, but it is a spec item
with no step or line mapping to it, and the PR body is where the spec says these
consequences get stated.

**Evidence.**

- Spec `:78-79`: "3. removes the row from the web settings API, which enumerates
  the same list (`CrossPointWebServer.cpp:1159,1263`)."
- Spec `:308`: the same in the §5a flow diagram.
- Verified both call sites: `src/network/CrossPointWebServer.cpp:1159` and
  `:1263` each call `getSettingsList(&sdFontSystem.registry())`.
- Plan `:33-37` covers only `toJson`/`fromJson`; plan `:500-504` likewise.

**Concrete fix.** Append one clause to the PR-body bullet at plan `:500-504`:
"…and the row also leaves the device web server's settings API, which enumerates
the same list (`CrossPointWebServer.cpp:1159,1263`)." Optionally add the same
half-sentence to Step 5's commit body after the `toJson`/`fromJson` paragraph.

---

## MINOR 2 — "Every step leaves the tree building and committable" is contradicted by Step 1

**Claim.** Plan `:8-9`: "Nine steps. Every step leaves the tree building and
committable."

**Problem.** Step 1 deliberately does the opposite, and says so. An implementer
who trusts the header over the step could commit the temporary `HOLD_MS = 700`
edit, which would ship a constant change into `lib/Input/` — a file the spec
lists under "Not touched" (`:536-537`) and which `.claude/agents/ui-dev.md`
protects.

**Evidence.**

- Plan `:102-107`: "Now make it fail for its own reason. Temporarily edit
  `lib/Input/Input/NavKeyGestures.h:41`: `static constexpr uint32_t HOLD_MS =
  700;   // TEMPORARY - revert in step 2`".
- Plan `:124`: "**Do not commit this state.**"
- The state is not committable in a second sense too: `HOLD_MS = 700` fails
  `NavKeyGesturesTest.cpp:142`'s `EXPECT_GT(NavKeyGestures::HOLD_MS, 700u)`, so
  the host suite is red, which CI (`.github/workflows/ci.yml`) would reject.

**Concrete fix.** Reword plan `:8-9` to: "Ten steps (0–9). Every step except the
deliberate red at Step 1 leaves the tree building and committable; Step 1's
temporary `HOLD_MS` edit is reverted in Step 2 and must never be staged." This
also absorbs the step-count correction from MAJOR 1.

---

## Not findings, recorded so a later pass does not re-raise them

- **Step 3's `pio run` on a macro nobody consumes** (plan `:183-189`) looks like
  the repeat build `CLAUDE.md`'s testing checklist discourages, but it earns its
  place: it is the only run that isolates "`FREEINK_DEVICE_X4PRO` is defined, so
  the `#error` arm is not taken" (`platformio.ini:166,184`) from a
  `SettingsList.h` failure in Step 5. Leave it.
- **Step 9's PR body is a content checklist, not verbatim prose**, while both
  commit messages are verbatim. Every bullet carries its citations and the title
  and `Closes #60` keyword are literal, so an implementer can execute it. Not
  worth converting.
- **`ReaderActivity.cpp:146` also reads `longPressButtonBehavior`.** It is the
  dead `ReaderActivity::loop()` struck by spec review 0 (spec `:331-335`); a
  fresh `grep -rn longPressButtonBehavior src lib` turns up no consumer the spec
  has not already dispositioned.
- **Step 5's scratch files land in `/tmp`** rather than a session scratchpad.
  Harmless for a single-implementer run; the filenames are issue-scoped.

---

VERDICT: CLEAR
MAJORS: 1
MINORS: 2
