Tier: heavy

# Issue #239 — plan review 0

- **Plan:** `docs/superpowers/plans/2026-09-30-issue-239-plan.md`
- **Spec:** `docs/superpowers/specs/2026-09-30-issue-239-design.md`
- **Tree:** `ebf4e262` (no source change since the plan's base `7066c0df`; `git diff 7066c0df --stat -- src` is empty).

## What was checked

- **Quoted "Find" blocks.** Every one was compared against the source. All eleven match the file text verbatim:
  `EpubReaderActivity.h:52-55` (pageShown), `:146-148` (openReaderMenu comment), `:154-156` (openHighlights
  declaration), `:166-169` (CancelTo); `EpubReaderActivity.cpp:293-306` (openReaderMenu tail and buildTickHeapGate),
  `:402-430` (openHighlights), `:746-749`, `:791-795`, `:805-808`, `:832-835`, `:853-860`, `:929`, `:946`, `:1027-1029`,
  `:1448-1453`. The plan's line numbers are within one or two of these, and it tells the implementer to match on text.
- **Every spec requirement has a step.**
  - A-4 flag → Step 1.
  - A-1, A-5, A-11 helper and log lines → Step 2, identical to the spec's `reopenReaderMenu` body plus a "why" comment.
  - A-2, the six sites → Step 3. `grep -n "openReaderMenu(false)"` today lists exactly `:747, :793, :807, :834, :929, :946`.
  - A-3 `CancelTo` on `openHighlights` → Step 4, all three callers. No other file calls `openHighlights`; a grep of
    `src/` finds it only in `EpubReaderActivity.{h,cpp}`.
  - Both entries in "Comments that this change makes wrong" → Step 2 (`.h:146-147`) and Step 4 (`.cpp:403-411`).
  - Build and format → Step 5. The device list → Step 7, a one-to-one copy of spec items 1-7.
- **Locking claims.** The render task takes `RenderLock` before `currentActivity->render(std::move(lock))`
  (`ActivityManager.cpp:52-59`), and `ReaderActivity::render(RenderLock&&)` holds it through `renderBook`
  (`ReaderActivity.cpp:166-188`). The write at Step 1 is therefore under the lock, as the new header comment says. The
  result handler runs after `lock.unlock()` (`ActivityManager.cpp:125-126`), so none of the asserts in
  `requestUpdateAndWait` (`:313-320`) fire.
- **Types and signatures stay consistent across steps.** Step 1 names `pageRendered`, Steps 2 and 6 use it, and
  `SETTINGS.screenInverted` exists (`CrossPointSettings.h:259`). Step 4 moves the declaration below `enum class
  CancelTo` (`.h:167`), in the same access section, and the definition's parameter order matches it. The call
  `openHighlights(CancelTo::Menu, static_cast<uint16_t>(...))` converts implicitly to `std::optional<uint16_t>`.
- **Each commit builds.** Step 1 adds a member nobody reads yet, and Step 2 a private function nobody calls yet. GCC
  raises no `-Wall` warning for either, so both commits build clean.
- **The grep gates give the numbers they promise.**
  - Step 3: 7 → 1 for `openReaderMenu(false)`, and 6 for `reopenReaderMenu();`, since no comment contains either string.
  - Step 4: the definition plus 3 calls.
  - Step 6 `pageRendered =`: two hits.
- **FILES line.** `FILES:` sits at column 0 outside any fence (`plan:13`) and lists both touched files as repo-relative
  paths. No step touches any other file.
- **Other callers.** `progressChangeResultHandler` has one user (`.cpp:894`, BOOKMARKS), so Step 3a changes no other path.

No BLOCKER or MAJOR finding. The plan is literal, anchored on verified text, and reaches the spec. The findings below
are small and can be fixed inline.

## MINOR

### MINOR 1 — `$PIO` is defined once and assumed in every later block

- **Claim:** `"$PIO" run -e x4pro` works in every step (`plan:31-32`, used at `:43, :106, :212, :354, :512, :535`).
- **Problem:** `PIO=` is set in a single illustrative block. Agent shells do not keep state between calls, so an
  implementer who runs Step 1's green block on its own gets `"" run ...`, i.e. `run: command not found`. It reads like
  a build failure that has nothing to do with the change.
- **Evidence:** `plan:28-33`. The binary exists at `/Volumes/stein/.platformio/penv/bin/pio`.
- **Fix:** Write each build command as `"$HOME/.platformio/penv/bin/pio" run -e x4pro`, or add "re-export `PIO` in
  every shell" to the note at `:28`.

### MINOR 2 — Step 2's green expectation miscounts the header hits

- **Claim:** The green check expects "the declaration in the header, the definition in the `.cpp`" (`plan:215`).
- **Problem:** After Steps 1 and 2 the header contains `reopenReaderMenu` three times:
  - Step 1's `pageRendered` comment ("Cleared by reopenReaderMenu", `plan:77`);
  - the rewritten `openReaderMenu` comment ("through reopenReaderMenu", `plan:142`);
  - the declaration (`plan:146`).

  An implementer who reads the check literally sees three hits where the plan promised one, and may stop.
- **Evidence:** `plan:76-78`, `plan:141-146`.
- **Fix:** Change the expectation to "three hits in the header (two comments and the declaration), one definition in
  the `.cpp`".

### MINOR 3 — Step 6 diffs against a moving `origin/main`

- **Claim:** `git diff origin/main --stat -- src/activities/reader/EpubReaderMenuActivity.*` should be empty, and so
  should the allocation grep (`plan:559-560`).
- **Problem:** A two-dot diff compares trees. If `origin/main` has moved since `7066c0df`, as the plan itself
  anticipates at `:7-9`, a sibling's change to `EpubReaderMenuActivity` or a sibling's `makeUniqueNoThrow` shows up
  here as a false failure.
- **Evidence:** `plan:7-9`, `plan:559-560`.
- **Fix:** Use the merge base: `git diff origin/main...HEAD` in both rows.

### MINOR 4 — Step 6 ends with a bare `git push`

- **Claim:** "Then push: `git push`" (`plan:562-566`).
- **Problem:** CLAUDE.md "Git workflow → Rules" 2 forbids pushing without explicit user approval. The branch already
  tracks `origin/fix/239-sheet-keeps-page`, so the pipeline evidently owns the push. A step that pushes on its own
  either duplicates that or oversteps it.
- **Evidence:** `plan:562-566`; `git rev-parse --abbrev-ref @{u}` → `origin/fix/239-sheet-keeps-page`.
- **Fix:** Drop the push from the plan, or replace it with "hand back to the pipeline for push and PR".

VERDICT: CLEAR
