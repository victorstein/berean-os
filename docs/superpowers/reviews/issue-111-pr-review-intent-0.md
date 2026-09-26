# PR #118 review — intent (pass 0)

Reviewed against issue #111, the spec
`docs/superpowers/specs/2026-09-26-issue-111-design.md` and the plan
`docs/superpowers/plans/2026-09-26-issue-111-plan.md`, at `dd826931`.

## Summary

The PR does what the issue asks and nothing more. `lib/Serialization/SdPaths.h`
holds both roots and every fixed path under them as `inline constexpr char[]`,
and every code use in `src/` and `lib/` now reads from it. The only exception is
`src/main.cpp:227`, which is deliberately deferred to the orchestrator (spec A8),
and the PR says so. I re-ran the sweep:

```
grep -rn 'crosspoint\|\.berean' src lib --include='*.cpp' --include='*.h'
```

Apart from `SdPaths.h`, the only string literal left is `src/main.cpp:227`. Every
other hit is a comment, a URL, or an unrelated `crosspoint_` symbol. These are
all spec non-goals.

Each site in the issue maps to a change in the diff:

| Issue site | PR |
|---|---|
| `BookCacheUtils.cpp:17,21` | `src/util/BookCacheUtils.cpp:18,22` → `sdpaths::CROSSPOINT_DIR` |
| `LauncherActivity.cpp:230` | `:231` |
| `PublicationsActivity.cpp:83` | `:84` |
| `SleepActivity.cpp:823` | `:824` |
| `EpubReaderActivity.cpp:179` | `:180` |
| `RecentBooksStore.cpp:118` | `:119` |
| `MigrationRunner.cpp:266` | `:265` |
| `ClearCacheActivity.cpp:100,120` | `:101,121` (`String(sdpaths::CROSSPOINT_DIR) + "/" + itemName`, same bytes) |
| `PersistableStore.cpp:12,23` | `:14,25` |
| `/.berean` in 5 TUs (`PubKeyRegistry`, `MigrationRunner`, `TagPaletteFile`, `MeetingWeekCache`, `CatalogIndexStore` as `STUDY_DIR`) | all five local definitions deleted, uses read `sdpaths::BEREAN_DIR` |

The PR goes further than the issue's list, and the spec asked for all of it:

- the extra bare use at `HomeActivity.cpp:63`, which the PR discloses
- the four `getFilePath()` headers
- the highlights and bookmarks dirs, both keeping their trailing `/` through `+ "/"` (spec A4)
- `LEGACY_DIR`
- the three per-publication subdirectories
- the owner aliases in `PubKeyRegistry.h`, `TagPaletteFile.h`, `MeetingWeekCache.h`, `MigrationRunner.h` and `BibleSearchStore.h` (spec A5, with names and types unchanged, so no caller changes)

Every changed string is byte-identical to before. I checked each substitution in the diff.

**Spec requirements.** A1 through A8 are all implemented as written. The header
at `lib/Serialization/SdPaths.h:1-57` matches the spec's code block exactly,
including one `static_assert` per constant and the three search paths also
checked against `SEARCH_DIR`. `catalog-<lang>.idx` is still composed at runtime
(A7, `src/network/CatalogIndexStore.cpp:51-53`). Comments and host-test literals
are untouched (non-goals). `docs/file-formats.md` needs no change, because no
format moved.

**Plan fidelity.**

- The commit sequence matches plan Tasks 2 to 10 one to one (`764c0b4a` through `dd826931`).
- The PR description covers what Task 11 requires, in the required order: what changed, the shared-file lines verbatim, the CI gap, and verification.
- In `CatalogIndexStore.cpp`, the include was placed inside the `// clang-format off` block, as Task 7 instructs.
- `git diff origin/main...HEAD --stat -- test/CMakeLists.txt src/main.cpp` is empty, as the plan requires.

There is no divergence the PR leaves unexplained.

**Tests.** `test/sd_paths/SdPathsTest.cpp` pins every constant against a literal
written out by hand, not one derived from the header. That makes the test the
source of truth for what is on the card, which is the behaviour this refactor
must preserve. The `isUnder` cases test the predicate's real edges:

- the root itself (`:55`)
- an empty tail (`:57`)
- a sibling that shares a prefix (`:60-62`)
- another root (`:64`)

The tests do not just restate the implementation.

## Findings

### MINOR 1: The issue is only complete once the orchestrator applies two shared-file lines

Two changes the issue needs are not on this branch:

- **`src/main.cpp:227`** still has `"/.crosspoint/sleep_frame.bin"`. The issue says "with every use switched to it".
- **`test/CMakeLists.txt`** does not register `sd_paths`. Until it does, CI's `unit-tests` job never builds or runs `SdPathsTest`, so the pinning test protects nothing in CI.

This is by design: spec A8 and the plan's "Not edited on this branch" note keep
both files off the branch. The PR body gives the exact lines and says
"**That line must land before merge.**" So this is disclosed and planned, not a
silent reduction in scope.

It is still a merge gate. `Closes #111` should not take effect until both lines
are in. The spec's fallback (the #94 precedent, `5c385845`) is to commit the
`add_subdirectory` line in the branch if the pipeline cannot guarantee it lands.

VERDICT: CLEAR
