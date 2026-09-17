# Adversarial review — issue #31 design spec, pass 0

**Reviewed:** `docs/superpowers/specs/2026-09-16-issue-31-design.md` (Design v1)
**Against:** `gh issue view 31` + brief, and `docs/superpowers/research/2026-09-16-issue-31-research.md`
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/refactor-31-unused-i18n-keys` @ `eabbecc0`
**Method:** every empirical claim re-derived by running it. Nothing below is quoted from the spec or
the research note.

## What was re-derived and holds

Reported first so the findings are read against a verified baseline, not a suspicion of one.

| Spec claim | Where | Re-derived result |
|---|---|---|
| `Total: 443 \| Used in code: 420 \| Never used: 23` | `:24` | Exact. The 23 names match A1's list plus `STR_HIGHLIGHTS_TOO_LARGE`. |
| A7 — generated C++ byte-identical before/after | `:233-238` | `diff -r` over `--strip-unused` output from today's YAMLs vs. a scratch copy with the 22 removed: **exit 0, no differences.** |
| A2 — post-edit report is `Total: 421 \| Used in code: 420 \| Never used: 1` | `:137` | Exact, and `Unused keys (1): - STR_HIGHLIGHTS_TOO_LARGE`. |
| A5 — orphan gate reads 23 after a stripped generate, 0 after an unstripped one | `:186-191` | Exact. 420 `StrId`s in `I18nKeys.h` → 23 lines; 443 → 0 lines. Same YAMLs, same commit. |
| Per-file deletion counts, 666 total | `:304-316` | Exact, all 32 rows. `diff -ru`: **32 files, 666 deletions, 0 insertions.** |
| Key counts per file, `11,377 → 10,711` | `:316` | Exact (sum of the per-file rows; a `cat \| grep -c` reads 11,376 only because `arabic.yaml` has no trailing newline — see MINOR 6). |
| Error handling case 1 (live key gone from English) | `:390` | `CRITICAL: 1 string(s) used in source but missing from english.yaml`, **exit 1.** |
| Error handling case 2 (live key gone from Spanish, text differs) | `:391`,`:397-405` | Generator **exit 0, no CRITICAL, 420 keys** — silent. The A7 diff catches it: `I18nStrings.cpp` differs. |
| Error handling case 3 (live key gone from Spanish, text equals English) | `:392`,`:407-411` | `diff -r` identical, as claimed. Dedup never stored it. |
| A3 — 17 non-English-only keys, 19 lines, 844 bytes | `:146-152` | Exact, key-for-key and byte-for-byte. |
| A6 — every non-blank line is `^KEY: value`, no comments, no duplicates | `:204-206` | Confirmed across all 32 files. Blank separators exist (e.g. `english.yaml:4`, `:432`) but the simulated edit leaves no doubled, leading or trailing blank. |
| Criterion 4 — `_language_name`/`_language_code`/`_order` in 32/32 | `:79-82` | 32/32 before **and** after the simulated edit. |
| `STR_HIGHLIGHTS_TOO_LARGE` in 1/32 files | `:144` | Only `english.yaml:390`. |
| `getCharacterSet` is dead API | `:337-341` | Zero callers outside `I18n.h:30` / `I18n.cpp:53`. |
| `STR_LOADING_POPUP` live at six sites | `:167-170` | All six exist at the cited file:line. |
| #39 quotes (the instruction, and 219 records / 140 chapters) | `:126-130` | Verbatim in #39's body and its follow-up comment. |
| Gate 8 — `cmake -S test -B …`, not Ninja | `:471-473` | `ninja not found`; the Makefile configure succeeds. `test/` has **zero** `I18n`/`StrId`/`STR_`/`tr(` hits; 51 `add_subdirectory` in `test/CMakeLists.txt:51-113`. |
| Gate 6 baseline `5,314,782 B` / RAM `64,052 B` / SUCCESS | `:494-496` | **Reproduced exactly** — see MAJOR 2 for how, since the prescribed command does not run. |
| `clang-format-fix` is a no-op with the `.venv` PATH prefix, exits 1 without it | `:480-485` | Both confirmed (`clang-format 21.1.8` in `.venv/bin`; tree stayed clean). |
| Citations: `platformio.ini:131`, `.gitignore:8-10`, `gen_i18n.py:267,275,831,874-881,916-923,1001`, `I18n.cpp:15-28,24-27,53-60`, `I18n.h:39`, `CrossPointSettings.h:338`, `main.cpp:414`, `english.yaml:34,35,334,346,379,390`, `AGENTS.md:336,709`, `docs/i18n.md:254-260`, `docs/translators.md` (66 lines of credits), `issue-38-design.md:77-78,418`, `issue-28-design.md:424-428`, `phase-0-fork-and-strip.md:731,844` | — | **All correct.** |

Assumption A2 in particular is well-founded and correctly escalated: #39 is open, names #31 by
name, and post-dates #28's rejection — which was a rejection of *reusing* the string, not of the
string existing. Open Question 1 is a real question the spec was right not to answer alone, but the
evidence behind the recommendation is unambiguous, so it is not treated here as a blocker.

---

## MAJOR 1 — A1's cross-branch check is false; 8 of 14 local branches reference a key on the removal list

**Claim.** `:102-105`: "Three independent checks back the set: … and `git grep` across all thirteen
local branches — including the live `feature/buscar-catalog-search` worktree — finds none of them in
any `src/**` or `lib/**` source." Research §7 states the same: "empty on every branch — including
`feature/buscar-catalog-search`, which has a live worktree and is the most plausible source".

**Problem.** It is not empty on every branch. It is non-empty on **eight of the fourteen** local
branches, including the one the spec names by way of reassurance. The third of the three
"independent checks" — the one that is specifically about integration risk from in-flight work —
does not say what the spec says it says.

**Evidence.** Word-boundary `git grep` per key per branch over `src/*.cpp src/*.h lib/*.cpp
lib/*.h`, excluding `I18nKeys.h`:

```
docs/berean-os-design            STR_ADD_SERVER(3) STR_CALIBRE_URL_HINT(1) STR_CHECKING_WIFI(1)
                                 STR_DELETE_SERVER(1) STR_DISPLAY_QR(2) STR_ERROR_MSG(1)
                                 STR_FETCH_FEED_FAILED(1) STR_FMT_AUTHOR_TITLE(3) STR_FMT_TITLE(3)
                                 STR_FMT_TITLE_AUTHOR(3) STR_LOADING(6) STR_NEXT_PAGE(1)
                                 STR_NO_ENTRIES(2) STR_NO_SERVERS(1) STR_NO_SERVER_URL(1)
                                 STR_PARSE_FEED_FAILED(1) STR_PASSWORD(3) STR_PREV_PAGE(1)
                                 STR_SERVER_NAME(2) STR_TAP_TO_RETRY(1) STR_USERNAME(3)
                                 STR_WIFI_CONN_FAILED(2)          <- all 22
feature/buscar-catalog-search    STR_DISPLAY_QR(2)
feature/enhancements             STR_DISPLAY_QR(2)
feature/launcher-covers-buscar-meetings  STR_DISPLAY_QR(2)
feature/publications-library     STR_DISPLAY_QR(2)
fix/27-atomic-store-saves        STR_DISPLAY_QR(2)
fix/30-launcher-wake-refresh     STR_DISPLAY_QR(2)
fix/37-gate-screen-rotation      STR_DISPLAY_QR(2)
worktree-agent-a968fc68fe6b50252 STR_DISPLAY_QR(2)
main / refactor/31 / refactor/38 / fix/28 / fix/launcher-settings-tile-label   CLEAN
```

Concretely, on `feature/buscar-catalog-search`:

```
src/activities/reader/EpubReaderMenuActivity.cpp:77:  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
src/activities/reader/QrDisplayActivity.cpp:34:  ... tr(STR_DISPLAY_QR), nullptr);
```

These are branches that simply predate `4a107d5c` ("remove Show page as QR"), not branches that
reintroduce the key — and `git branch | wc -l` is now **14**, not thirteen. The practical risk is
lower than the raw counts suggest (a merge of a branch that did not *touch* those lines keeps main's
deletion), but it is not zero, and it is exactly the risk A1's third check exists to rule out.

**Concrete fix.** Rewrite A1's third bullet to say what is true and keep the conclusion:

> `git grep` across all fourteen local branches finds no branch that *introduces* a new reference to
> any of the 22. Eight branches still carry the pre-`4a107d5c` QR code and so reference
> `STR_DISPLAY_QR` in `src/` (`EpubReaderMenuActivity.cpp:77`, `QrDisplayActivity.cpp:34`); the
> abandoned `docs/berean-os-design` branch predates the Phase 0 strip and carries all 22. Neither
> resurrects a reference on `main` unless one of those branches is merged without first rebasing
> past `4a107d5c` — in which case Error handling case 1 fails the build loudly.

Then keep the existing Risks bullet ("The list drifts between measurement and edit") as the
mitigation it already is, and leave gate 0 mandatory.

---

## MAJOR 2 — gate 6's build command does not run in this environment; the prescribed `pio` is broken

**Claim.** `:462` `~/.platformio/penv/bin/pio run`, restated from the brief's working agreement
("the bare `pio` is not on PATH"). The spec hangs its most-quoted number on it: the
`5,314,782 B` flash baseline at `:494-496`, and "A moving flash figure is a failure signal".

**Problem.** `~/.platformio/penv/bin/pio` is currently unexecutable. Its shebang points at a venv
`python` symlink that Homebrew's python upgrade has orphaned. An implementer running gate 6 verbatim
gets a one-line interpreter error, no build, and no way to tell it apart from something they broke.

**Evidence.**

```
$ ~/.platformio/penv/bin/pio run
zsh: /Volumes/stein/.platformio/penv/bin/pio: bad interpreter:
     /Volumes/stein/.platformio/penv/bin/python: no such file or directory

$ head -1 ~/.platformio/penv/bin/pio
#!/Volumes/stein/.platformio/penv/bin/python

$ ls -l ~/.platformio/penv/bin/python
... python -> /opt/homebrew/Cellar/python@3.14/3.14.5/Frameworks/Python.framework/Versions/3.14/bin/python3.14

$ ls /opt/homebrew/Cellar/python@3.14/
3.14.7                         # 3.14.5 is gone
```

**This does not invalidate the baseline.** Routed around the dead symlink, the build succeeds and
the spec's figures reproduce to the byte:

```
$ PYTHONPATH="$HOME/.platformio/penv/lib/python3.14/site-packages" \
    /opt/homebrew/bin/python3 -m platformio run
  Stripping 23 unused string(s) from output.
  Languages: 32 | String keys: 420
RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)
Flash: [========  ]  81.1% (used 5314782 bytes from 6553600 bytes)
========================= [SUCCESS] Took 59.02 seconds =========================
```

(PlatformIO Core 6.1.19 imports cleanly under system python 3.14.7 — same minor version, so the
venv's `site-packages` is compatible.)

**Concrete fix.** Add a line to the Testing strategy, immediately above gate 6:

> `~/.platformio/penv/bin/pio` is broken on this host: its shebang points at a venv `python` symlink
> orphaned by a Homebrew `python@3.14` upgrade (`3.14.5` → `3.14.7`). Repair it once with
> `ln -sfn /opt/homebrew/Cellar/python@3.14/3.14.7/Frameworks/Python.framework/Versions/3.14/bin/python3.14 ~/.platformio/penv/bin/python`,
> or run the build as
> `PYTHONPATH="$HOME/.platformio/penv/lib/python3.14/site-packages" python3 -m platformio run`.
> A `bad interpreter` error is a host problem, not a symptom of this change.

---

## MINOR 3 — A5's "exit status" caveat protects nothing; `i18n_orphans.sh` always exits 0

**Claim.** `:196-198`: run the gate "expecting **1** line (`orphan: STR_HIGHLIGHTS_TOO_LARGE`), never
piped through `wc -l` in a way that discards its exit status." Inherited from
`issue-38-design.md:418` ("the `wc -l` pipe (which discarded the script's exit status) is gone").

**Problem.** There is no exit status to discard. `scripts/i18n_orphans.sh` ends in a `while` loop
that only `echo`es; it returns 0 whether it finds 0 orphans or 23. Gate 7 therefore cannot be
asserted on an exit code by any runner — it must be asserted on the **text** of the output. Stating
the caveat as written invites a plan to wire gate 7 into a `set -e` pipeline that will pass
unconditionally.

**Evidence.**

```
$ ./scripts/i18n_orphans.sh | wc -l
      23
$ ./scripts/i18n_orphans.sh >/dev/null; echo "exit=$?"
exit=0
```

(`scripts/i18n_orphans.sh:9-13` — the `grep | while read … echo` is the last command.)

**Concrete fix.** Replace the caveat with an assertion on output, e.g.

```sh
./scripts/i18n_orphans.sh | tee /dev/stderr | \
  grep -qx 'orphan: STR_HIGHLIGHTS_TOO_LARGE' && \
  [ "$(./scripts/i18n_orphans.sh | wc -l)" -eq 1 ]
```

and note in A5 that the script's exit status is unconditionally 0, so an automated gate must read
its stdout.

---

## MINOR 4 — gate 5's second command exits 1 on the path where it passes

**Claim.** `:459`: `git diff -- lib/I18n/translations/ | grep -c '^+[^+]'   # expect 0`.

**Problem.** `grep -c` prints `0` and exits **1** when it matches nothing. The expected, correct
outcome of this gate is a non-zero exit. Run interactively that is harmless; run inside a `set -e`
verification script — which is how the gate block reads — the gate kills the run precisely when the
change is clean.

**Evidence.**

```
$ bash -c 'set -e; git diff -- lib/I18n/translations/ | grep -c "^+[^+]"; echo SURVIVED'
0
$ echo "outer exit=$?"
outer exit=1          # "SURVIVED" never printed
```

**Concrete fix.** `[ "$(git diff -- lib/I18n/translations/ | grep -c '^+[^+]' || true)" -eq 0 ]`, or
just drop the second command — `git diff --stat` already prints `0 insertions(+)` and is the gate's
real signal.

---

## MINOR 5 — the Architecture prose undercounts why `22 × 32 ≠ 704`, and contradicts its own table

**Claim.** `:318-320`: "`22 × 32 = 704` is the wrong number and a plan that asserts it fails on its
own arithmetic: `danish`, `dutch` and `romanian` never had the five OPDS server keys; `finnish` also
lacks `STR_DISPLAY_QR`; `orangutan` lacks nine."

**Problem.** Those three files lack **seven** of the 22, not five — `STR_NO_SERVERS` and
`STR_TAP_TO_RETRY` on top of the five server keys. As written the sentence yields `22 − 5 = 17`,
which contradicts the table two lines above it (`danish`, `dutch`, `romanian` → **15 each**). The
table is correct and the research §5 breakdown is correct; only this sentence is wrong, and it is
the sentence a plan is most likely to derive per-file counts from.

**Evidence.**

```
danish   lacks: STR_ADD_SERVER STR_DELETE_SERVER STR_NEXT_PAGE STR_NO_SERVERS
                STR_PREV_PAGE STR_SERVER_NAME STR_TAP_TO_RETRY            (7 -> 22-7 = 15)
dutch    lacks: (same 7)                                                  (15)
romanian lacks: (same 7)                                                  (15)
finnish  lacks: those 7 + STR_DISPLAY_QR                                  (8 -> 14)
orangutan lacks: STR_CALIBRE_URL_HINT STR_ERROR_MSG STR_FETCH_FEED_FAILED
                 STR_NO_ENTRIES STR_NO_SERVERS STR_NO_SERVER_URL
                 STR_PARSE_FEED_FAILED STR_TAP_TO_RETRY STR_WIFI_CONN_FAILED  (9 -> 13)
```

**Concrete fix.** "…: `danish`, `dutch` and `romanian` never had the five OPDS server keys, nor
`STR_NO_SERVERS` or `STR_TAP_TO_RETRY` — seven each; `finnish` lacks those seven plus
`STR_DISPLAY_QR`; `orangutan` lacks nine. Derive every per-file count from the table above, never by
multiplication."

---

## MINOR 6 — `arabic.yaml` has no trailing newline, and A6's own suggested alternative would break gate 5

**Claim.** A6 at `:220`: "no trailing-whitespace cleanup, no touching a file's final newline", with
an attack surface that invites "a Python edit for portability" instead of `sed -i ''`.

**Problem.** Exactly one of the 32 files ends without a newline, and the spec never names it. The
line-delete is safe there only by luck — `arabic.yaml`'s last line is `STR_RECOVERY_MODE_HINT`,
which is not on the removal list, so BSD `sed` leaves the missing newline alone (verified). A
Python rewriter built on `splitlines()` + `'\n'.join()` + a trailing `'\n'`, which is the obvious
portable form A6 nudges toward, would append one — turning gate 5's "0 insertions" into 1 and
producing a `\ No newline at end of file` hunk that looks like scope creep in review.

**Evidence.**

```
$ for f in lib/I18n/translations/*.yaml; do [ -n "$(tail -c 1 "$f")" ] && echo "NO NEWLINE: $f"; done
NO NEWLINE: lib/I18n/translations/arabic.yaml

$ tail -1 lib/I18n/translations/arabic.yaml
STR_RECOVERY_MODE_HINT: "ضع الملف firmware.bin في المجلد الرئيسي لبطاقة SD ثم اختره"

# after the simulated 22-key delete with `sed -i ''`, arabic.yaml still ends mid-byte, no \n
```

**Concrete fix.** Name the file in A6: "`arabic.yaml` is the one file with no terminating newline
(`tail -c 1` is non-empty); its last line is not on the removal list, so a line delete preserves
that. Any implementation that reads and rewrites whole files must preserve it explicitly — gate 5's
0-insertions expectation depends on it."

---

## Not raised, and why

- **Open Question 1 / A2's restatement of acceptance criterion 5.** The brief's criterion 2 delegates
  this decision ("Check before deciding"), the spec checked, and #39 — open, later, and naming #31 —
  is explicit. Criterion 5's "zero unused keys" is unsatisfiable *given criterion 2*, so the conflict
  is in the issue, not the spec. Escalating it in Open Questions is the right handling; it is not a
  defect and is not counted as a finding here.
- **A3's 17 non-English-only keys.** Verified exactly (17 keys, 19 lines, 844 bytes) and the
  out-of-scope reasoning is sound: they are invisible to every gate the issue's criteria are phrased
  against, so folding them in would give this task an assertion its own detector cannot make.
- **A4's knowingly-stale `AGENTS.md:336,709`.** Both lines verified; `CLAUDE.md` is a symlink to
  `AGENTS.md`. The trade-off is stated and the follow-up recorded. A judgement, not an error.

**Findings: 0 BLOCKER, 2 MAJOR, 4 MINOR.** Neither MAJOR reverses a decision, changes scope, or
needs a judgement only the human can make: MAJOR 1 corrects an evidence sentence without moving the
removal set, and MAJOR 2 is host tooling with a verified one-line repair. All six are fixable inline.

VERDICT: CLEAR
