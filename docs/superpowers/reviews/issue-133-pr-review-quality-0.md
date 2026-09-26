# PR #134 review: code quality, round 0

Scope: `gh pr diff 134`. The only code change is `test/persistable_store/PersistableStoreTest.cpp`
(+36/-1). Everything else in the diff is pipeline documentation under `docs/superpowers/`.

## Findings

None at BLOCKER, MAJOR or MINOR.

## What was checked

### It follows the existing pattern

- Both tests live in the existing `PersistableStoreGuard` fixture (`PersistableStoreTest.cpp:52-60`).
  They reuse `ProbeStore`, `PATH`, `TMP_PATH`, `NEWER` and `bytesOn` (`:17-48`). No new fixture,
  helper or store type was added.
- `AFutureVersionTempIsPromotedThenRefusedAndNeverOverwritten` (`:117-130`) follows the same steps
  as `ARefusedLoadBlocksEverySaveAndLeavesTheFileUntouched` (`:64-75`) with the same assertion
  messages: seed, refused load, `value == 0`, `value = 42`, both saves refused, and a check of the
  card. The one addition is the post-load card check at `:122-123`, which is what shows that
  promotion happened inside the load.
- `AGarbageTempIsKeptByTheLoadAndStillLiftsTheRefusal` (`:132-148`) opens with the same refused
  start as `AMissingFileLiftsTheRefusal` (`:78-82`): seed, `ASSERT_FALSE` load,
  `ASSERT_TRUE(Storage.remove(...))`, then `EXPECT_FALSE(...) << "Missing"`. `ASSERT_` is used
  for setup and `EXPECT_` for the assertions, the same split as `:79-82` and `:99-103`.
- `TORN` (`:43`) is a named constant beside `NEWER`, in the file's style. Its torn-prefix shape
  matches the fixture in the base-reader sibling `AdoptingReadTest.cpp:63` (`{"v":`).

### Naming and structure

- The test names are sentences, like the siblings at `:64`, `:77`, `:88`, `:98` and `:108`.
- The header comment (`:1-4`) was extended by one clause and still reads as merged-state prose,
  with no before/after narration.

### Comments

- The only new comment, `:133-134`, gives a reason the code does not show: without a refused
  start, a regression that reported `ParseError` would still pass. Reading the code would not
  tell you this, and the fixture's `SetUp` (`:56`) would otherwise make the test vacuous. The
  project rules allow this kind of comment.
- The assertion messages (`:121-123`, `:129`, `:141`, `:147`) describe what each check means. They
  do not repeat the expression.

### Dead code and duplication

- There is no dead code or commented-out code.
- No setup helper was extracted for the three-line refused start (`:135-137`). It also appears
  inline in `:78-80` and `:99-100`, so inline is the file's convention. Extracting it only here
  would create a second style.

### Test design

- Each test asserts return values and bytes on the card at both points (after the load and after
  the save). A failure therefore points at the step that broke.
- In test 1, `value = 42` (`:125`) means a leaked save cannot produce bytes that happen to equal
  `NEWER`.
- In test 2, the final checks (`:146-147`) pin the exact written document and confirm the torn
  `.tmp` is gone. That is the contract `TempAdoption.h` gives for leaving garbage behind.

### Verification

I deleted `build/test/persistable_store/**/*.o`, rebuilt `PersistableStoreTest` and ran it:
`[  PASSED  ] 7 tests.` Afterwards `git status --short` was clean apart from this file.

VERDICT: CLEAR
