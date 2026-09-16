# Code-quality review — PR #42, pass 0

**Target**: `gh pr diff 42` (`fix/27-atomic-store-saves` @ `3e353335`)
**Scope**: code quality only — mirrors existing patterns, naming/structure, dead
code, comment hygiene, error-handling shape, test design, no duplication. Intent
and scope are settled by `issue-27-pr-review-intent-0.md` (CLEAR) and are not
re-litigated here.

## What I checked

- Full diff (`docs/` entries are the spec/plan/research/review trail already
  reviewed at earlier stages; the code diff is `lib/Serialization/PersistableStore.h`
  plus 24 files under `src/`).
- `git log main..HEAD` on `lib/Serialization/{SaveBudget.h,PersistableStore.h,.cpp}`
  to confirm which parts of the atomic/budget machinery pre-date this branch
  versus were added by it.
- `grep -rn 'saveToFile()' src lib` — one hit, `PersistableStore.h:127`, the
  definition itself. `grep -c saveToFileAtomic --include='*.cpp' src` → 54.
  Matches the plan's arithmetic (54 converted, 1 deleted at `CardBooks.cpp`,
  plus the base-class call) exactly.
- `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix -g` — clean, no diff.
- Where each `SAVE_BUDGET` constant sits relative to its class's access
  specifiers (all four land in `public:`, as the plan requires and explains).
- Read `PersistableStore.cpp`'s `writeDocToFileAtomic` to verify the claim added
  to `CrossPointSettings.h:354` ("four storageMutex acquisitions, not two") —
  mkdir, writeFile, remove, rename: four `Storage.*` calls, confirmed correct.
- Cross-referenced the `SAVE_BUDGET` naming and the `static_assert`-at-namespace-
  scope idiom against existing repo precedent (`fontIds.h:18-28`,
  `OtaBootSwitch.h:25`, `SdCardFont.cpp:14-17`) and against the sibling
  free-function stores' `SAVE_BYTE_BUDGET` shape (`HighlightFile.h:42`,
  `PassageDoc.h:26`) to check this isn't a second way of doing the same thing.

## Findings

### No BLOCKERs, no MAJORs

The diff is unusually disciplined for its size. Specifics:

- **No second pattern introduced.** `saveBudget()`'s `requires { T::SAVE_BUDGET; }`
  branch and `persist::fitsBudget` already existed on `main`
  (`git log main..HEAD -- lib/Serialization/` shows only two commits touching
  `PersistableStore.h`, both comment-only). This PR is the first branch to
  exercise that existing mechanism, not a new one invented alongside it. The
  `SAVE_BUDGET` name was already fixed by that pre-existing `requires` clause,
  so the four stores had exactly one spelling available to them — there was no
  naming choice to get wrong.
- **`static_assert` at namespace scope with a shared message string** for three
  of the four budgets matches an established idiom in this repo
  (`fontIds.h`'s eleven identical `"Font ID collision with sentinel"` asserts,
  `SdCardFont.cpp`'s per-struct-size asserts). Reusing one message across
  `CrossPointState`/`CrossPointSettings`/`WifiCredentialStore` and writing a
  distinct one for `RecentBooksStore` (which pins a decision, not a mechanical
  check) is the right call, not copy-paste laziness.
- **Error-handling shape matches the file it's in.** The three new
  `LOG_ERR("RBS", ...)` sites in `RecentBooksStore.cpp` copy the tag, phrasing
  register ("Failed to persist X"), and placement (immediately after the guarded
  call) of the pre-existing `removeByPath` log at the same file — the plan named
  that as the pattern to copy, and the copy is faithful down to argument order.
  Everywhere else, the diff leaves `saveToFileAtomic()`'s return value discarded,
  which matches the pre-existing convention at those call sites (fire-and-forget
  settings/state saves) rather than inventing per-site handling the codebase
  doesn't otherwise do.
- **`CardBooks.cpp:56-59`** replaces a conditional double-save with a single
  call plus a comment explaining why the second save was removed rather than
  converted. This is a deletion, not dead code — the removed line doesn't
  linger commented out anywhere in the diff.
- **No comment restates the next line.** Every new/edited comment in this diff
  carries a "why" that isn't visible from the code alone: the four `SAVE_BUDGET`
  comments cite the field layout and a worst-case byte count; the
  `CrossPointWebServer.cpp:1416-1419` comment explains why the 400 message was
  widened (one boolean now covers three distinct failure causes);
  `CardBooks.cpp` explains why the second save would be actively harmful, not
  merely redundant.
- **Tests**: no host test is added, and none could be — confirmed independently
  by reading `test/highlight_file/CMakeLists.txt:1-4`, which already documents
  that anything instantiating a `PersistableStore` subclass pulls `Arduino.h`
  transitively and isn't host-buildable. `test/save_budget/` exists and already
  covers `persist::fitsBudget` as pure arithmetic; this PR correctly doesn't
  try to bolt a fake test onto ground it can't cover, and says so in the PR
  body instead of claiming coverage it doesn't have.

### MINOR — `PersistableStore.h:124` narrates history rather than describing the merged state

```cpp
// Legacy path: non-atomic, unbudgeted, and as of #27 it has no store callers
// at all. Kept only so a future store cannot reach for it without reading this
// comment. Use saveToFileAtomic().
bool saveToFile() const {
```

CLAUDE.md's comment discipline: "write them for the merged state, as if the code
had always worked this way" and "remove before/after narration... that belongs
in the commit message." "As of #27 it has no store callers at all" is exactly
that shape — it tells a future reader what changed in this PR, not what's true
about the code as it now stands (which is simply: *zero store callers, kept only
as a documented trap*). There's a loose precedent elsewhere in the tree for a
PR-number citation (`ChapterHtmlSlimParser.cpp:28`, `// This number comes from
PR #73`), but that one anchors an otherwise-unexplainable magic number to its
origin; this one is narrating a state transition on a comment that reads fine
without the transition at all. Trivial to fix (drop "as of #27" and "at all"),
doesn't affect any decision, and I'm not blocking on it — noting it because
CLAUDE.md is explicit about this exact pattern.

No other findings. The mechanical conversions (54 call sites) are truly
mechanical — every one is `saveToFile()` → `saveToFileAtomic()` with no
neighboring reformatting, consistent with the PR body's own claim, which I
spot-checked across a dozen sites in the diff and found accurate.

## Verdict rationale

One MINOR, cosmetic, doesn't reverse any decision or touch a call a human needs
to make. Everything else — pattern reuse, naming, error-handling shape, comment
hygiene, and the (correct) absence of a fabricated test — matches how this
codebase already does these things.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
