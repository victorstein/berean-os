# Adversarial review — issue #31 implementation plan, pass 0

**Reviewed:** `docs/superpowers/plans/2026-09-16-issue-31-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-31-design.md` (Design v1) and
`docs/superpowers/reviews/issue-31-spec-review-0.md` (CLEAR, 2 MAJOR + 4 MINOR, all folded in)
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/refactor-31-unused-i18n-keys` @ `7096731a`
**Method:** the plan was dry-run end to end in a scratch tree
(`$SCRATCH/work/translations`, a copy of `lib/I18n/translations/`), with the generator invoked from
the worktree root so it scanned the real `src/` and `lib/`. `git diff --no-index` stood in for the
`git diff` assertions. Every command in Steps 0–4 was executed. The repository's own
`lib/I18n/translations/` was not modified; `git status --short` is empty.

## What was re-derived and holds

| Plan claim | Where | Re-derived result |
|---|---|---|
| Step 0 report `Languages: 32 / String keys: 443 / Unused keys: 23` | `:99-102` | Exact, and `tail -3` frames it exactly as printed. |
| Before-snapshot `String keys: 420`, exactly three files | `:116,121` | Exact: `I18nKeys.h`, `I18nStrings.cpp`, `I18nStrings.h`. |
| The 22-key list == generator's 23 minus `STR_HIGHLIGHTS_TOO_LARGE` | `:130-153` | Exact, key for key. |
| Step 1: generator exit 0, zero `CRITICAL`, gate still catches it | `:205-216` | Exact. `generator exit=0`, `0` CRITICALs, `GATE CAUGHT IT`. |
| `SEDEXPR` expands to the literal string printed at `:254` | `:247-255` | Byte-identical, trailing `;` included; BSD `sed` accepts it. |
| Step 2: `443` → `421`; `STR_LOADING` gone, `STR_LOADING_POPUP`/`_FONT_LIST` survive | `:241,269-273` | Exact (`english.yaml:34` `STR_LOADING`, `:35` `_POPUP`, `:346` `_FONT_LIST` before the edit; two lines after). |
| Step 2: `1 file changed, 22 deletions(-)`, no insertions | `:288` | Exact. |
| Step 2: post-edit report `String keys: 421 / Unused keys: 1` | `:296-300` | Exact. |
| Step 2: generated C++ already `IDENTICAL` after the English-only edit | `:310-315` | Exact — `diff -r` silent. |
| Step 3: `31` files match before, `0` after | `:356-379` | Exact. `paste -sd'\|'` behaves as assumed on BSD. |
| Step 3 per-file table (26 × 22, danish/dutch/romanian 15, finnish 14, orangutan 13) | `:396-405` | **All 32 rows exact**, every file `+0`. Combined total `32 files changed, 666 deletions(-)`; Step 3 alone is 644. |
| The prose behind the short files (7 / 7 / 7, 8, 9 missing keys) | `:403-405` | Exact, key for key. |
| Metadata keys `32` on each of three lines | `:411-417` | Exact, after the edit. |
| `arabic.yaml` keeps its missing final newline; `0` "No newline" hunks | `:419-428` | Exact. |
| Gate 1: post-edit generated C++ `IDENTICAL` to the before-snapshot | `:464-467` | Exact — `diff -r` silent over all three files. |
| Gate 2: `Unused keys (1): - STR_HIGHLIGHTS_TOO_LARGE`, no `CRITICAL` | `:470-475` | Correct (see MINOR 1 for the extra line it also prints). |
| Gate 4: `~/.platformio/penv/bin/pio run` works; `Stripping`, `String keys: 420`, RAM `64052`, Flash `5314782`, `[SUCCESS]` | `:484-491` | **Build exit 0. RAM 64052/327680, Flash 5314782/6553600, SUCCESS in 48.13 s** — the spec's baseline to the byte. The prior review's MAJOR 2 (`bad interpreter`) no longer reproduces; the Environment note at `:70-75` is now correctly a troubleshooting note. |
| Gate 5: orphan gate reads 23 after a stripped build and exits 0 either way | `:495-501` | Exact: 23 lines, `exit=0`. `I18nKeys.h` holds 420 `StrId`s post-build. Removing the 22 leaves exactly `orphan: STR_HIGHLIGHTS_TOO_LARGE`. |
| Gate 6: default generator, host suite green | `:505-509` | `cmake` configure + build exit 0; **`100% tests passed out of 563`**. |
| Gate 7: `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` is a no-op | `:514-516` | exit 0, no output, `git status --short` empty. `clang-format 21.1.8`. |
| `test/` is i18n-free (`:41-42`) | `:41` | 0 hits. The unanchored form's 21 hits are all `c_str(` / `substr(` / `buf.str(`; BSD `grep -E` does honour `\b` (probed), so this is not a repeat of the spec review's MAJOR 1. |
| Citations `platformio.ini:131`, `gen_i18n.py:874-881,1001`, `i18n_orphans.sh:9-13`, `.gitignore:8-10`, `AGENTS.md:336,709`, `docs/i18n.md:254-260`, `CLAUDE.md` is a symlink | — | **All correct.** |
| No `.cpp`/`.h` outside `lib/I18n/translations/` references any of the 22 | `:17-19` | `git grep` over the whole tree: only `AGENTS.md:336,709` (A4) and `docs/`. The removal set is sound **on this tree**. |

The plan is unusually concrete: no placeholders, no "update the relevant files", every command
literal, every expected value spelled out, and the four prior-review fixes carried forward (the
`pio` note at `:70-75`, the stdout-not-exit-status rule at `:497-499`, the per-file table with
"Never derive … by multiplying" at `:406`, and the `arabic.yaml` newline paragraph at `:419-428`).
Steps 2 and 3 each leave a tree that builds and generates identical C++ — I verified the
intermediate state after Step 2 independently. The findings below are about the ground the plan
stands on, not the plan's craft.

---

## BLOCKER 1 — `main` moved after the plan was written, and one of the new commits edits `lib/I18n/translations/`; gate 3 cannot pass, and Step 0's drift guard does not fire

**Claim.** The plan is anchored to `c48e3ec7`. Step 0 (`:96-106`) expects `String keys: 443` and
`Unused keys: 23`, and says "**If it is not 443 and 23, stop and report.** … A different number
means `main` moved". Step 4 gate 3 (`:477-479`):

```sh
git diff --stat main -- lib/I18n/translations/ | tail -1
# expect: 32 files changed, 666 deletions(-)   -- and no insertions
```

**Problem.** `main` is three commits past the merge-base, and `02d9106a` **deletes
`STR_TAGS_AND_SETTINGS` from `english.yaml` and `spanish.yaml`**. Two independent consequences:

1. **Gate 3 fails today, on the success path.** `git diff … main` compares the working tree against
   `main`, and this branch still carries the two lines `main` deleted. The gate prints
   `2 insertions(+)` against a plan that says "and no insertions" — the exact signature the plan
   teaches the implementer to read as scope creep (`:549-551`). They will have done all the work,
   passed gates 1 and 2, and hit an unexplained failure at the final shape check with no way to tell
   it from something they broke.
2. **Step 0's guard is blind to the drift it exists to catch.** It measures the *branch's* working
   tree, which is still `443`/`23`, so it passes and the implementer proceeds. The guard fires only
   *after* someone rebases — at which point it halts the task with no recovery path, because every
   hardcoded number in the plan (and in the spec) is one off.

**Evidence.**

```
$ git log --oneline HEAD..main
f19172a7 docs: the launcher's fourth tile is Ajustes, not tags (#54)
8ca8ed91 chore(main): release 1.9.6 (#55)
02d9106a fix: label the launcher's fourth tile Settings (#53)

$ git merge-base main HEAD | xargs git log --oneline -1
c48e3ec7 chore(main): release 1.9.5 (#52)

$ git show 02d9106a --stat | tail -6
 lib/I18n/translations/english.yaml           |  1 -
 lib/I18n/translations/spanish.yaml           |  1 -
 src/activities/launcher/LauncherActivity.cpp | 10 +++++-----
 src/activities/launcher/LauncherActivity.h   |  6 +++---
```

Gate 3, simulated against `main`'s translations and the fully-edited scratch tree:

```
$ git diff --no-index --stat $SCRATCH/maintrans/lib/I18n/translations $SCRATCH/work/translations | tail -1
 32 files changed, 2 insertions(+), 666 deletions(-)
```

And the numbers on a rebased tree (`git archive main src lib scripts`, generator run from that root):

```
$ python3 scripts/gen_i18n.py lib/I18n/translations out | tail -3
  Languages: 32
  String keys: 442                      <- plan says 443, and says to stop
  Unused keys: 23 (pass --strip-unused to remove them)

$ python3 scripts/gen_i18n.py lib/I18n/translations outstrip --strip-unused | tail -1
  String keys: 419                      <- plan's before-snapshot says 420 (:116)
```

The 23 unused keys on `main` are **the same 23** — the removal set does not move, and no branch
introduces a reference. What moves is every count the plan asserts: `443`→`442` (`:100`, `:241`),
`420`→`419` (`:116`, `:487`), `421`→`420` (`:269`, `:298`), and the `5,314,782 B` flash baseline
(`:489`, `:564`) which must be re-measured because one string leaves the tables. The per-file table
and the `666` total are unaffected (`STR_TAGS_AND_SETTINGS` is not on the removal list) — I verified
that against `main`'s YAMLs.

**Concrete fix.** Two parts, and the first needs the human's decision:

1. **Decide and record whether this branch rebases onto `main` before execution.** If it does, Step 0's
   expected values become `442` / `23`, the before-snapshot becomes `419`, Step 2 becomes `442` → `420`,
   gate 4 becomes `String keys: 419`, and the flash baseline must be re-measured with one `pio run`
   on the rebased tree before the plan is handed to an implementer. The spec's Problem section, the
   per-file "keys before → after" column and the `11,377 → 10,711` total all move with it. If it does
   not rebase, say so explicitly in the plan so the drift is a recorded decision rather than an
   accident, and note that `main` will delete `STR_TAGS_AND_SETTINGS` under the merge.
2. **Make gate 3 rebase-proof** by measuring this change instead of the distance to a moving branch.
   After both commits:

   ```sh
   git diff --stat HEAD~2 HEAD -- lib/I18n/translations/ | tail -1
   # expect: 32 files changed, 666 deletions(-)   -- and no insertions
   ```

   This is invariant under rebase and under anything `main` does, and it is what the gate is actually
   asking about.

---

## MAJOR 1 — Step 1's gate-demonstration hardcodes the one key `main` just deleted, so the demonstration dead-ends on a rebased tree

**Claim.** `:184-216`. Step 1 proves the diff gate can fail by deleting a live key from Spanish only,
and names `STR_TAGS_AND_SETTINGS` — with its expected `grep -n` output pinned to line 11 of both
files and to the text `"Etiquetas y ajustes"`.

**Problem.** `STR_TAGS_AND_SETTINGS` is exactly the key `02d9106a` removed from both YAMLs and from
`LauncherActivity.cpp`. On the current branch it is still live and Step 1 works — I ran it. On a tree
rebased onto `main` the key does not exist, so the `grep` prints nothing, the `sed` deletes nothing,
the output is identical, and Step 1 prints `GATE MISSED IT`. The plan then instructs
(`:216-217`): "your `/tmp/i18n-before` snapshot is wrong (most likely re-taken after an edit); redo
Step 0." That diagnosis is false and the instruction is a loop — the implementer redoes Step 0, gets
the same result, and has no exit. Step 1 is the plan's own stated foundation ("Step 4's gate is the
only guard on a silent mistake … do not skip it", `:170-171`), so it fails closed in the most
expensive way.

Independently of the rebase question, pinning a proof to a key that was being deleted on `main` the
same day the plan was written is a fragility worth removing: the demonstration only needs *a* live
key whose translation differs from English.

**Evidence.** Reproduced against `main`'s tree (`git archive main src lib scripts`):

```
$ grep -n '^STR_TAGS_AND_SETTINGS:' lib/I18n/translations/english.yaml lib/I18n/translations/spanish.yaml
$ echo $?
1                                        # not present anywhere

$ sed -i '' '/^STR_TAGS_AND_SETTINGS:/d' vac/translations/spanish.yaml
$ python3 scripts/gen_i18n.py vac/translations vac/out --strip-unused >/dev/null
$ diff -r outstrip vac/out >/dev/null && echo "GATE MISSED IT"
GATE MISSED IT
```

(On the current branch the same sequence prints `GATE CAUGHT IT`, as the plan says.)

**Concrete fix.** Use a key that is live on both `main` and this branch and whose Spanish differs —
`STR_MEETINGS` is the closest neighbour and is verified on both:

```
english.yaml:10:STR_MEETINGS: "Meetings"
spanish.yaml:10:STR_MEETINGS: "Reuniones"
src/activities/launcher/LauncherActivity.cpp:461:  drawCoverTile(rects[1], meetingsCoverPath, tr(STR_MEETINGS),
src/activities/network/MeetingsActivity.cpp:30:  ... return tr(STR_MEETINGS);
$ git show main:lib/I18n/translations/english.yaml | grep -c '^STR_MEETINGS:'
1
```

Substitute it at `:184-197` (line numbers 10, not 11) and add one sentence: *if the `grep` prints
nothing, the key has been removed upstream — pick any other key that is live and whose Spanish
differs, and do not read a missing key as a gate failure.*

---

## MINOR 1 — two "expect" blocks in Step 4 under-report what the commands actually print

Gate 2 (`:470-475`) expects two lines, but `sed -n '/Unused keys/,/^$/p'` matches the summary line
too:

```
  Unused keys (1):
    - STR_HIGHLIGHTS_TOO_LARGE

  Unused keys: 1 (pass --strip-unused to remove them)      <- not in the expected block
```

Gate 4's grep (`:486`) catches a fourth line the expected block does not mention:

```
  Flash: 241,932 B strings (deduped)  +  28,352 B offset tables  =  270,284 B
  Stripping 23 unused string(s) from output.
  String keys: 420
```

Both are benign, but the plan's whole method is "compare against the literal expected output", and an
implementer who finds an unlisted line has to decide on their own whether it matters. Also cosmetic:
`wc -l` on BSD prints leading spaces (`      22`, `      31`), not `22`/`31` as at `:155`, `:359`,
`:379`, `:417`. **Fix:** add the missing lines to both blocks (and the `Flash: … strings (deduped)`
line is itself a useful invariant — it must read `241,932 B` unchanged), and say the `wc -l` figures
are padded.

## MINOR 2 — spec gate 9's `--ignored` check has no counterpart step

Spec `:510-512` requires both `git status --short` (only YAMLs) **and**
`git status --short --ignored lib/I18n/` showing the three generated files as `!!`. The plan carries
the first (`:322`, `:435`) and the prohibition (`:543-546`), but never the `--ignored` form, which is
the one that actually demonstrates the three files are excluded rather than merely absent. **Fix:**
add it to Step 4 as an eighth gate, `git status --short --ignored lib/I18n/  # expect the three as !!`.

## MINOR 3 — Step 3's commit message describes the whole change, not the commit

`:447`: "666 lines across all 32 files, no insertions." That commit contains **644 deletions across
31 files** (verified: 666 total − Step 2's 22). A reader running `git show --stat` on it gets a
different number than the message claims. **Fix:** "644 lines across the other 31 files, no
insertions — 666 with the english.yaml commit."

## MINOR 4 — the "assertion that must fail" blocks do not fail

`:47-49` sets the method: "run the 'before' command and watch it fail, make the edit, run the 'after'
command and watch it pass". But the before blocks at `:238-241` and `:353-359` are labelled
"**Before (the assertion that must fail)**" while expecting `443` and `31` — i.e. they print their
before-value and *pass*. Nothing red is ever observed except in Step 1. This is a framing defect, not
a mechanical one, but it is the part of the plan an implementer is most likely to mis-execute (by
looking for a failure that never comes). **Fix:** either relabel them "Before (the baseline the after
assertion is checked against)", or make them literal failing assertions, e.g.
`[ "$(grep -c '^STR_' lib/I18n/translations/english.yaml)" -eq 421 ]  # must fail now, pass after`.

## MINOR 5 — the prior review's `grep -c` exit-status fix is carried at Step 3 but not at Step 1

Spec review MINOR 4 established that `grep -c` exits 1 when it prints `0`, and the plan carries that
at `:427-428` ("`grep -c` exits 1 when it prints `0`; that is the passing case here, so do not wrap
this in `set -e` without a `\|\| true`"). The identical pattern at `:202`,
`grep -c CRITICAL /tmp/i18n-vacuity/gen.log  # expect 0`, carries no such note — and it sits two
lines after `echo "generator exit=$?"`, which is where a reader is already attending to exit codes.
A half-carried fix is worse than none, because the silence at `:202` reads as "this one is
different". **Fix:** add the same parenthetical at `:202`, or write it as
`grep -c CRITICAL … || true`.

---

## Not raised, and why

- **Acceptance criterion 5 / `STR_HIGHLIGHTS_TOO_LARGE`.** The plan handles it correctly and
  loudly: `:34-36`, `:530-534`, `:578-580`. The escalation belongs to the human, the spec's
  Open Question 1 stands, and the plan does not quietly resolve it. Not a defect.
- **`sed -i ''` being BSD-specific.** Named at `:77-78` with the Linux spelling given, and this host
  is Darwin (`uname -s` → `Darwin`). Adequate.
- **`pio check` excluded** (`:519-521`) and **no hardware gate** (`:572-575`). Both are argued from
  gate 1's byte-identity, and gate 1 is real — I reproduced it. Correct calls.
- **`/tmp` rather than a session scratch dir.** Works on this host; a stale-directory hazard the plan
  already guards with `rm -rf` before each `mkdir`.
- **Commit subject length.** `:440` is 58 characters against `AGENTS.md`'s 50-char guidance. Too small
  to spend a finding on.

**Findings: 1 BLOCKER, 1 MAJOR, 5 MINOR.** BLOCKER 1 needs a decision only the human or orchestrator
can make — whether this branch rebases onto a `main` that has moved — and that decision changes
numbers in the spec as well as the plan. MAJOR 1 is mechanical but is downstream of the same
decision. The five MINORs are inline fixes.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
MINORS: 5
