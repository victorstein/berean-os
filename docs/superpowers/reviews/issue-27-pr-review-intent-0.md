# Intent review — PR #42, pass 0

**Target**: `gh pr diff 42` (`fix/27-atomic-store-saves` @ `b19c29d4`)
**Against**: issue #27, `docs/superpowers/specs/2026-09-16-issue-27-design.md`,
`docs/superpowers/plans/2026-09-16-issue-27-plan.md`
**Scope**: intent only — does the PR do what was asked, completely, and nothing
else. Code quality is out of scope for this pass.

## What I checked

Read the issue, spec, plan and both prior CLEAR reviews (spec-review-0,
plan-review-0). Read the full code diff (`lib/Serialization/PersistableStore.h`
plus 29 call-site files). Verified against the live tree, not just the diff:

- `grep -rn 'saveToFile()' src lib` → exactly one hit, `PersistableStore.h:127`,
  the definition itself. No store caller remains.
- `grep -rn 'saveToFileAtomic' --include='*.cpp' src | wc -l` → 54, matching the
  PR's own count.
- `SAVE_BUDGET` placement in all four stores: `CrossPointState.h:34` (public
  section starting `:13`), `CrossPointSettings.h:389` (public, no later
  access-specifier), `WifiCredentialStore.h:48` (public, after `friend` at
  `:41`), `RecentBooksStore.h:35` (public, after `friend` at `:26`) — all
  reachable from `PersistableStore<T>::saveBudget()` independent of the
  `friend` declarations, as the spec requires.
- `RecentBooksStore.cpp:62-63,76-77,89-90,106-107` — four `LOG_ERR("RBS", …)`
  sites, one per mutating operation (`addBook`, `updateBook`, the pre-existing
  one in `removeByPath`, `updatePath`), each naming what was lost rather than
  reusing the base class's generic refusal log.
- `src/network/CrossPointWebServer.cpp:1421` — `"Cannot add network (limit
  reached)"` widened to `"Cannot add network"`, with a comment explaining why.
- `src/util/CardBooks.cpp:59-62` — the redundant `RECENT_BOOKS.saveToFile()`
  after `removeByPath` is deleted, not converted, exactly as the spec directs.
- Ran `~/.platformio/penv/bin/pio run` myself on the clean checked-out branch:
  **SUCCESS**, RAM 19.5% (64,052 B), Flash 81.2% (5,320,374 / 6,553,600 B) —
  reproduces the PR body's own build-success line exactly.
- `gh issue view 40` — the A5 truncation follow-up exists, filed by the human
  (`victorstein`), references #27 and the exact truncation mechanism the spec's
  A5 describes. The PR body could not file it itself (no remote-write
  clearance) and handed back the exact `gh issue create` command instead,
  per the plan's step 12 fallback instruction — condition met.
- Confirmed no changes to `src/util/BookmarkFile.cpp` or
  `lib/I18n/translations/*.yaml` (issue constraints), no changes to the bodies
  of `saveToFileAtomic`/`writeDocToFileAtomic`, and `saveToFile()` is not
  deleted.
- Confirmed the two MAJOR findings from `issue-27-plan-review-0.md` (the BSD
  `sed \b` no-op, and the A5 follow-up being demoted to a sentence) do not
  survive in the executed work: all 54 sites are genuinely converted (not a
  `\b`-pattern no-op), and issue #40 exists.

## Acceptance criteria (issue #27 body)

1. All four stores persist via `saveToFileAtomic()` — met, verified above.
2. Each declares `SAVE_BUDGET` or documents why the default applies — met.
   `CrossPointState` 2048, `CrossPointSettings` 4096, `WifiCredentialStore`
   8192, all with a comment stating the derivation; `RecentBooksStore` keeps
   `persist::DEFAULT_SAVE_BUDGET` with a comment stating the concrete worst
   case accepted (10 entries × 4 unbounded strings) — this is the literal
   escape hatch the criterion itself offers, and the spec review accepted it
   on two conditions, both satisfied (comment + filed issue).
3. `RecentBooksStore.cpp:62,74,102` (pre-conversion line numbers) no longer
   discard the return value — met; all three now `LOG_ERR` on refusal, plus
   the pre-existing one in `removeByPath` is untouched.
4. `pio run` succeeds — met, reproduced independently.

No acceptance criterion is silently narrowed or reinterpreted away.

## Scope check

Nothing outside the issue/spec was touched. The web-server 400-message change
and the four stale-comment corrections are both explicitly called for by the
spec (§Error handling, §Data and control flow) and stayed inside files the
change already converts. No unrelated refactor, no drive-by rename, no
touched file outside the census the plan mapped up front.

## Tests

The spec's position — no host test can reach this code because
`PersistableStore.h:3` includes `<Arduino.h>` unconditionally
(`test/highlight_file/CMakeLists.txt:1-4` already documents the same wall) —
was reviewed and accepted at spec-review-0 and is not re-litigated here. The
four `static_assert`s are the compile-time test the spec substitutes, and I
confirmed they are genuinely load-bearing: `RecentBooksStore`'s asserts
`== persist::DEFAULT_SAVE_BUDGET` (pinning the A5 decision), and the other
three assert the exact numeric budgets declared in the headers, so a
misspelled or mis-scoped `SAVE_BUDGET` would fail the build rather than
silently reverting to 45,000.

## Findings

### MINOR — the PR description's own flash numbers don't agree with each other

The verification section states two different final firmware sizes for the
same build:

> `~/.platformio/penv/bin/pio run` — **SUCCESS**, 81.2% flash (5,320,374 /
> 6,553,600).
>
> **Flash delta: +1,520 bytes** (5,319,360 → 5,320,880) against a predicted
> ~3,824 …

`5,319,360 + 1,520 = 5,320,880`, which is not the `5,320,374` reported two
paragraphs earlier as the actual build's size (a ~506-byte discrepancy). I
independently reproduced `5,320,374` by running `pio run` on the checked-out
branch, so that figure — and the "well under the 6,144 stop-threshold"
conclusion — holds regardless of which pair of numbers is used; this doesn't
change any decision or the pass/fail of the flash check. It's a copy/paste or
arithmetic slip in the hand-back narrative, not in the code, and worth a
one-line correction before merge so the recorded baseline is trustworthy for
whoever reads this PR later.

No other findings. No BLOCKERs, no other MAJORs.

## Verdict rationale

Every acceptance criterion is met literally, not by reinterpretation. Both
spec-review and plan-review conditions (A5's two-part acceptance, A6a's named
call sites, the plan's `sed` bug) are correctly reflected in the shipped code.
No constraint was violated (BookmarkFile, i18n yaml, `saveToFileAtomic`/
`writeDocToFileAtomic` bodies, deleting `saveToFile()`). No scope creep. The
one finding is a documentation-only arithmetic inconsistency in the PR body
that doesn't affect any judgment call.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
