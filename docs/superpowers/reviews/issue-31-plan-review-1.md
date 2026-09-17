# Adversarial review — issue #31 implementation plan, pass 1

**Reviewed:** `docs/superpowers/plans/2026-09-16-issue-31-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-31-design.md` (Design v2, rebased) and
`docs/superpowers/reviews/issue-31-plan-review-0.md` (BLOCKER: 1 BLOCKER, 1 MAJOR, 5 MINOR)
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/refactor-31-unused-i18n-keys` @ `ee515a80`
**Method:** the plan was dry-run end to end. `lib/I18n/translations/` was copied to a scratch
directory and every edit applied there, with the generator invoked from the worktree root so it
scanned the real `src/` and `lib/`; `git diff --no-index` stood in for the `git diff` assertions.
The real build (`~/.platformio/penv/bin/pio run`), the host suite, the orphan gate, the format
wrapper and the `--ignored` status gate were all run against the worktree. The repository's own
`lib/I18n/translations/` was not modified; `git status --short` is empty and
`git diff -- lib/I18n/translations/` is empty at the end of the review.

## Pass-0 findings: were they really fixed?

| # | Pass-0 finding | Applied? | Independent check |
|---|---|---|---|
| BLOCKER 1a | Rebase decision | **Yes, correctly** | `git merge-base HEAD origin/main` → `39fb1aef`; `git log --oneline HEAD..origin/main \| wc -l` → `0`. The branch really sits on release 1.9.7. |
| BLOCKER 1a | Every count re-derived on the new base | **Yes, correctly** | Measured: `String keys: 442` / `Unused keys: 23`; stripped snapshot `419`; after the edit `420` / `1`; `english.yaml` 445 → 423; `spanish.yaml` 412 → 390; totals 11,375 → 10,709 (exact, summing per-file rather than `cat`-ing — a `cat \| grep -c` reads 11,374 because `arabic.yaml` has no final newline); Flash `5314686`, RAM `64052`. Every figure in the plan's changelog table (`:700-711`) and the spec's rebase table (`:608-620`) reproduces. |
| BLOCKER 1a | Step 0 drift guard against `origin/main` | **Yes** (one half vacuous — MINOR 2) | `git log --oneline HEAD..origin/main \| wc -l` → `       0`, padded exactly as `:111` says. |
| BLOCKER 1b | Gate 3 → `git diff --stat HEAD~2 HEAD` | **Yes, and `HEAD~2` is correct** | The plan commits exactly twice (Step 2 `:407`, Step 3 `:519`), so `HEAD~2` is `ee515a80` and the range spans exactly this change. Simulated: `32 files changed, 666 deletions(-)`, zero insertions. Invariant under rebase, as claimed. |
| MAJOR 1 | Step 1 derives `$VKEY` instead of hardcoding | **Yes, and it works** | The `:223-237` snippet yields `STR_ADD_HIDDEN_NETWORK` (`english.yaml:63` "Add hidden network...", `spanish.yaml:373` "Añadir red oculta..."), exactly as `:246` predicts. Full Step 1: `generator exit=0`, `0` CRITICALs, `GATE CAUGHT IT`. The `sorted(en)` iteration is safe by accident but reliably: `S` (0x53) sorts before `_` (0x5F), so `_language_name` — which also matches the loader regex and also differs between the two files — can never be selected. |
| MINOR 1 | Gate 2 expected block | **Yes, exact** | `--verbose \| sed -n '/Unused keys/,/^$/p'` on the edited tree prints exactly the four lines at `:552-556`. |
| MINOR 1 | Gate 4 expected block | **Half-applied, and a new error introduced** | See MINOR 1 below. |
| MINOR 1 | BSD `wc -l` padding called out | **Two of four sites** | See MINOR 6 below. |
| MINOR 2 | `--ignored` gate added | **Yes, exact** | `git status --short --ignored lib/I18n/` prints exactly the three `!!` lines at `:614-616`, nothing else. |
| MINOR 3 | Step 3 commit message says 644/31 | **Yes** | `:526-527` now reads "644 lines across these 31 files … 666 across all 32 once Step 2's english.yaml commit is counted with it." Arithmetic verified: 666 − 22 = 644. |
| MINOR 4 | Red/green reframing | **Yes, genuinely** | `:303` reads `22` before the edit and `0` after; `:433` reads `31` before and `0` after. These are the same command both times and the before-run really returns the wrong answer. "About TDD here" (`:40-62`) describes this honestly. |
| MINOR 5 | `\|\| true` on Step 1's `grep -c CRITICAL` | **Yes** | `:255` carries it, with the reason inline at `:258-259`. |

## What else was re-derived and holds

Every executable assertion in Steps 0–4 was run. All of them pass, with the literal values the plan
states:

| Plan claim | Where | Re-derived |
|---|---|---|
| `Languages: 32 / String keys: 442 / Unused keys: 23` | `:131-135` | Exact, and `tail -3` frames it exactly. |
| The 22-key list == the generator's 23 minus `STR_HIGHLIGHTS_TOO_LARGE` | `:167-188` | Exact, key for key, against `--verbose`'s `Unused keys (23):` block. |
| Before-snapshot `String keys: 419`, exactly three files | `:152,157` | Exact. |
| `SEDEXPR` expands to the literal string at `:324` | `:317-324` | **Byte-identical**, trailing `;` included. BSD `sed` accepts it; `paste -sd'\|'` behaves as assumed. |
| Step 2: `442` → `420`; `STR_LOADING_POPUP` / `_FONT_LIST` survive at lines 34 and 323 | `:311,346-352` | Exact. `STR_FMT_TITLE*` count `0`; `STR_HIGHLIGHTS_TOO_LARGE` still present. |
| Step 2: `1 file changed, 22 deletions(-)` | `:365` | Exact, no insertions. |
| Step 2: post-edit report `String keys: 420 / Unused keys: 1` | `:373-377` | Exact, and the intermediate tree generates with no WARNING/CRITICAL of any kind. |
| Step 2: generated C++ already `IDENTICAL` after the English-only edit | `:387` | Exact — `diff -r` silent. |
| Step 3: `31` files match before, `0` after | `:433-458` | Exact. |
| Step 3 per-file table (26 × 22, danish/dutch/romanian 15, finnish 14, orangutan 13) | `:475-480` | **All 31 rows exact, by filename**, every file `+0`; 26·22 + 3·15 + 14 + 13 = 644. With `english.yaml` that is 27 files at 22 and `32 files changed, 666 deletions(-)`. |
| Metadata keys `32` on each of three lines | `:490-496` | Exact after the edit. |
| `arabic.yaml` is the sole file with no final newline; `0` "No newline" hunks | `:498-506` | Exact — its last byte is `"` before and after, and the diff carries no `\ No newline` marker. |
| Gate 1: post-edit generated C++ `IDENTICAL` to the snapshot | `:544-547` | Exact, all three files. |
| Gate 4: build succeeds, `RAM 64052`, `Flash 5314686`, generator `Flash: 241,894 B strings (deduped) + 28,288 B offset tables = 270,182 B` | `:571-581` | **All exact to the byte.** Also verified on a second, no-op build: PlatformIO still prints the RAM/Flash summary when nothing recompiles, so the gate cannot go silent on the success path. |
| Gate 5: orphan gate prints exactly `orphan: STR_HIGHLIGHTS_TOO_LARGE` after a stripped build, and exits 0 either way | `:589-594` | Exact. A5's asymmetry also reproduces: 23 orphans after a stripped generate, `0` after an unstripped one, same sources. |
| Gate 6: default cmake generator, host suite green | `:598-600` | `100% tests passed out of 563`. |
| Gate 7: `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` is a no-op | `:607-608` | exit 0, no output, `git status --short` empty. |
| Gate 8: three `!!` lines | `:612-616` | Exact. |
| `test/` is i18n-free | `:43-44` | 0 hits for the plan's grep; 0 hits for `I18n` anywhere under `test/`. |
| Citations `platformio.ini:131`, `gen_i18n.py:874-881,1001`, `i18n_orphans.sh:9-13`, `.gitignore:8-10`, `AGENTS.md:336,709` | — | **All correct on this base.** |

Every spec requirement maps to a step: spec gates 0–9 (`:454-513`) land as plan Step 0, Step 0,
gate 1, gate 2, Step 3's metadata loop, gate 3 + Step 3's `--numstat`, gate 4, gate 5, gate 6 and
gate 8; A2/A3/A4/A5/A6/A9 all appear in "What you must NOT do" (`:630-654`); A8 is satisfied a
fortiori by "no `.cpp`, `.h`, `.c` … file … is edited" (`:20-21`). There are no placeholders, no
"update the relevant files", no invented types or signatures, and both commits leave a tree that
builds and generates byte-identical C++ — I verified the intermediate state after Step 2
independently. An implementer with no other context can execute this literally and arrive at the
spec. The findings below are presentation defects, not execution defects.

---

## MINOR 1 — gate 4's expected block lists the lines in the wrong order, says "five" while listing six, and omits the seventh line the command prints

**Claim.** `:573-581`:

```sh
grep -E 'Stripping|String keys|RAM:|Flash:|SUCCESS' /tmp/i18n-build.log
# expect these five, in this order -- note the FIRST Flash line is the
# generator's own string accounting, not the binary's:
#   "Stripping 1 unused string(s) from output."       (was 23)
#   "  String keys: 419"                              (unchanged)
#   "  Flash: 241,894 B strings (deduped)  +  28,288 B offset tables  =  270,182 B"
#   ...
```

**Problem.** Three errors in one block, in the plan's most consequential gate. The order is wrong:
the generator's `Flash: … strings (deduped)` accounting line is printed **first**, before
`Stripping` and `String keys`, not third. The count word is wrong: it says "five" and lists six.
And the grep matches a **seventh** line the block does not mention — PlatformIO's per-environment
summary row. This is a half-applied pass-0 MINOR 1: the missing `Flash: … strings` line was added,
but inserted at the wrong position, and the newly-added phrase "in this order" is itself the new
error. The plan's whole method is literal comparison against a stated expectation
(`:582` — "A DIFFERENT FLASH FIGURE IS A FAILURE SIGNAL"), so an implementer who finds the output in
a different order, one line longer than promised, at the end of a ~5-minute build, has to decide
unaided whether it matters.

**Evidence.** Real `pio run` on this worktree, exit 0, the log's own line numbers:

```
61:  Flash: 241,894 B strings (deduped)  +  28,288 B offset tables  =  270,182 B
63:  Stripping 23 unused string(s) from output.
67:  String keys: 419
687:RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)
688:Flash: [========  ]  81.1% (used 5314686 bytes from 6553600 bytes)
694:========================= [SUCCESS] Took 46.85 seconds =========================
698:x4pro          SUCCESS   00:00:46.849
```

Reproduced identically on a second, no-op build (line numbers 61/63/67/139/140/141/145).

**Concrete fix.** Replace the block at `:574-581` with the real order and count:

```
# expect these SEVEN lines, in this order -- note the FIRST line is the
# generator's own string accounting, not the binary's:
#   "  Flash: 241,894 B strings (deduped)  +  28,288 B offset tables  =  270,182 B"
#   "  Stripping 1 unused string(s) from output."     (was 23)
#   "  String keys: 419"                              (unchanged)
#   "RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)"
#   "Flash: [========  ]  81.1% (used 5314686 bytes from 6553600 bytes)"
#   "========================= [SUCCESS] Took ... ========================="
#   "x4pro          SUCCESS   00:00:NN.NNN"
```

## MINOR 2 — the spec's rebase section claims every `file:line` citation was re-verified, and that the two YAMLs are not cited by line; both `english.yaml` citations are cited by line and both are now off by one

**Claim.** Spec `:637-638`, under "What did not move, re-derived and confirmed":

> **Every `file:line` citation in this spec.** None of the cited files were touched by the four
> commits except the two YAMLs, and neither is cited by line.

**Problem.** `english.yaml` is cited by line twice, and `02d9106a` deleted `STR_TAGS_AND_SETTINGS`
from line 11 of that file, shifting everything below it up by one. Both citations are stale. This
matters less for what it breaks — nothing in the plan depends on them — than for what it says: the
rebase section's value is the claim that *everything* was re-derived rather than adjusted on paper,
and the one sentence asserting an exhaustive sweep is the sentence that is false.

**Evidence.**

```
$ grep -n 'english.yaml:' docs/superpowers/specs/2026-09-16-issue-31-design.md
179:`english.yaml:35`) is live at six sites — `src/main.cpp:184,197`,
224:`english.yaml:334` sits between `STR_STEP_HINT_SIDE` and `STR_SERVER_NAME`, not

$ grep -n '^STR_LOADING_POPUP:\|^STR_STEP_HINT_SIDE:\|^STR_ADD_SERVER:\|^STR_SERVER_NAME:' lib/I18n/translations/english.yaml
34:STR_LOADING_POPUP: "Loading"          <- A4 cites :35
332:STR_STEP_HINT_SIDE: "Side buttons:"
333:STR_ADD_SERVER: "Add Server"         <- A6 cites :334
334:STR_SERVER_NAME: "Server Name"

$ git show c48e3ec7:lib/I18n/translations/english.yaml | grep -n '^STR_LOADING_POPUP:\|^STR_ADD_SERVER:'
35:STR_LOADING_POPUP: "Loading"
334:STR_ADD_SERVER: "Add Server"
```

The *substance* of both citations survives: `STR_ADD_SERVER` still sits between
`STR_STEP_HINT_SIDE` and `STR_SERVER_NAME`, so A6's "interleaved with live ones, not in an OPDS
block" argument holds.

**Concrete fix.** Spec `:179` → `english.yaml:34`; spec `:224` → `english.yaml:333`; and reword
`:637-638` to "…except the two YAMLs; `english.yaml` is cited by line at A4 and A6 and both
citations were shifted up by one, corrected above."

## MINOR 3 — Step 0's second drift check can never fail once the first one passes

**Claim.** `:116-121`:

```sh
git diff --stat HEAD...origin/main -- lib/I18n/translations/
```

> **Expect empty.** Non-empty means `main` has edited the very files this task owns…

**Problem.** The three-dot form is `git diff $(git merge-base HEAD origin/main) origin/main`. It
only ever runs after the preceding guard (`:107`) has established that
`git log --oneline HEAD..origin/main` is empty — i.e. that `origin/main` is an ancestor of `HEAD`,
so the merge-base *is* `origin/main` and the diff is `origin/main` against itself. It is empty by
construction, for every possible repository state in which the implementer reaches it. It cannot
detect the condition it is described as detecting. Harmless, but it is a gate that asserts nothing,
in the one place the plan added specifically to stop asserting nothing.

**Evidence.**

```
$ git merge-base HEAD origin/main | xargs git log --oneline -1
39fb1aef chore(main): release 1.9.7 (#56)
$ git log --oneline HEAD..origin/main | wc -l
       0
$ git diff --stat HEAD...origin/main -- lib/I18n/translations/
$                                        # empty, necessarily
```

**Concrete fix.** Either drop it, or make it the check it is described as — two dots, so it measures
`origin/main` against the working tree and stays meaningful if the first guard is ever relaxed:

```sh
git diff --stat origin/main -- lib/I18n/translations/   # expect empty
```

(with `git status --short` already asserted empty at `:94`, this reduces to the same fact, but at
least it is a fact about the two trees rather than about one tree twice).

## MINOR 4 — Step 0 re-derives the unused *count* but never the unused *names*, while the 22-key list is a hardcoded heredoc

**Claim.** `:126-142` runs the generator and asserts `String keys: 442` / `Unused keys: 23`, with
`:140-142` explaining that "`Unused keys` is the number that matters: if it is not 23, a key on the
list has become live or a new orphan has appeared". `:166-189` then writes the 22 names into
`/tmp/i18n-remove.txt` from a literal heredoc, and `:191-194` checks only that the file has 22 lines
and no `HIGHLIGHTS`.

**Problem.** The count is not the set. A world in which one listed key becomes live and one new
orphan appears leaves `Unused keys: 23` unchanged while the removal set is wrong — and the plan's
own sentence at `:140-142` asserts the opposite. The spec's gate 0 (`:455-457`) uses
`--verbose | sed -n '/Unused keys/,/^$/p'`, which prints the **names**, and A1's attack surface
(`:126-130`) is explicit that "the plan must re-run the generator immediately before editing, not
trust this list". The plan re-runs the generator but still trusts a typed list against it.

The residual risk here is near zero — `:94` asserts a clean tree, `:107` asserts `origin/main` has
not moved, and a live key deleted from `english.yaml` fails loudly at `:379-382` — which is why this
is MINOR and not MAJOR. But the fix removes the typed list entirely, which is strictly better than
hardening it.

**Evidence.** `--verbose` already prints exactly what is needed, and it matches the heredoc:

```
$ python3 scripts/gen_i18n.py lib/I18n/translations /tmp/out --verbose | sed -n '/Unused keys/,/^$/p'
  Unused keys (23):
    - STR_ADD_SERVER
    ...
    - STR_HIGHLIGHTS_TOO_LARGE
    ...
    - STR_WIFI_CONN_FAILED
```

**Concrete fix.** Replace the heredoc at `:166-189` with a derivation, and keep the two existing
assertions as the guard on it:

```sh
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-before --strip-unused --verbose \
  | sed -n 's/^    - //p' | grep -v '^STR_HIGHLIGHTS_TOO_LARGE$' > /tmp/i18n-remove.txt
wc -l < /tmp/i18n-remove.txt   # expect 22 (BSD wc pads: "      22")
```

then keep the printed list in the plan as the *expected* content, checked with a `diff`, rather than
as the source of truth.

## MINOR 5 — two explanatory asides do not survive checking

**Claim.** `:482-484` (repeated verbatim in the spec at `:342-344`): "`danish`, `dutch` and
`romanian` never had the five OPDS server keys, nor `STR_NO_SERVERS` or `STR_TAP_TO_RETRY` — seven
each". And `:327-329`: "`STR_LOADING` is a prefix of `STR_LOADING_POPUP` and
`STR_LOADING_FONT_LIST`, both of which are live at six call sites between them."

**Problem.** The counts (7, and the 15/14/13 table) are right; the composition is not. The seven
keys those three files lack include `STR_NEXT_PAGE` and `STR_PREV_PAGE` — which are not server keys
and are not mentioned — and exclude `STR_NO_SERVER_URL`, which those files *do* have. Whatever
"the five OPDS server keys" denotes, it is not the set that is missing. This sentence entered via
spec review pass 0's MINOR 5, so the "correction" fixed the arithmetic and left the account wrong.
Separately, the two `STR_LOADING*` neighbours have **seven** call sites between them, not six; the
spec's own A4 (`:179-182`) correctly says six for `STR_LOADING_POPUP` alone, and the plan
generalised it to both keys without re-counting.

**Evidence.**

```
$ for b in danish dutch romanian; do ... done          # keys on the removal list absent from each
danish/dutch/romanian each lack exactly:
  STR_ADD_SERVER, STR_DELETE_SERVER, STR_NEXT_PAGE, STR_NO_SERVERS,
  STR_PREV_PAGE, STR_SERVER_NAME, STR_TAP_TO_RETRY        (7 -> 22-7 = 15)
$ grep -n 'STR_NO_SERVER_URL' lib/I18n/translations/danish.yaml
166:STR_NO_SERVER_URL: "Ingen server-URL konfigureret"     # present, contra the prose

$ grep -rn "STR_LOADING_POPUP" src lib --include=*.cpp --include=*.h   # 6 sites
$ grep -rn "STR_LOADING_FONT_LIST" src lib --include=*.cpp --include=*.h
src/activities/settings/FontDownloadActivity.cpp:638                   # 1 site -> 7 total
```

`finnish` lacking "those seven plus `STR_DISPLAY_QR`" (8 → 14) and `orangutan` lacking nine (→ 13)
are both **correct** as written.

**Concrete fix.** `:483` → "never had `STR_ADD_SERVER`, `STR_DELETE_SERVER`, `STR_SERVER_NAME`,
`STR_NO_SERVERS`, `STR_TAP_TO_RETRY`, `STR_NEXT_PAGE` or `STR_PREV_PAGE` — seven each"; same edit at
spec `:343`. `:329` → "live at seven call sites between them (six for `STR_LOADING_POPUP`, one for
`STR_LOADING_FONT_LIST`)".

## MINOR 6 — the BSD `wc -l` padding note is carried at two of the four places it is asserted

**Claim.** Pass-0 MINOR 1 asked for the padding to be stated where `wc -l` output is compared. The
plan carries it at `:111` (`"       0"`) and `:457-458` (`"       0"`).

**Problem.** The two remaining `wc -l` assertions do not: `:191`
(`wc -l < /tmp/i18n-remove.txt  # expect: 22`, which really prints `      22`) and `:436`
(`Expect **`31`**`, which really prints `      31`). Same half-carried-fix shape as pass-0's MINOR 5.

**Evidence.**

```
$ wc -l < /tmp/i18n-remove.txt
      22
$ grep -l -E "..." lib/I18n/translations/*.yaml | wc -l
      31
```

**Concrete fix.** Add "(BSD `wc` pads: `      22`)" at `:191` and "(padded, as at `:458`)" at `:436`.

---

## Not raised, and why

- **Acceptance criterion 5 / `STR_HIGHLIGHTS_TOO_LARGE`.** Handled correctly and loudly at `:36-38`,
  `:632-636` and `:680-682`, escalated rather than resolved. Verified end state: the generator
  reports `Unused keys (1): - STR_HIGHLIGHTS_TOO_LARGE` and the orphan gate prints exactly one line.
  Not a defect; still the human's call, and it is already recorded as such.
- **The spec's gate 5 (`git diff --stat -- lib/I18n/translations/`) vs the plan's gate 3
  (`HEAD~2 HEAD`).** The plan's form is strictly better and the spec's form is stated as running
  "after the last edit", i.e. pre-commit, where it is correct. No contradiction.
- **The spec's gate 5 insertion test (`[ … -eq 0 ]`) has no literal counterpart in the plan.**
  The plan's `--numstat` loop at `:470-473` ("every line must show `+0`") asserts the same property
  per file rather than in aggregate, which is stronger. Not a gap.
- **`sed -i ''` being BSD-specific.** Named at `:81-82` with the Linux spelling; this host is Darwin.
- **`/tmp` rather than a session scratch directory.** Works, and every use is preceded by `rm -rf`.
- **`pio check` excluded (`:621-623`) and no hardware gate (`:674-677`).** Both argued from gate 1's
  byte-identity, which I reproduced. Correct calls.
- **Commit subject lengths** (52 and 55 chars against `AGENTS.md`'s 50). Too small to spend a finding
  on, and pass 0 declined it too.

**Findings: 0 BLOCKER, 0 MAJOR, 6 MINOR.** The pass-0 BLOCKER and MAJOR are both genuinely and
correctly fixed — the rebase is real, every re-derived figure reproduces to the byte, the drift
guard fires on the condition it names, `HEAD~2` is the right range for a two-commit plan, and the
derived-`$VKEY` vacuity demonstration prints `GATE CAUGHT IT` on this tree. The six MINORs are text
corrections inside the plan and spec; none changes a command, a count, a decision or the scope.

VERDICT: CLEAR
