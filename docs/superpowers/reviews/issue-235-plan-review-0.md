Tier: standard

# Issue #235 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-30-issue-235-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-235-design.md`
Code read at `631719b2`. The source is the same as at `663e2011`, the commit the plan cites.

## Summary

The plan is sound. Every spec requirement maps to a step:

| Spec requirement | Plan step |
|---|---|
| A1 / Goal 5 | Tasks 1–4 |
| A2 | 4.1, plus the test `BibleReachableDefaultsToHidden` |
| A3 | 8.6 |
| A4 | 8.5 |
| A5 | 8.4 |
| A6 | Task 6 |
| A8 / A10 | 8.3 |
| A9 | Task 7 |
| A11 | 8.7 |
| Designated initialisers | 5.2 |
| `HIGHLIGHTS_SUPPORTED` hoist | 5.3–5.4 |

I checked the plan's code and line numbers against the source. They hold:

- `ReaderMenuModel.h:35` is the line after which Task 2 inserts, and `:82`, `:86` are the two gated adds.
- `ReaderMenuModelTest.cpp:12-19`, `:44-54`, `:60-61` and `:133-152` are the edit sites.
- `EpubReaderMenuActivity.h:25-28` and `.cpp:105-114` hold the constructor. `EpubReaderActivity.cpp:289` is its only call site.
- In `EpubReaderActivity.cpp`, `:271-278` is the local `hasHighlights`, `:318-351` the passage functions, `:461-469` the long-press block, `:514-518` and `:539-541` the holds, and `:789-796` the menu cases.
- `EpubReaderActivity.h:74` is `pendingSelectionAnchorY`. It sits in the class's private section, since `public:` only starts at `:188`.
- `LauncherActivity.cpp:100-137` is `resolveTargets` and `:198-218` holds the two helpers. `LauncherActivity.h:60-65` holds their declarations.
- `test/ui_layout/CMakeLists.txt:55-68` and `:85-99` register the two tests.

Types and names stay consistent across the steps: `TagTarget`, `tagTarget`, `Inputs::bibleReachable`, `BibleFinder::find(std::string_view)`, `Found{path, by}`, `HIGHLIGHTS_SUPPORTED`, `bibleForTagsPath` / `bibleForTagsResolved`, `openBibleTags` and `runTagCommand`.

Specific checks:

- **Narrowing claim (Task 4).** The plan says adding the field breaks the firmware build until Task 5. That is true: seven positional values meet eight fields, so `tagsHereCount` lands in `bool hasRotation`, and that is narrowing in a braced initialiser. The plan handles it by committing Tasks 3–5 together (5.7).
- **Designated initialiser order (5.2).** It matches the `Inputs` declaration order once 4.1 is applied.
- **Headers.** `activityManager` reaches `EpubReaderActivity.cpp` through `Activity.h:9` → `ActivityManager.h:115`.
- **Build pickup.** No host `CMakeLists.txt` globs `src/activities/launcher/`, so the new `BibleFinder.cpp` stays out of the host builds. `platformio.ini` has no `src_filter`, so the firmware compiles it.
- **Type checks.** `RecentBook::path` / `title` are `std::string` (`src/RecentBook.h:9-12`). `findBySymbol` takes `std::initializer_list<std::string_view>` (`src/study/PubKeyRegistry.h:30`). `BIBLE_SYMBOL` is a `std::string_view` (`LauncherBible.h:15`). `Epub::getPath` returns `const std::string&` (`lib/Epub/Epub.h:56`).
- **The 128-mask test (3.4).** It encodes `tagTarget` correctly. `!isBible && !bibleReachable` removes both entries, and the removal is a no-op when `hasHighlights` is false.
- **Expected rows in `NonBibleWithoutABibleHidesBothTagEntries`.** These match `build()` for `BOOK_ALL` minus `HIGHLIGHTS`.
- **File lock.** The `FILES:` lines at `:7-12` sit at column 0 outside code fences and list every file the plan edits or creates, with no globs:
  - `ReaderMenuModel.h` and its test
  - `ReaderEntryIntent.h` and its test
  - `BibleFinder.h` and `.cpp`
  - `LauncherActivity.h` and `.cpp`
  - `EpubReaderMenuActivity.h` and `.cpp`
  - `EpubReaderActivity.h` and `.cpp`

There are no BLOCKERs and no MAJORs. The findings below are MINORs to fix inline.

## MINOR

### M1: Task 7.4 wrongly says `recents` is still used, and hedges on a certain outcome

- **Claim.** Plan `:502-503`: "The local `recents` (`:102`) is still used later in `resolveTargets`; if the compiler reports it unused, delete that line."
- **Problem.** `recents` has exactly one use, at `LauncherActivity.cpp:120`, which is the `findBibleInRecents(recents)` call that 7.4 removes. After the edit it is certainly unused. `scripts/enable_repo_warnings.py` adds `-Wall` to `src/`, so the build warns. CLAUDE.md says not to commit while the build warns. An implementer reading the plan literally may instead hunt for a "later use" that does not exist.
- **Evidence.** `grep -n recents src/activities/launcher/LauncherActivity.cpp` returns only `:102`, `:120`, `:211` and `:212`. Lines `:211-212` are inside the helper being deleted.
- **Fix.** Replace the bullet with: "Delete `const auto& recents = RECENT_BOOKS.getBooks();` (`:102`); its only use was the removed `findBibleInRecents(recents)` call. Keep `RECENT_BOOKS.loadFromFile();` (`:101`)."

### M2: Task 7.4's `<algorithm>` check contains a placeholder

- **Claim.** Plan `:500-501`: "Keep `<algorithm>` if anything else in the file uses it (`grep -n "std::" … | grep -E "find|sort|min|max|count|remove"`)."
- **Problem.** The `…` is a placeholder, so the command cannot be run as written. The answer can also be settled now. With `findBibleOnCard` gone, `LauncherActivity.cpp` has no `std::find_if` / `sort` / `min` / `max` / `count` / `remove`. Only `:203` matched, and it moves out.
- **Evidence.** `grep -n "std::find_if\|std::sort\|std::min\|std::max\|std::count\|std::remove" src/activities/launcher/LauncherActivity.cpp` matches only `:203`.
- **Fix.** Pick one:
  - Write the command out in full: `grep -nE "std::(find|sort|min|max|count|remove)" src/activities/launcher/LauncherActivity.cpp`.
  - State the result: `<algorithm>` (`:11`) is unused after the move and may be removed together with the `PubKeyRegistry.h` / `CardBooks.h` includes.

  Leaving the include in is also harmless, so either way is fine as long as the step is executable.

### M3: Task 8.3 leaves the null-`epub` guard up to the implementer

- **Claim.** Plan `:582-585`: "`epub` is non-null here … If in doubt, add `if (!epub) return false;`…"
- **Problem.** "If in doubt" hands the decision to the implementer, so two literal executions can produce different code. The reasoning in the plan is right: `loop()` finishes on a null `epub` (`EpubReaderActivity.cpp:379-380`), and `openReaderMenu` / the holds run only after that point. The step should still say one thing.
- **Evidence.** `EpubReaderActivity.cpp:379-381`. `isBible()` is `epub && …` (`EpubReaderActivity.h:94`), so `isBible()` returning false does not prove `epub` is non-null.
- **Fix.** Make the guard part of the code block: `if (!epub || isBible()) return false;` as the first line of `bibleReachableForTags`. It costs one comparison and makes the dereference visibly safe. Drop the "if in doubt" sentence.

### M4: Task 5.6's warning check names the wrong number of files

- **Claim.** Plan `:317`: "no new warnings in the four touched files."
- **Problem.** Tasks 3–5 touch five files: two `ReaderMenuModel` files and three `EpubReader*Activity` files, as 5.7 itself lists. Only three of them are firmware translation units or headers that change. The count does not match either number, which is a small snag for someone executing the plan literally.
- **Evidence.** Plan `:319-320`.
- **Fix.** Change the sentence to "no new warnings from `EpubReaderMenuActivity.{h,cpp}` or `EpubReaderActivity.cpp`."

VERDICT: CLEAR
