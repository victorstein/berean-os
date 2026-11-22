Tier: standard

# berean-o-25 plan review 0

Plan: `docs/superpowers/plans/2026-11-22-berean-o-25-plan.md`
Spec: `docs/superpowers/specs/2026-11-22-berean-o-25-design.md`

## Summary

The plan covers the spec. Every public constant, struct, status and pass in the spec's
"Public surface" and "Data and control flow" is in the verbatim header and source (plan
`:257-490`). All four departures are applied: "about 65 KB" and the `Salmo 51` sentence (`:576-581`),
the whole-suite command (`:513-520`), the 63-byte href boundary (`:177-194`) and the parse-OOM case
(`:205-210`). The reader matches its model line for line where the spec says it should
(`src/study/PreachingPassages.cpp:57-95` vs plan `:366-404`). The only real difference is the
version check, which splits `TooNew` out as A3 requires (`:394-402`). The `FILES:` lines (`:25-27`)
cover every file the plan touches. `build/berean-o-25-pio.log` (`:610`) is gitignored
(`.gitignore:13`). No step pushes, opens a PR or runs `hpipe submit`.

Two findings. Neither is a BLOCKER.

## MAJOR

### M1. The text-OOM test cannot see whether `load()` resets `out` after a failed `assign`

**Claim.** `ATextThatCannotBeAllocatedIsOutOfMemory` is meant to prove the spec row "A verse text
cannot be allocated → `OutOfMemory`, `out` empty, earlier verses released" (spec `:154`, `:264`).

**Problem.** In the `noText` case the first verse of card 0 fails, so no verse has been pushed yet.
The test checks only `handout.verses.empty()` (plan `:203`), and that holds whether or not the
`out = Handout{}` on the OOM path (plan `:478`) exists. When the failure happens, `load()` has
already set `out.title = "Stress"` (`:461`) and pushed one heading (`:467`). If an implementer drops
or misplaces that reset, `OutOfMemory` comes back with a partial handout and all 9 tests still pass.
The test also starts from an empty `handout`, so it never shows a stale `out` being cleared. This
is the spec's "out holds nothing unless the result is Ok" contract (spec `:27-28`), and only this
test exercises it on the late-failure path.

**Evidence.** `PassageText::assign` calls the allocator for every non-empty text, with no inline
buffer (`lib/StudyStore/StudyStore/PassageText.h:54-66`). So `failAllocation` fails on verse 0
("Made-up verse one."), after the title and heading are written. The parse-OOM half checks
`headings` (plan `:210`), but `load()` sets `out = Handout{}` on entry (`:438`) and returns before
it writes anything, so that check proves nothing about the OOM reset either.

**Fix (inline, plan `:196-211`).** Fill `handout` with a successful load first, so the test proves a
non-empty `out` is cleared. Then assert every field after each failure:

```cpp
TEST_F(PreachingHandoutsIo, ATextThatCannotBeAllocatedIsOutOfMemory) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);

  study::PassageDoc::Allocators noText;
  noText.text = {failAllocation, releaseNothing};
  EXPECT_EQ(PreachingHandouts::load(0, noText, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());

  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ExhaustedJsonAllocator exhausted;
  study::PassageDoc::Allocators noJson;
  noJson.json = &exhausted;
  EXPECT_EQ(PreachingHandouts::load(0, noJson, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());
}
```

The test count stays 9. To prove the test now bites, add a second mutation to Task 2 Step 4:
delete `out = Handout{};` at plan `:478`, rebuild, and expect this test to fail.

## MINOR

### m1. The mutation step's clean-up check can never fail

**Claim.** Task 2 Step 4 ends with "`git diff src/study` must show no trace of the mutation" (plan
`:510-511`).

**Problem.** `src/study/PreachingHandouts.cpp` is untracked at that point. It is first staged in
Step 6 (`:525`). `git diff` ignores untracked files, so the check prints nothing whether or not the
mutation was reverted. A forgotten `<` would survive into the commit, and Step 3 would catch it
only if the implementer re-ran it.

**Evidence.** Plan `:321` creates the file, `:507-511` mutates it and runs the check, and `:525`
is the first `git add`.

**Fix (inline, plan `:510-511`).** Replace the sentence with: "`grep -c 'bytes <= MAX_HREF_BYTES'
src/study/PreachingHandouts.cpp` must print `1`."

VERDICT: CLEAR
