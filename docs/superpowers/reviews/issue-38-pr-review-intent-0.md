# PR #41 — stage-1 intent review

**PR:** `refactor/38-remove-qr-display` → `main`, "fix: remove Show page as QR,
which never produced a scannable code"
**Issue:** #38
**Spec:** `docs/superpowers/specs/2026-09-16-issue-38-design.md`
**Plan:** `docs/superpowers/plans/2026-09-16-issue-38-plan.md`
**Scope of this review:** intent only (does it do what was asked, completely,
and nothing else). Code quality is stage 2 and out of scope here.

---

## Method

Read issue #38, the PR body (`gh pr view 41`), the full diff (`gh pr diff 41`),
the spec, the plan, and both upstream review docs
(`issue-38-spec-review-0.md`, `issue-38-plan-review-0.md`, both CLEAR).
Independently re-ran the PR's claimed gates from this worktree rather than
trusting the PR body:

| Claim in PR body | Re-verified | Result |
| --- | --- | --- |
| `pio run -e x4pro` succeeds, Flash 5,311,502 B | yes | `Flash: [========  ]  81.0% (used 5311502 bytes from 6553600 bytes)` — exact match |
| `firmware.bin` 5,312,016 B | yes | `-rw-r--r--@ ... 5312016 ... firmware.bin` — exact match |
| Host suite 543/543 passing | yes | `100% tests passed out of 543` |
| i18n orphans 22 → 23, new one is `STR_DISPLAY_QR` | yes | `./scripts/i18n_orphans.sh \| wc -l` → 23; `grep -i display_qr` → `orphan: STR_DISPLAY_QR` |
| `clang-format-fix` whole tree clean | yes | no output, `git status --short` clean after |
| `QrUtils::drawQrCode` has 3 live callers outside the reader (contradicts issue) | yes | `CrossPointWebServerActivity.cpp:445,463,483` |
| `ricmoo/QRCode` still in `platformio.ini` | yes | `platformio.ini:152` unchanged |
| translations untouched | yes | no `lib/I18n/translations/*.yaml` in the diff |
| commit order matches plan | yes | `git log --oneline main..HEAD` matches the plan's 5 code commits plus docs commits |

---

## Findings

### No BLOCKER, no MAJOR.

### MINOR 1 — Plan's Edit B (row-count comment) dropped pre-emptively, not on an observed conflict

The plan (Step 4, Edit B) called for correcting the stale comment at
`src/activities/reader/EpubReaderMenuActivity.h:53-56` ("13 unconditional …
reaches exactly 18") to "10 … 15", with an explicit escape hatch: "If Edit B
conflicts … drop Edit B and keep Edit A." The PR dropped it, but its stated
reason is scope hygiene ("adding four lines of conflict surface for a
comment"), not a merge conflict actually encountered — this branch is the only
one editing the file at PR time, so there was nothing to observe yet. Confirmed
uncorrected: the comment at `EpubReaderMenuActivity.h:53-56` still reads "13
unconditional items … reaches exactly 18", now off by three (real counts are 10
and 15) instead of the pre-existing two.

This is disclosed prominently in the PR body's "Contested files" section with
the exact reasoning, so it is not a silent scope reduction — the brief's bar
("no divergence … that the PR does not explain") is met. It is not scope-gating
because the comment was already stale before this PR and nothing in the issue's
acceptance criteria mentions it; it is purely a plan/spec deviation. Low
severity because the array's real constraint (`MAX_MENU_ITEMS = 24`, left alone
by design per spec A6) is unaffected — this is a comment, not a correctness
issue.

### MINOR 2 — No test added or asserted to be impossible in the PR body itself

The spec and plan both argue at length (and two prior CLEAR reviews accepted)
that `test/` compiles nothing from `src/activities/`, so there is no
host-testable surface for a reader-menu deletion. Confirmed independently: the
plan review's grep (`test/` has zero references to `EpubReaderMenuActivity`,
`QrUtils`, or `QrDisplay`) is accurate as of this diff, and the host suite's
543 tests are unrelated pre-existing coverage, not new tests exercising this
change. The PR body doesn't restate this argument itself (it defers to the
linked spec), which is a minor completeness gap in the hand-back for a reviewer
who reads only the PR — but the argument is present, correct, and was already
adversarially reviewed, so this doesn't rise above MINOR.

---

## Acceptance-criteria and scope check

| Issue AC | Status | Evidence |
| --- | --- | --- |
| 1. Row, enumerator, handler removed | Met | `EpubReaderMenuActivity.cpp:77` push_back gone; `EpubReaderMenuActivity.h:26` enumerator gone; `EpubReaderActivity.cpp` case block and include gone. `grep -rn "DISPLAY_QR\|QrDisplayActivity" src lib --include='*.cpp' --include='*.h'` → 0 hits (verified). |
| 2. `QrDisplayActivity.{h,cpp}` **and** `QrUtils.{h,cpp}` deleted | Half — `QrDisplayActivity` deleted, `QrUtils` correctly kept | `QrUtils::drawQrCode` has 3 live callers in `CrossPointWebServerActivity.cpp:445,463,483` (verified), which the issue explicitly asked the implementer to check rather than trust, and the issue's own claim ("nothing else uses it") is wrong. Declining to delete `QrUtils` is the correct call, not scope reduction. |
| 3. `ricmoo/QRCode` removed from `platformio.ini:152` | Not met, correctly | Same reason — the dependency is still linked for the web server. Verified `platformio.ini:152` unchanged and the build still succeeds. |
| 4. `pio run` succeeds, flash reported | Met | Verified above, exact figures match. |

Both misses are the issue's own criteria turning out to be factually
unachievable (AC-3) or achievable only by defeating the point (AC-2's `QrUtils`
half — relocating `drawQrCode` into the web server file would satisfy the
letter while putting deletion-motivated churn into a subsystem both the spec
and issue leave alone). The PR is transparent about this rather than silently
declaring victory, and the underlying reasoning (three live non-reader callers)
checks out against the actual source. This is the correct outcome of the
issue's own instruction to verify rather than trust its claim, not a shortfall
in effort.

**Constraints honored:**
- Translation YAMLs untouched — confirmed, no `lib/I18n/translations/*` in the diff.
- Auto page turn untouched — confirmed, `AUTO_PAGE_TURN` present and unmodified in both files.
- Orientation rows (sibling task's territory) untouched — confirmed, `ROTATE_SCREEN` present and unmodified.
- `platformio.ini` not committed to a false removal — confirmed unchanged.
- No gitignored files staged — `git status --short` clean, nothing generated committed.

**No scope expansion found.** The diff's non-code files
(`USER_GUIDE.md`, and the process docs under `docs/superpowers/`) are the
project's own documented spec/plan/review workflow artifacts plus a one-line
user-guide bullet directly describing the removed feature — not unrelated
changes. `git diff main...HEAD --stat` shows exactly the six code/doc files the
plan enumerated, nothing more.

**Renumbering safety** (removing a middle `enum class MenuAction` value):
independently confirmed via `ActivityResult.h` — `MenuResult::action` is a
plain `int` set and consumed within the same binary
(`EpubReaderMenuActivity.cpp:145`, cast back at `EpubReaderActivity.cpp:291`),
nothing persists a `MenuAction` to SD, so shifting the values of `GO_HOME`,
`DELETE_CACHE`, `HIGHLIGHT_PASSAGE`, `HIGHLIGHTS` down by one is inert. This
was also independently reasoned through in the plan review; I re-derived it
rather than taking it on faith and it holds.

---

## Verdict rationale

Both AC misses are disclosed, correctly reasoned, and independently verified
against the source (the issue's own premise was wrong on both). The one plan
divergence (dropped comment fix) is explained in the PR body, low severity, and
doesn't touch anything the issue asked for. No test gap beyond what two prior
adversarial reviews already accepted as structurally unavoidable. All numeric
claims in the PR body were independently reproduced exactly. No files outside
the announced scope were touched, and every explicit constraint (translations,
Auto page turn, orientation rows) was honored.

VERDICT: CLEAR
