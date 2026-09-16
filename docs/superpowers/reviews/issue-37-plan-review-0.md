# Issue #37 — plan review, pass 0

Adversarial review of `docs/superpowers/plans/2026-09-16-issue-37-plan.md` against
`docs/superpowers/specs/2026-09-16-issue-37-design.md` (spec reviewed CLEAR at
`docs/superpowers/reviews/issue-37-spec-review-0.md`).

**Method.** The plan claims "every number and every line of code below was executed in
this worktree before being written down." That claim was tested rather than taken: the
plan's edits were applied verbatim by a script that asserted a unique match for each
`old_string`, then the full harness was run — the three preprocessor checks red and
green, the step-1 `#error` probe, both repo gates, `clang-format-fix` over the whole
tree, `git diff --stat ded48d17 -- src/`, and `pio run -e x4pro` on both the pristine
and the changed tree. Every number in the plan reproduced exactly, to the byte. The
edits were then reverted; `git status --short` is empty and `git diff HEAD` is empty.

Four MINOR findings, no MAJOR, no BLOCKER. Detail on what held up is in
"Verified against the code" at the end, because on a plan this specific the useful
signal is which claims survived checking.

---

## MINOR 1 — step 1's red→green test exercises the one case its own comment says the guard is *not* for

**Claim.** Step 1's probe (plan:135-157) is the red→green test for **A2**: "a build for
a device that is not the X4 Pro must fail loudly rather than silently inherit a rotation
policy" (plan:130-131).

**Problem.** The probe simulates *no device at all*, not *a different device*. The
comment the same step tells the implementer to write says those are different cases and
that only the second is what the guard is for:

> `// with no device at all is already caught earlier, by BoardConfig.h; this`
> `// catches a build for a DIFFERENT device, which BoardConfig accepts.)`
> — plan:184-185

So the test never enters the state the feature exists to catch. The plan is candid about
this in prose ("It already fails, but on `BoardConfig.h`'s own 'no device selected'
error — not on ours", plan:160-162), and the discriminator it settles for — "is there a
`BEREAN_CAP_ROTATION` line in stderr?" — does work. But it is weaker than it needs to
be, and a stronger version costs one extra flag.

**Evidence.** Measured on the pristine tree, preprocessing
`src/activities/settings/SettingsActivity.cpp` with the plan's flag munging:

| extra flags | rc | errors |
| --- | --- | --- |
| `-UFREEINK_DEVICE_X4PRO` (the plan's) | 1 | 4 — `BoardConfig.h:74` "no device selected", `BoardConfig.h:88` "must share one MCU family" |
| `-UFREEINK_DEVICE_X4PRO -DFREEINK_DEVICE_X4=1` | **0** | **0** |

`BoardConfig.h:33-67` `#ifndef`-defines all twelve `FREEINK_DEVICE_*` macros to 0 and
`:71-75` only errors when none is set, so naming any other device satisfies it. With the
change applied, that second flag set fails with exactly one error — ours — which is the
"BoardConfig accepts the build, we reject it" scenario **A2** describes.

**Fix.** In plan:151-152, change

```python
out = [a for a in out if not a.startswith("-DFREEINK_DEVICE_X4PRO")]
r = subprocess.run(out + ["-E", "-UFREEINK_DEVICE_X4PRO"], capture_output=True, text=True)
```

to

```python
out = [a for a in out if not a.startswith("-DFREEINK_DEVICE_X4PRO")]
r = subprocess.run(out + ["-E", "-UFREEINK_DEVICE_X4PRO", "-DFREEINK_DEVICE_X4=1"],
                   capture_output=True, text=True)
```

and update the two expectations: RED becomes `returncode: 0` with no output lines (a
different device compiles silently today — which is exactly the bug **A2** closes), and
GREEN stays `returncode: 1` with the single `src/CrossPointSettings.h:21:2` line already
quoted at plan:211. Drop the "It already fails, but on `BoardConfig.h`'s own..."
sentence at plan:160-162, which no longer applies.

---

## MINOR 2 — step 5's heading says four files, its expected output says three

**Claim.** plan:487, "### Confirm the diff is exactly four files' worth of directives".

**Problem.** The block immediately below it (plan:496-499) expects three files and says
so: `3 files changed, 27 insertions(+)`. "Four" is a leftover from the spec's *four
surfaces* closed by one edit (spec:253-257) — surfaces, not files. An implementer
reading the heading literally will count three and think a file is missing.

**Evidence.** Measured with the full change applied, byte-identical to plan:496-499:

```
 src/CrossPointSettings.h                         | 17 +++++++++++++++++
 src/SettingsList.h                               |  8 ++++++++
 src/activities/reader/EpubReaderMenuActivity.cpp |  2 ++
 3 files changed, 27 insertions(+)
```

The spec agrees on three: "the diff touches `src/CrossPointSettings.h`,
`src/SettingsList.h` and `src/activities/reader/EpubReaderMenuActivity.cpp` only"
(spec:232-235).

**Fix.** plan:487 → "### Confirm the diff is exactly three files' worth of directives".

---

## MINOR 3 — step 0 never asserts the compilation database contains project sources, and the troubleshooting remedy for that symptom does not fix it

**Claim.** Step 0 (plan:60-63): run `pio run -t compiledb`, "Expect `SUCCESS` in about
11 s and a `compile_commands.json` at the repo root." Everything in steps 1-4 then reads
that file.

**Problem.** `SUCCESS` plus a file at the repo root is not sufficient — the database can
succeed and contain no translation unit from this worktree, in which case
`next(e for e in db if e["file"].endswith(src))` raises `StopIteration` and *every* test
in the plan is unrunnable. Step 0 has no check that would notice, and the
troubleshooting row for the exact symptom prescribes the wrong remedy:

> `| ppcount.py raises StopIteration | no database, or wrong path spelling | re-run pio run -t compiledb; pass the path exactly as in step 0 |`
> — plan:557

Neither cause listed is the one I hit, and re-running is not what repaired it.

**Evidence.** Observed in this worktree, in order:

1. First `~/.platformio/penv/bin/pio run -t compiledb` → `FAILED` at 18.9 s, no
   `compile_commands.json` at the root.
2. Second run → `SUCCESS` at 19.6 s, and the root `compile_commands.json` held **2,286
   entries, 0 of them under this worktree** — every entry was an ESP-IDF framework or
   `managed_components` source. `.pio/build/x4pro/compile_commands.json` had the same
   shape (2,043 entries, 0 project files).
3. All three `ppcount.py` invocations from plan:113-118 raised `StopIteration`.
4. `rm -f compile_commands.json` then regenerating → `SUCCESS`, **509 entries, 54 under
   `/activities/`**, with `src/activities/settings/SettingsActivity.cpp` and
   `src/activities/reader/EpubReaderMenuActivity.cpp` present as repo-relative paths.
   Every test then behaved exactly as documented.
5. A further run over the good database → `SUCCESS` in 11.3 s, still 509 entries. Stable
   thereafter.

The trigger appears to be leftover state from the failed run 1 rather than the plan, and
I could not reproduce run 1's failure. That is precisely why it belongs in step 0 as an
assertion rather than in the reviewer's head: the failure mode is silent, it fakes
`SUCCESS`, and the plan's own recovery advice walks past it.

**Fix.** Add one line to step 0, right after the `compiledb` invocation at plan:60-63:

```bash
python3 -c "import json;db=json.load(open('compile_commands.json'));assert any(e['file'].endswith('src/activities/settings/SettingsActivity.cpp') for e in db),'database has no project sources -- rm compile_commands.json and regenerate'"
```

Expect no output. And replace the remedy in plan:557 with
`rm -f compile_commands.json && ~/.platformio/penv/bin/pio run -t compiledb` — deleting
first is what actually recovers it. While there, plan:63's "about 11 s" is right only
for a warm tree; I measured 19-29 s on the first two runs and 11.3 s once warm. Say
"about 11 s warm, up to ~30 s on the first run".

---

## MINOR 4 — `MAX_MENU_ITEMS` is described as "only a `reserve`"; it is also the bound of a fixed array

**Claim.** plan:406-409, justifying why step 4 needs no other edit: "`MAX_MENU_ITEMS` is
only a `reserve` (`EpubReaderMenuActivity.h:57-58`), so removing a row needs nothing
else".

**Problem.** The conclusion is right, the reason is not quite. `MAX_MENU_ITEMS` is a
`static constexpr size_t` at `src/activities/reader/EpubReaderMenuActivity.h:57` that
also sizes a fixed member array on the next line and caps two loops. A sentence that
says "only a `reserve`" invites the reverse inference — that adding a row would also be
free — which is not true past 24. It matters only because this is the load-bearing
justification for touching nothing else in a file the plan is at pains to keep a
one-line diff in.

**Evidence.**

```
src/activities/reader/EpubReaderMenuActivity.h:57:  static constexpr size_t MAX_MENU_ITEMS = 24;
src/activities/reader/EpubReaderMenuActivity.h:58:  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
src/activities/reader/EpubReaderMenuActivity.cpp:36:  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
src/activities/reader/EpubReaderMenuActivity.cpp:48:  items.reserve(MAX_MENU_ITEMS);
src/activities/reader/EpubReaderMenuActivity.cpp:193:  const size_t rowCount = std::min(menuItems.size(), MAX_MENU_ITEMS);
```

**Fix.** plan:406-407 → "`MAX_MENU_ITEMS` is a `reserve` hint and the bound of a fixed
row array (`EpubReaderMenuActivity.h:57-58`); removing a row can only lower the count,
so nothing else changes."

---

## Verified against the code

Applied the plan's edits verbatim, ran the whole harness, reverted.

**Every source citation in the plan is exact.** Checked individually, against the
pristine tree: `src/SettingsList.h:15` (`#include "CrossPointSettings.h"`), `:138-148`
(the mid-enum comment), `:161` (`#if BOARD_HAS_PSRAM`), `:192-217`
(`buildLongPressMenuSetting`), `:279` (`#if FREEINK_CAP_FRONTLIGHT`), `:317-320`,
`:347-350`, `:352-362` (the `STR_SHORT_PWR_BTN` two-variant precedent), `:459-467` (the
`hasTouch()` erase block); `src/CrossPointSettings.h:6` (`#include <cstdint>`), `:8`
(`class CrossPointSettings`), `:67` (`enum ORIENTATION`, a class member — so the plan's
file-scope relocation is justified), `:177-182` (`ORIENTATION_CHANGE = 2` last before
the `_COUNT` sentinel, so **A8** holds), `:241` (`uint8_t orientation = PORTRAIT;`);
`src/CrossPointSettings.cpp:161` (the ENUM clamp, and `grep -n orientation` on that file
is genuinely empty); `src/activities/reader/EpubReaderMenuActivity.cpp:10`, `:73`,
`:86`, `:104-117`, `:145`, `:198-201`, and `.h:22`, `:57-58`, `:79`, `:81-82`;
`src/activities/reader/EpubReaderActivity.cpp:282`, `:286`, `:625-632`, `:896`;
`src/activities/settings/SettingsActivity.cpp:49`, `:61`, `:86-90`, `:260`, `:273`,
`:412-413`; `platformio.ini:166` and `:184`; `.gitignore:15`
(`/compile_commands.json`). `SleepActivity.cpp:513` and `:525` are right too, at
`src/activities/boot_sleep/SleepActivity.cpp`.

**Step 0 baselines.** `scripts/settings_snapshot.py` → 133 lines. `scripts/i18n_orphans.sh`
→ 22 lines. `.venv/bin/clang-format --version` → 21.1.8. `ls freeink-sdk/libs` populated.
All as plan:100-102 and plan:53 state.

**Red and green.** With the correct database, the three checks return `1 1 1` on the
pristine tree and `0 0 0` with the change applied — plan:113-118 and plan:249, 288, 360.
No stray `.d` file appeared at the repo root, so dropping `-MMD` (plan:93-94) does what
it claims.

**Step 1's `#error`.** Lands at exactly the predicted location and message:

```
src/CrossPointSettings.h:21:2: error: #error "BEREAN_CAP_ROTATION: unhandled device set; decide whether this board offers rotation"
```

plan:211 verbatim, including the line number, when the block is pasted as given.

**Format.** `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the whole tree with
the change applied: exit 0, and `git diff --stat` unchanged afterwards. The new code —
including the reflow-prone comment block and the hand-wrapped `#else` variant in step 3 —
is already clang-format-21 clean under `ColumnLimit: 120` / `ReflowComments: Always`.
plan:459-460 holds.

**Both gates.** With the change applied: snapshot still 133 keys and `diff` against the
pristine key set is empty; orphans still 22. `settings_snapshot.py` uses
`sorted(set(...))`, so step 3's duplicated `StrId::` tokens in the `#else` branch cannot
move it — the mechanism behind **A9**, not just the outcome.

**Build, both sides.**

| | RAM | Flash |
| --- | --- | --- |
| pristine (`4f330b56`) | 64,052 | 5,318,858 |
| change applied | 64,052 | 5,318,598 |

SUCCESS both times. −260 B flash, RAM byte-identical — plan:474-480 to the byte,
including acceptance criterion 4. The only warning is
`.pio/libdeps/x4pro/WebSockets/src/WebSocketsClient.cpp:573`, pre-existing; nothing under
`src/` warns, as plan:483-485 says.

**Diff shape.** `git diff --stat ded48d17 -- src/` reproduces plan:496-499 exactly: 17/8/2,
27 insertions, 0 deletions, and neither `EpubReaderActivity.cpp` nor
`CrossPointSettings.cpp` appears.

**Spec coverage.** Every spec requirement maps to a step and no step invents work the
spec does not sanction: §4.1 → step 1; §4.2 #1 → step 4; #2 → step 2; #3 → step 3;
§4.3 → the explicit "change nothing else" at plan:411-416; §7.1.1-3 → step 5; §7.3 →
step 6, item for item. Acceptance criteria 1-4 are each reachable (1: steps 2+4; 2: step
3 plus the load clamp; 3: step 1; 4: step 5's build). No placeholder, no "handle the
remaining cases", no step that says what without showing how — every edit is given as
exact before/after text, which is why applying it by unique-match assertion worked on
the first attempt.

**Committability of each step, independently checked** rather than taken from plan:8.
Step 1 only defines a macro; steps 2-4 each remove list entries that nothing indexes by
position. There is no `static_assert` on the list length in `SettingsList.h` or
`SettingsActivity.cpp`, and `"orientation"` as a persisted key appears nowhere in `src/`
or `data/` outside `SettingsList.h:321` — so no intermediate state can fail to build for
a reason the final build would not have caught.

**Two spec premises the plan leans on, independently confirmed.**
`grep -rn 'CrossPointSettings\|SettingsList' test/` is empty, so step 1's `#error` cannot
break the host suite today — **A11**'s latent trap stays latent. And the erase block at
`SettingsList.h:459-467` does drop `STR_FRONT_BTN_FOLLOW_ORIENTATION`,
`STR_SUNLIGHT_FADING_FIX` and `STR_BACK_SHORT_TO_FILE_BROWSER` under
`if (BoardConfig::hasTouch())`, which is true on the X4 Pro — so **A5(iv)**'s "this repo
already un-persists keyed settings on this exact board" is real, and **A5** does not need
human adjudication.

**Step 6's device script is executable as written.** Only three `STR_CAT_READER` entries
lack `.withTextSettings()` — `STR_ORIENTATION`, `STR_IMAGES`, `STR_NIGHT_MODE` — so with
orientation gated, `readerSettings` is `{IMAGES, NIGHT_MODE}` plus the three ACTION rows
inserted at `SettingsActivity.cpp:86-90`, giving exactly the five rows plan:518-522 tells
the tester to expect. This was the spec's pass-0 MAJOR and the correction survived into
the plan intact.

**One argued deviation, correctly flagged, not a finding.** Spec §4.1 places the
capability "immediately above the `ORIENTATION` enum it constrains (`:67`)"; the plan puts
it at file scope above the class (plan:195-198) because that enum is a class member. The
deviation is stated, reasoned, and preserves **A3**'s actual constraint (one derivation,
same header, no new file). The relocation is the better call.

VERDICT: CLEAR
