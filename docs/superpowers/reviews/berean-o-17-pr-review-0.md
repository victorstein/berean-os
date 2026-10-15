Tier: standard

# berean-o-17 (t4) — Show mode: PR review 0

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-show-mode` (head `97df18b9`).
Read against the t4 brief (`hpipe brief --task t4`), the spec
`docs/superpowers/specs/2026-10-15-berean-o-17-design.md` and the plan
`docs/superpowers/plans/2026-10-15-berean-o-17-plan.md`. The branch log has no empty reply commits.

Checked by running:
- `cmake --build build/test --target PreachingCardsTest && ./build/test/ui_layout/PreachingCardsTest`: 48 tests, all pass (the existing card layout and nav tests plus the new `PreachingShow*` ones).
- `.pio/show-build.log` (the `pio run -e x4pro` after `186b1ecc`, the last functional commit; `97df18b9` only reformats): SUCCESS. It compiles `PreachingShowActivity.cpp`, `PreachingShowLayout.cpp` and `PreachingTopicActivity.cpp` (lines 546-548), and its only warning is the existing one in the third-party `WebSockets` library (line 420).

## Intent

**Acceptance criteria, one by one.**

- *A quote tap opens show mode at that card, pushed with `startActivityForResult`, and the topic screen selects the returned card.* Met. `openShow` checks the range, builds the screen with `makeUniqueNoThrow`, shows a toast on OOM, clears the tap flash and pushes it (`PreachingTopicActivity.cpp:175-187`). `onShowFinished` takes `RenderLock` and calls `preaching::selectVerse`, which turns to the card's page (`:189-196`). The manager releases the lock before it calls the handler (`ActivityManager.cpp:125-127`), so taking the lock again does not deadlock.
- *No chrome; the quote centred with the reference bold below it; the footer "N / M · title"; steps 18, 14, 12; pages at 12; nothing cut.* Met.
  - `render` draws only the page and the footer (`PreachingShowActivity.cpp:142-147`).
  - The page is centred using `pageHeight` (`:158`). The reference is drawn bold through `styleOf` (`:33-35`, `:169-170`).
  - The footer formats `STR_PREACHING_SHOW_POSITION` with `topic->header` (`:177-178`), and uses `truncatedText` when the result is too wide (`:183-188`).
  - The font ladder is a `constexpr` table, `{NOTOSERIF_18, 14, 12}` (`:24`). `layOut` accepts the first size at which nothing splits a word and everything fits one page, and it always accepts 12 (`PreachingShowLayout.cpp:67-82`). `paginate` never drops a line (`:40-55`).
  - The `NothingIsCut` test rebuilds every non-break byte of the quote and the reference from the spans.
- *Left/Right and the outer thirds step; the centre tap and the left-edge swipe go back.* Met. Input is read in spec A10's order: Back (which includes the edge swipe), then `isTouchMenuTap`, then `detectTouchPageTurn`, then `ButtonNavigator` (`PreachingShowActivity.cpp:103-121`). A step to nowhere draws nothing (`:125`).
- *It holds no text copy; a verse change refreshes like a reader page turn.* Met.
  - `Deck` is a pointer and a count over `topic->cards`, and `title` is `topic->header.c_str()`. Both stay valid while the topic screen is on the stack, because its `loop()` does not run there, and only its `loop()` can swap the topic out.
  - Each frame goes through `ReaderUtils::displayWithRefreshCycle`, the same call as `EpubReaderActivity.cpp:1613`. `pagesUntilFullRefresh` starts at 0, so the first frame is a half refresh (spec A14).
- *Host tests in an existing suite; builds clean; clang-format clean.* Met. `PreachingShowLayoutTest.cpp` joins `PreachingCardsTest` (`test/ui_layout/CMakeLists.txt:148-154`); the build is above; `97df18b9` is the formatting pass.

**Spec requirements.** Every assumption, A1-A18, is implemented, including the parts that are harder than they look:
- the reference is never split across pages, and never starts one alone (A5, `paginate`'s `keepTail`);
- a reference word that would split also forces a smaller size (A4, as changed after plan review);
- the `· p/n` suffix appears in the footer only when the quote pages (A9, `:179-182`);
- every cursor write happens under `RenderLock` (A17, `:69-73`, `:126-131`);
- `layOut` clears and refills the layout instead of reassigning it, so the capacity reserved in `onEnter` survives a verse change. `ReusesTheCallersCapacity` tests this.

The one divergence from spec is an unreachable edge case (m2 below).

**Scope.** No silent reductions. Nothing outside `PreachingShow*`, `PreachingTopicActivity.*`, `test/ui_layout/` and the task's own docs. No new strings or fonts. `ActivityResult.h`, `ReaderUtils.h` and `PreachingCardLayout.*` are untouched.

**Divergence from the plan.** None. The activity source matches the plan's Task 5 code once whitespace is ignored (`diff -w`: one reflowed `if`), `paginate` is the plan's Task 4 code verbatim, and the topic-screen change is Task 6 verbatim.

**Tests.** They test behaviour, not the implementation:
- concrete fixtures with fake widths for each size, asserting which size is chosen, where pages break, page heights, and that every byte comes through;
- the orphan rule and the unsplit wrapped reference, each built so that greedy filling would break it;
- a box shorter than one line still advancing one line per page (so the loop terminates);
- the stepping edge cases at both ends.

## Quality

- **Patterns.**
  - The pure module mirrors `PreachingCardLayout`/`PreachingCardNav`: no Arduino code, widths through a function-pointer `MeasureFn`, and spans into the source text. It reuses `wrapText` through a small context adapter rather than copying it.
  - The font ladder follows the `HomeVerse::FONT_IDS` / `StudySleepScreen` shape.
  - The push for a result follows `HighlightsActivity::openLinks`.
  - The result handler follows the topic screen's own `openNextQuestion`: `RenderLock`, set the cursor, `halfRefreshPending`.
- **Naming and structure** are consistent with the sibling files.
- **Dead code and comments.** No dead or commented-out code. The comments carry a reason: lifetimes (`PreachingShowActivity.h:12`, `PreachingShowLayout.h:19`), capacity reuse (`PreachingShowLayout.h:54-55`), ghosting on return (`PreachingTopicActivity.cpp:190`), and why `isBreak` is a local copy.
- **Error handling** has the established shape: `LOG_ERR` with a toast on OOM, a silent return for a stale ordinal, and a `LOG_ERR` with an early finish for an empty deck.
- **Resources.**
  - The three vectors are reserved once in `onEnter`, so a step in the usual case does no heap operation.
  - Locals are `char[128]` and `char[96]`, in separate functions.
  - There is no `std::function` beyond the existing handler type.
  - Nothing is written to SD.
- **Duplication.** One verbatim copy (m1). `isBreak` is also a copy, but the spec sanctions it and the original is file-local in a file outside this task's scope.

## Findings

No BLOCKER or MAJOR.

- **MINOR m1: the line-copy loop is duplicated verbatim, comment included.** `PreachingShowActivity.cpp:41-49` (`copyLine`) repeats the body of `PreachingTopicActivity::drawLine` at `PreachingTopicActivity.cpp:308-315`: clamp to `LINE_BUFFER_BYTES - 1`, map bytes below 0x20 to spaces, NUL-terminate. Both copies carry the same comment.
  - Spec step 5 asks for the topic screen's behaviour, not its code, and the natural shared home (`PreachingCardLayout.h`, next to `wrapText`, whose break bytes it mirrors) is outside t4's scope.
  - Suggested action: leave it here and file a discovery to hoist one helper. Otherwise the two copies can drift, for example if the break set ever gains U+2028.
- **MINOR m2: the empty-deck path returns ordinal 0, not the start ordinal.** The spec's error table says that a deck with no Verse card "finishes at once with the start ordinal". `onEnter` calls `finishWithResult()` before `cursor` is set (`PreachingShowActivity.cpp:60-63`), so the result is the default `cursor.verse = 0` (`:136-137`).
  - It has no effect: `openShow` never pushes onto an empty deck (`PreachingTopicActivity.cpp:176`), and `selectVerse` would fall back to the first Verse card anyway.
  - Suggested action: align the spec row or the code if either is touched again. It is not worth a commit on its own.

VERDICT: CLEAR
