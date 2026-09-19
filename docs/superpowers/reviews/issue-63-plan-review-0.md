# Adversarial review — `2026-09-19-issue-63-plan.md`, pass 0

Plan: `docs/superpowers/plans/2026-09-19-issue-63-plan.md`
Spec: `docs/superpowers/specs/2026-09-19-issue-63-design.md`
Spec review (CLEAR, six MINORs applied): `docs/superpowers/reviews/issue-63-spec-review-0.md`
Research: `docs/superpowers/research/2026-09-19-issue-63-research.md`

Reviewed in the worktree `fix-63-honour-read-status` at `3704dd57`, tree clean.

---

## What I did, rather than read

This plan presents literal code blocks and literal find-text. The only honest way
to review that is to apply it, so I did — every edit in steps 1 through 7, by
exact-string replacement with a uniqueness assertion on each find, then the gates
in step 8. Everything below is measured. The tree and the `build/test` baseline
were restored afterwards (`git status --short` empty, `ctest` back to
`100% tests passed out of 593`).

| Claim in the plan | Result |
|---|---|
| Baseline is 593 | ✅ `100% tests passed out of 593` |
| `DocReadStatusTest.cpp` is 27 lines ending with `OnlyMissingIsSafeToOverwrite` | ✅ exactly |
| `DocReadStatus.h` is 22 lines ending with `classifyDocRead`'s `}` | ✅ exactly |
| Step 1's red is `use of undeclared identifier 'mayOverwriteAfterRead'` | ✅ five instances, that exact wording |
| Step 2 turns it green at 598 | ✅ `100% tests passed out of 598` |
| Every step 3–7 find-text matches the current file **uniquely** | ✅ all ten replacements, count == 1 |
| `grep -n "readLedger"` → exactly three hits | ✅ `:61`, `:183`, `:198` |
| `grep -n "appendLedger"` → exactly three hits | ✅ `:86`, `:247`, `:387` |
| A-10: no `src/` file includes `DocReadStatus.h` directly | ✅ grep over `src/` and `lib/` outside `lib/Serialization/` returns nothing |
| Step 8b `pio run` → SUCCESS | ✅ `SUCCESS`, RAM 19.5%, Flash 81.1%, **zero warnings** on both touched TUs |
| Step 8c `pio check -e x4pro --fail-on-defect low/medium/high` | ✅ `No defects found`, `x4pro` named, exit 0 |
| Step 8d `clang-format-fix` leaves the tree clean | ❌ one reflow — see MINOR 1 |

I also built the **step-4 intermediate state** on its own (steps 2+3+4, without
5–7), because that is the one commit in the plan that changes a signature and
both callers at once with no build between. It compiles clean, no warnings. The
plan's "every other step ends in its own commit and leaves the tree committable"
holds for the case where it could plausibly have failed.

**Spec coverage.** Every row of the spec's §Files-touched table maps to a step
(`DocReadStatus.h` → 2, `PubKeyRegistry.cpp` → 3, `MigrationRunner.cpp` → 4/5/6,
`MigrationRunner.h` → 7, `DocReadStatusTest.cpp` → 1). Every goal maps: Goal 1 →
steps 3 and 5; Goal 2 → steps 4 and 6; Goal 3 → step 5's `measureJson` gate;
Goal 4 → `LEDGER_FORMAT_VERSION` in 4b, read-guard in 4c, write-guard in 5;
Goal 5 → step 1 plus the verified fact that no shared file is touched
(`test/CMakeLists.txt:62` already wires `doc_read_status`). All twelve
assumptions A-1…A-12 are executed as written. Types and signatures stay
consistent across steps: `readLedger`'s new return type is introduced in 4c and
both callers are converted in the same step; `ledger` keeps its
`std::vector<std::string>` type at 4e so steps 5 and 6 need no further change.

The ArduinoJson usage is correct against the pinned v7.4.2 (`platformio.ini`
`@ 7.4.2`, `test/CMakeLists.txt:31` `GIT_TAG v7.4.2`): `doc["v"] | 0`,
`doc["done"].is<JsonArray>()`, `.to<JsonArray>()`, `.add<JsonObject>()` and
`measureJson(doc)` all compile and are the same idioms `writeReport`
(`MigrationRunner.cpp:100-134`) and `PubKeyRegistry.cpp:31-36` already use.
The anonymous-namespace scoping is right: `LEDGER_FORMAT_VERSION` lands at
`MigrationRunner.cpp:31`, inside the `namespace {` that opens at `:25` and
closes at `:142`, alongside `MODULE`, and both `readLedger` and `appendLedger`
stay inside it. The `std::optional` handling is right — `return
std::vector<std::string>{}` and `return done` both convert, `*ledger` on the
`const auto` in `pending()` yields a `const&` that binds to `ledgerContains`,
and `std::move(*ledgerRead)` at 4e move-constructs rather than copies.

---

## MAJOR 1 — steps 8e and 9 push and open a PR with no approval gate, against CLAUDE.md and against all three sibling plans

**Claim.** Step 8e is `git push`, step 9 is `gh pr create ...`, and "Done when"
closes with "The branch is pushed and the PR is open against `main`". Nothing in
the plan conditions either on the user.

**Problem.** CLAUDE.md's Git rules, item 2: *"Never push to any remote, or open
or close a PR, without explicit user approval. Complete local work and any
requested local commit, then stop."* The brief for a plan review is whether an
implementer with no other context can execute it literally. Executing this one
literally performs both forbidden actions. The spec does not ask for them either
— its §"Build and format gates" stops at `pio run`, `pio check`,
`clang-format-fix` and `git diff --exit-code`, and its §Shared-file report is
explicitly "for the PR description", not an instruction to open one.

**Evidence.** Every other plan in this directory that reaches a push carries the
gate, in so many words:

- `docs/superpowers/plans/2026-09-17-issue-51-plan.md:847-848` — "Ask before
  pushing or opening the PR — CLAUDE.md, 'Never push to any remote, or open or
  close a PR, without explicit user approval.'" That plan has **no** `git push`
  command at all.
- `docs/superpowers/plans/2026-09-17-issue-58-plan.md:1344` — "**Do not push or
  open the PR without explicit user approval** (CLAUDE.md, Git …)", beside its
  `git push` at `:1234`.
- `docs/superpowers/plans/2026-09-17-issue-60-plan.md:491-492` — "**Push and open
  the PR.** Ask the user before pushing or opening — `CLAUDE.md` requires
  explicit approval for both", immediately above its `git push -u` at `:497`.

`docs/superpowers/plans/2026-09-19-issue-63-plan.md:714-718` and `:722-733` carry
neither sentence, and `:848` promotes the result to a completion criterion.

**Concrete fix.** Insert, as the first line of step 8e and again above the `gh pr
create` block in step 9:

> **Stop and ask the user before running this.** CLAUDE.md, Git rules: never
> push to any remote, or open a PR, without explicit user approval.

and soften the last "Done when" bullet to "…and, **once the user approves**, the
branch is pushed and the PR is open against `main` with `Closes #63` in the
body." The commands themselves are fine and the PR body is accurate — every
`file:line` in it that I checked (`test/CMakeLists.txt:62`, `main.cpp:502-503`,
`PubKeyRegistry.cpp:23,36-39`, `MigrationRunner.cpp:74`,
`PersistableStore.cpp:89-101`, `DocReadStatusTest.cpp:21-27`) resolves
correctly. Only the gate is missing.

---

## MINOR 1 — step 1's literal test block is not clang-format-clean, so step 8d always produces a diff

**Claim.** Step 8d: "`git diff --exit-code` must print nothing and exit 0."

**Problem.** It will not, on a tree where only this plan's edits were applied.
The last case in step 1 is formatted in a way `clang-format 21.1.8` rewrites, so
the implementer is deterministically pushed into the "if it reformatted
something, commit it" branch — an extra commit whose entire content is
reformatting code the plan itself wrote two steps earlier.

**Evidence.** Applying step 1 verbatim, then `.venv/bin/clang-format -i
test/doc_read_status/DocReadStatusTest.cpp`:

```
<         EXPECT_EQ(permitted, expected)
<             << "exists=" << exists << " empty=" << contentEmpty << " parseFailed=" << parseFailed;
---
>         EXPECT_EQ(permitted, expected) << "exists=" << exists << " empty=" << contentEmpty
>                                        << " parseFailed=" << parseFailed;
```

Every other block in the plan — steps 2, 3, 4, 5, 6, 7 — is already
clang-format-clean; I ran the formatter over all five touched files and only
this one hunk moved.

**Concrete fix.** Replace those two lines in the step 1 block with the formatted
form above. Step 8d then genuinely exits 0 and the fallback commit never fires.

---

## MINOR 2 — the step 8d fallback commit uses a type CLAUDE.md does not list

**Claim.** Step 8d's fallback: `style: clang-format the tree`.

**Problem.** CLAUDE.md, "Commit messages": *"Types: `feat`, `fix`, `refactor`,
`docs`, `test`, `chore`, `perf`."* `style` is not among them. It is harmless for
the release — PRs are squash-merged and release-please parses the PR title, not
the commits — but it is an instruction to write a commit the repo's own
convention does not sanction, in a plan that is otherwise scrupulous about
conventional commits.

**Evidence.** `CLAUDE.md`, Git workflow → Commit messages; plan `:707`.

**Concrete fix.** `chore: clang-format the tree`. Better still: apply MINOR 1 and
delete the fallback, since nothing will need reformatting.

---

## MINOR 3 — step 4's preamble miscounts its own edits

**Claim.** Plan `:256-257`: "Do all four edits before committing."

**Problem.** Step 4 has five: 4a includes, 4b the constant, 4c `readLedger`, 4d
`pending()`, 4e `runIfPending`. An implementer counting to four and committing
would leave one of them out — and the two that look most droppable to a reader
skimming for "the function and its callers" are 4a and 4b, either of which breaks
the build.

**Evidence.** Plan `:260-391` enumerates 4a through 4e.

**Concrete fix.** "Do all five edits before committing."

---

## MINOR 4 — 4e introduces the file's first `std::move` without `<utility>`

**Claim.** Step 4a adds only `<optional>` to the include block.

**Problem.** Step 4e's `auto ledger = std::move(*ledgerRead);` is the first use of
`std::move` in `src/study/MigrationRunner.cpp` — `grep -n "std::move"` on the
current file returns nothing. `std::move` is declared in `<utility>`. It compiles
here (verified: full `pio run` SUCCESS, no warnings) because libstdc++ pulls
`<bits/move.h>` in through `<memory>`/`<vector>`, but it is a transitive-include
dependency the plan did not intend to take.

**Evidence.** `src/study/MigrationRunner.cpp:12-14` today; plan `:270-275` and
`:384-391`.

**Concrete fix.** Make 4b's include block:

```cpp
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
```

`clang-format` keeps that order, so MINOR 1's gate stays clean.

---

## MINOR 5 — 4c adds a `LOG_ERR` inside `readLedger` that the spec's error table does not have, and it fires twice per boot

**Claim.** Step 4c's `readLedger` logs `"Refusing to read a newer ledger format"`
on the version arm.

**Problem.** The spec's `readLedger` is silent there — its pseudocode is a bare
`if ((doc["v"] | 0) > LEDGER_FORMAT_VERSION) return std::nullopt;` (design
`:405`), and §Error handling assigns the logging elsewhere: *"`readLedger`, ledger
Unreadable/ParseError/future `"v"` → `nullopt`. `readDocFromFileAdopting` has
already logged the parse failure (`PersistableStore.cpp:57`); `runIfPending` adds
the decision it took."* The plan's addition is defensible — the version arm is
the one `nullopt` cause that nothing else logs, since
`readDocFromFileAdopting` returns `Ok` for a well-formed future-format file — but
it is an undeclared deviation, and it has a cost the plan does not price:
`readLedger` is called from **both** `pending()` (`:183`) and `runIfPending`
(`:198`), so a future-format ledger emits `ERR MIGRATE Refusing to read a newer
ledger format` twice, followed by `ERR MIGRATE Migration ledger unreadable;
refusing to migrate over it` — three error lines for one condition, one of them
describing a readable file as unreadable.

**Evidence.** Plan `:326-329`; spec `:405` and `:518`; the two call sites
confirmed by `grep -n "readLedger"` after applying step 4.

**Concrete fix.** Keep the log (it closes a genuine gap the spec left) but say so
in the step: one sentence noting it departs from the spec's error table because
the version arm is otherwise silent, and that a future-format ledger therefore
logs from `pending()` as well. If the duplicate line is unwanted, the
alternative is to move it to `runIfPending` by having `readLedger` return the
reason — which is more machinery than the condition is worth, so the sentence is
the right fix.

---

## Things I attacked and could not break

- **The `Missing` split in 4c.** `if (status == DocReadStatus::Missing) return
  std::vector<std::string>{};` before `if (status != DocReadStatus::Ok) return
  std::nullopt;` is the order that preserves first boot. Reversed it would turn
  every fresh card into a refusal. The plan gets it right and says why.
- **A-2, "refuse before touching `doc`."** Both step 3 and step 5 place the
  `mayOverwriteAfterRead` check before any `doc[...]` read, and 4c reads
  `doc["v"]` only on the `Ok` arm. The hazard the spec cites
  (`PersistableStore.cpp:89-94`'s comment: the clear is deliberately *not*
  extended to the `ReportFailed` arm) is genuinely avoided.
- **Step 6's two guards.** Neither double-pushes `report`: the failure branch
  pushes and `break`s, the success path falls through to the single existing
  `reports.push_back(report)`. `writeReport` at `:340` still runs after the
  `break`, and `allOk = false` reaches `src/main.cpp:502-503` — both verified
  against the real `main.cpp`.
- **The budget comment's arithmetic.** 200 × 146 = 29,200 against
  `persist::DEFAULT_SAVE_BUDGET = 45000` (`lib/Serialization/SaveBudget.h:23`).
  The comment's "about 29 KB" is honest, and the plan correctly declines the
  shrink exception `src/util/BookmarkSaveAction.h` needs.
- **The new `appendLedger` refusal arm being mostly unreachable within a run**
  (because step 4e already refused the whole run on a bad ledger read) is
  defence in depth, not dead code — the ledger can be rewritten between the two
  reads, and the arm costs nothing.
- **`pio check` fails open.** The plan's `-e x4pro` plus three `--fail-on-defect`
  severities is stronger than the spec asked for and is the right response to the
  known gate-fails-open trap. It passes: `No defects found`.

---

Verdict: one MAJOR, five MINORs, no BLOCKERs. The MAJOR is a missing approval
gate, not a design or scope reversal — its fix is two sentences of plan text and
needs no decision from the human, so it belongs inline rather than back with the
author. Fix MAJOR 1 and MINORs 1–5 in place and the plan is executable as
written; I executed it end to end and it produces exactly the spec.

VERDICT: CLEAR
MAJORS: 1
