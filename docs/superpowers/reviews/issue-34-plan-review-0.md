# Plan review pass 0 — issue #34, `ReturnStack` capacity

Plan: `docs/superpowers/plans/2026-09-19-issue-34-plan.md`
Spec: `docs/superpowers/specs/2026-09-19-issue-34-design.md`
Prior: `docs/superpowers/reviews/issue-34-spec-review-0.md` (CLEAR, applied)
Reviewed in: `/Volumes/stein/.herdr/worktrees/berean-os/feature-34-return-stack-capacity` (clean at start and at finish)

## What I executed rather than read

I ran the plan's code blocks in a scratch build (`/private/tmp/claude-501/.../scratchpad/btest`),
including a temporary apply-and-revert of both pasted files into the worktree so the repo's own
formatter and CMake build saw them. The tree was restored with `git checkout --` and is clean.

| Plan step | Result |
|---|---|
| Step 1 | `[==========] 12 tests from 1 test suite ran.` / `[  PASSED  ] 12 tests.` — matches the plan's "expect exactly" byte for byte |
| Step 2 | 3 failures, exactly the three names listed, `tail -6` shows all three |
| Step 3 paste | compiles under the CMake target's real `-Wall -Wextra -pedantic`; 13 tests, all pass at `CAPACITY = 3`; `grep -c '^TEST(ReturnStack,'` → 13 |
| Step 3/5 pastes vs `./bin/clang-format-fix -g` | **zero** diff — both pastes are already format-clean |
| Step 4 sweep | `CAPACITY=3/8/16/32 -> [  PASSED  ] 13 tests.` — matches the plan's expected block exactly |
| Step 5 paste vs current header | `diff -u` shows **exactly** the four claimed changes; `push`/`pop`/`unpush`/`clear`/`count`/`oldest`/private members byte-identical |
| Step 5 `static_assert` on Xtensa | `xtensa-esp32s3-elf-g++ -std=c++2a` compiles the `CAPACITY = 16` header clean; `sizeof(ReturnStack) == 136` also holds |
| Step 6 find-text | matches `.claude/agents/ui-dev.md:43-44` verbatim |
| Step 7 placement | `EpubReaderActivity.h` is included at `EpubReaderActivity.cpp:1`, so `EpubReaderActivity` is a complete type at line 23 — `sizeof` there is legal, `#include <utility>` is line 22 and `"../../util/BookmarkFile.h"` is line 24, so "line 23" is the right seam |
| Step 8 `ctest ... -j` (no value) | accepted by ctest 4.4.2 (≥ 3.29 semantics) |
| Step 8 full suite | `cmake --build "$BUILD" -j8` then ctest → **594/594, 100% passed** with the plan's files in place |
| Step 8 whole-tree formatter | `./bin/clang-format-fix` on the clean tree exits 0 and leaves `git status --short` empty, so "must be clean" is achievable |
| Step 10 heredoc | `--body "$(cat <<'BODY' … BODY)"` with backticks, fences and unbalanced `)` round-trips intact under both bash and zsh |
| Citations | `MappedInputManager.cpp:308-309`, `EpubReaderActivity.cpp:547-551`, `:160-162`, `ReaderUtils.h:255-257`, `CrossPointSettings.h:276`, `test/CMakeLists.txt:77`, design-doc `:214`, `:511`, `:658`, `ROADMAP.md:73,111` — all read, all say what the plan says |

Spec-requirement coverage: Goal 1 → Step 5; Goal 2 → Step 5's byte-for-byte diff constraint; Goal 3 →
Step 3; Goal 4 → Step 5's `static_assert`; the three comment fixes in the spec's *Comments* table
(`ReturnStack.h:5-7`, `ReturnStackTest.cpp:5-7`, `ui-dev.md:44`) → Steps 5, 3, 6; **A6** → Step 7;
firmware gates → Step 8; the five human-verification items → the PR body's "Not verified here",
including the `TOUCH_READER_SWIPE` qualification from MINOR 3. Nothing in the spec is unmapped. The
"7 untouched" claim is true: `diff -u` old→new shows no change inside `StartsEmpty`,
`PopOnEmptyFailsAndLeavesTheOutputAlone`, `PopsInLifoOrder`, `OldestFollowsThePopsBackDown`,
`ClearEmptiesAPartialRing`, `UnpushUndoesAPush`, `UnpushOnEmptyLeavesTheRingUsable`.

I did **not** run `pio run` or `pio check` — a cold build in this worktree is 10–20 minutes and no
finding below depends on one. I closed the only compile risk in the header (the Xtensa
`static_assert`) directly with the toolchain instead.

---

## MAJOR 1 — the Definition of Done's re-run of the Step 4 sweep silently tests `CAPACITY = 16` four times

**Claim.** Plan `:810`: "13 host tests pass at `CAPACITY` 3, 8, 16 and 32 (Step 4 sweep re-run after
Step 5)".

**Problem.** Step 4's shim is produced by a `sed` anchored to the literal `3`
(plan `:365`): `sed "s/CAPACITY = 3/CAPACITY = $cap/" src/activities/reader/ReturnStack.h`. After
Step 5 the header reads `static constexpr int CAPACITY = 16;`, which does not contain the string
`CAPACITY = 3`. The substitution silently does nothing, the shim keeps `CAPACITY = 16` on all four
iterations, and the loop prints four green lines that look exactly like the valid Step 4 output. The
gate fails open — and it fails open on the *one* claim the whole change rests on.

**Evidence.** Simulating the post-Step-5 header and re-running the Step 4 loop verbatim:

```
requested 3  -> shim actually has: CAPACITY = 16
requested 8  -> shim actually has: CAPACITY = 16
requested 16 -> shim actually has: CAPACITY = 16
requested 32 -> shim actually has: CAPACITY = 16
```

The pre-Step-5 run of Step 4 is correct (I reproduced it: four genuine passes at 3/8/16/32), so the
evidence pasted into the PR body is sound. It is only the DoD's *re-run* — the one that would prove
the **committed** header and the **committed** tests are capacity-agnostic together — that is
meaningless as written. Step 2's command has the same anchor, so anyone re-deriving the red after
Step 5 hits it too.

**Fix.** Make the substitution capacity-blind and make a failed patch loud. Replace the `sed` in both
Step 2 and Step 4 with (verified on this macOS `sed`):

```bash
sed -E "s/CAPACITY = [0-9]+;/CAPACITY = $cap;/" src/activities/reader/ReturnStack.h \
  > "$SCRATCH/shim/activities/reader/ReturnStack.h"
grep -q "CAPACITY = $cap;" "$SCRATCH/shim/activities/reader/ReturnStack.h" \
  || { echo "shim patch failed for cap=$cap"; exit 1; }
```

and change the DoD line to name it as a post-Step-5 re-run of the *fixed* loop.

---

## MAJOR 2 — the rewrite of the two `clear()` tests does not restore coverage, because `clear()`'s `top_ = 0` is unobservable through the public API

**Claim.** Plan `:52-55`: "at `CAPACITY = 16` they silently stop **testing** a wrapped ring … they
degrade into duplicates of the partial-ring test while staying green. A failure count cannot see
that." Step 3's commit message (`:343-346`) and the PR body (`:690-693`) carry the same framing, and
the spec attributes to `ClearEmptiesAWrappedRing` the property "`clear()` resets `top_` as well as
`count_`, so a **wrapped** ring is empty and not half-rotated" (spec `:563`), which it says "loses
**all** coverage" at 16 (spec `:426`).

**Problem.** That property has no coverage at 16 **and none at 3** — not before the rewrite and not
after it. Every observable of `ReturnStack` after `clear()` is a function of `count_` alone:
`count()` returns `count_`, `oldest()` short-circuits on `count_ == 0`, `pop` short-circuits on
`count_ == 0`, and once `count_` is 0 the next pushes are rotation-invariant
(`oldest()` = `slots_[(top_ + k - k) % CAPACITY]` = `slots_[top_]` = the first push). So `top_ = 0` in
`clear()` (`ReturnStack.h:41`) is semantically dead through the public surface, and no host test can
pin it. The rewrite is still correct hygiene — the setup genuinely wraps, which matters the moment
anyone adds an assertion or changes `pop` — but it restores nothing, and the plan's commit message
and PR body say it does.

**Evidence.** Mutant: `clear()` with the `top_ = 0` line deleted, header otherwise unchanged. Both
the current test file and the plan's rewritten one, at both capacities:

```
mutantA(clear forgets top_) cap=3  NEW suite -> [  PASSED  ] 13 tests.
mutantA(clear forgets top_) cap=16 NEW suite -> [  PASSED  ] 13 tests.
OLD suite, *Clear*:*AfterAClear* filter        -> [  PASSED  ] 3 tests.
NEW suite, *Clear*:*AfterAClear* filter        -> [  PASSED  ] 3 tests.
```

For contrast, the rewrite *does* kill a real wrap mutant — `oldest()` reduced to `&slots_[0]` —
at both capacities, which is the evidence **A4**'s anti-tautology argument actually needed and which
the plan does not currently collect:

```
NEW suite vs oldest()=slots_[0], cap=3  ->  3 FAILED TESTS
NEW suite vs oldest()=slots_[0], cap=16 ->  3 FAILED TESTS
```

**Fix.** Two edits, no code change to the pasted files:

1. In the plan's TDD note (`:52-55`), Step 3's commit body (`:343-346`) and the PR body bullet
   (`:690-693`), stop claiming recovered coverage. Say what is true: these two tests were always
   duplicates of the partial-ring case with respect to anything the public API can observe, because
   `clear()`'s `top_` reset is unobservable; they are re-parameterised so their *setup* still
   matches their name and so the file has no surviving literal bound, not because coverage was lost
   at 16. One sentence, e.g. *"`top_ = 0` in `clear()` is not observable through `count()`, `pop()`
   or `oldest()`, so neither form pins it; the rewrite keeps the name honest and removes the last
   literal, it does not add an assertion."*
2. Add the mutation evidence above to Step 4 (or the PR body's *Verification*) in place of the
   unsupported claim — it is two `sed`-patched headers and two compiles, and it is the only thing
   that demonstrates the rewritten bounds still catch a wrap bug at 16.

---

## Minor findings (4), all inline

1. **Steps 9 and 10 push and open a PR unconditionally.** `CLAUDE.md` → *Git workflow* → *Rules* 2:
   "Never push to any remote, or open or close a PR, without explicit user approval. Complete local
   work and any requested local commit, then stop." Plan `:637-641` and `:645-773` do both with no
   approval gate. The model plan does the same (`docs/superpowers/plans/2026-09-17-issue-40-plan.md`
   is the only other plan of 21 containing `gh pr create`), so this is pipeline convention rather
   than an oversight — but the plan should say the push and PR are on explicit approval, not present
   them as steps to run.
2. **Step 7's removal instruction is a comment, not a command.** Plan `:578-581` says
   `# delete the static_assert line` between two `git diff` invocations, in a block whose other lines
   are executable. The step's own end state ("`EpubReaderActivity.cpp` must be unmodified") has an
   exact command: `git checkout -- src/activities/reader/EpubReaderActivity.cpp`. Use it; a hand
   edit is the one way to leave a stray blank line and fail Step 8's clean-tree check.
3. **Step 2's stated purpose is inverted.** Plan `:81-83`: "so that the rewrite in Step 3 can be
   shown *not* to have followed the constant." Step 2 demonstrates the opposite — that the *existing*
   file did not follow the constant, which is what Step 3 fixes. Reword to "…so the rewrite in Step 3
   can be shown to follow the constant."
4. **No time budget on `pio check`.** Step 8 (`:599`) runs it right after a `pio run`, framed like
   the seconds-long commands around it. `check_flags` is `--enable=all` over `src/` and `lib/`
   (`platformio.ini:24`), which is minutes, and `check_tool = cppcheck` sits in `[base]`
   (`platformio.ini:20`) so it runs per environment. Say so, next to the Step 7 build budget.

---

## Things I checked that are sound, and deliberately am not raising

- **The unusual red state.** Step 3 commits a green→green refactor with no preceding red, and two of
  the five rewrites have no red at any capacity. The plan does not hide this — `:42-55` states it,
  and the spec settled it as MINOR 1 (spec `:540-546`). Steps 3, 5 and 6 each leave a green,
  committable tree (verified: 13/13 after Step 3 at `CAPACITY = 3`, 13/13 after Step 5 at 16,
  594/594 for the full suite).
- **`PopsInLifoOrder` keeping literal `3`.** The spec's absolute rule ("*every* loop bound and push
  count must be `CAPACITY`-relative", spec `:414-416`) reads as violated, but the spec's own Testing
  table lists `PopsInLifoOrder` among the seven untouched (spec `:566`), and the plan's defence
  (`:324-326`) is correct — it asserts LIFO on three pushes, true at any `CAPACITY >= 3`. Same for
  the literal 2- and 3-push setups in the other untouched tests.
- **`SurvivesRepeatedWrapping`'s guard.** `static_assert(PUSHES > ReturnStack::CAPACITY * 2)` trips
  at `CAPACITY >= 50`, which is what the plan claims at `:319`. It compiles at 3/8/16/32.
- **`EXPECT_EQ(stack.count(), ReturnStack::CAPACITY)` ODR-use.** Safe: `static constexpr` members are
  implicitly inline since C++17 and the build is `-std=c++2a`. Linked clean in the CMake target.
- **Placeholders.** There are none. Every step has a runnable command, an exact paste, or an exact
  find-and-replace, and both pastes are already clang-format-clean so no step hides a reformat.
- **Scope discipline.** `test/CMakeLists.txt`, `lib/I18n/translations/*.yaml`, `src/main.cpp`,
  `MappedInputManager`, `ROADMAP.md` and `2026-09-13-berean-os-design.md:214,658` are all named as
  not-to-touch (`:534-537`, `:802-804`), matching the spec.

Both MAJORs are wording and one `sed` expression; neither reverses a decision, changes scope, or
needs a call only the human can make. The four minors are inline edits. An implementer with no other
context can execute this plan literally and land the spec — with MAJOR 1 fixed, they will also be
able to *prove* it.

VERDICT: CLEAR
MAJORS: 2
