Tier: heavy

# btrial-5 spec review 0

- **Under review:** `docs/superpowers/specs/2026-10-12-btrial-5-design.md`, at `8cf4a110`.
- **Checked against:**
  - the brief (`hpipe brief --task t5`);
  - the research note `docs/superpowers/research/2026-10-12-btrial-5-research.md`;
  - the batch spec's §3, §4, §6, A1, A3 and A4.
- **Method:** every claim below was checked against the code in this worktree, the FreeInkUI sources under `freeink-sdk/`, or a command run here.

## What holds

The design is sound. Each brief bullet maps to a sketched member. Each labelled assumption I attacked held, except the two numbers noted below. The checks behind that:

- **Hooks and `final`.**
  - The base seals `handleHomeGesture`, `render`, `listCount`, `buildScreen`, `drawFooter`, `onBackButton`, `onRowAction`, `onRowLongPress`, `handleCustomInput`, `handleButtons` and `navigateButtons` (`TagChipGridActivity.h:33-34`, `:58-71`).
  - `onEnter`, `headerTitle` and `drawChrome` stay open (`UiListActivity.h:21`, `:65-66`).
  - So the removal list at spec `:242-248` is forced, not chosen.
- **The commit-order drift is real, and the fix restores `main`.**
  - On this branch `activeTags()` walks `activeIdsByName()` (`src/study/StudyStore.cpp:160-164`).
  - On `main` it walks `activeIds()` (`git show main:src/study/StudyStore.cpp`, `:159-163`).
  - `normaliseTags` keeps order (`lib/StudyStore/StudyStore/PassageDoc.cpp:17-27`, `:195-198`), so the commit order is the stored order.
  - `selectedInOrder(palette().activeIds(), …)` reproduces `main`.
- **A1, the D8 equivalence.**
  - `isActive` and `activeIds` agree (`TagPalette.cpp:67-72`, `:89-96`).
  - `fromJson` drops id 0 and duplicate ids (`:173-181`).
- **Threads.**
  - The render task holds `RenderLock` across `render()` (`ActivityManager.cpp:52-59`).
  - `renderUi` renders with no input, so no handler ever dispatches on the render task (`UiAppHost.cpp:17-27`, `FreeInkApp.h:678-707`).
  - The keyboard's result handler and `onEnter` both run unlocked (`ActivityManager.cpp:126-127`, `:159-160`).
  - `drawChrome` runs before `renderUi` in one `render()` (`TagChipGridActivity.cpp:269-271`), so the title buffer and `doneRect_` are written and read on the render task only.
  - No render-task path and no network path reads the palette: `grep -rln "STUDY\.\|palette()" src/network src/components` is empty. A14 matches the base's unlocked retire (`TagChipGridActivity.cpp:195-202`).
- **Flash.**
  - `clearTapFlash` suppresses the arm after the current dispatch (`FreeInkApp.h:755-759`, `:733-742`).
  - Outside a dispatch it is reset by the next `render` or `route` (`:688`, `:734`).
  - `TagChipView::draw` copies each chip's own look into every state slot (`TagChipView.cpp:104-109`).
- **Done.**
  - A long-press release looks for `InputLongPress`, with the touch fallback off (`FreeInkUICore.h:1171-1176`, `:1246-1249`).
  - An open popup consumes every touch (`OptionPopup.h:72-109`), and the base asks it first (`TagChipGridActivity.cpp:205`).
  - `app.on` appends or replaces within `FreeInkApp<96, 6>` (`FreeInkApp.h:630-643`), and `resetUi` does not clear handlers (`UiAppHost.cpp:12-15`).
- **Geometry.** `HeaderLayout.h:98-107` and `HeaderLayoutTest.cpp:247-270` give the action rect `{y 9, h 36}` under Lyra and `{y 25, h 25}` under Classic. `bandHitPadding` therefore returns 4/4 and 20/0, as A6 says.
- **Removals.**
  - `STR_TAG_NEW`'s only code use is `TagPickerActivity.cpp:100`, and its only key is `english.yaml:373`.
  - `gen_i18n.py` skips its own outputs (`:249`, `:277`).
  - `ctest --test-dir build/test -N` reports 1745 tests, 7 of them `TagRows.*`, as the baseline says.
- **Callers.** Both callers carry their own includes for `StudyStore`, `OptionPopup` and `PassageDoc` (`HighlightsActivity.h:3-16`, `PassageSelectActivity.h:8-17`, `PassageSelectActivity.cpp:21`). Dropping those includes from the picker header breaks neither.

## Findings

### MAJOR 1 — Device check 7 cannot verify the commit order it exists for

- **Claim.**
  - Spec `:808-811`: "Take a passage with two or more tags whose alphabetical order differs from the order they were created. Note its sleep-screen tag pill. Open the picker, press Done with no change, and the pill is unchanged."
  - The Goal (`:70-71`) ties stored order to that pill.
- **Problem.** On a correct build the check is either not runnable or reports a regression that does not exist.
  1. **The tester cannot choose the passage the sleep screen shows.**
     - The sleep screen reservoir-samples every passage that fits, at random (`src/activities/boot_sleep/StudySleepPick.h:103-106`, `:117-121`).
     - So "note its pill" depends on luck.
  2. **A migrated passage need not be stored in allocation order.**
     - Migration pushes ids in the legacy highlight's tag order, and allocates them first-seen across the whole run (`src/study/MigrationRunner.cpp:307-309`, `lib/StudyStore/StudyStore/MigrationPlanner.cpp:43-45`).
     - Example: highlight 1 tagged `[hope]` allocates hope = 1. Highlight 2 tagged `[faith, hope]` allocates faith = 2 and stores `[2, 1]`. `normaliseTags` keeps that order (`PassageDoc.cpp:17-27`).
     - Done with no change rewrites it to `[1, 2]`, and the pill turns from faith to hope.
     - `main` does exactly the same, so this is not a regression. The check would report one anyway.
- **Why it matters.** This is the only device check of the commit-order acceptance.
  - The activity is not host-testable (spec `:753-759`).
  - `selectedInOrder`'s host test proves the helper, not the call in `commitAndFinish`.
- **Fix (inline).** Replace check 7 with one read off the card:
  1. Use **+ New tag** to create "Zz order", then "Aa order". Zz gets the lower id. Apply both to one passage and press Done.
  2. Read both ids from `/.berean/tags.json` (`lib/Serialization/SdPaths.h:23`).
  3. In `/.berean/passages/<pubKey>.json` (`src/study/PassageFile.cpp:16`), that passage's `"t"` must list Zz's id first. That is allocation order; name order would put Aa first.
  4. Reopen the picker from Highlights and press Done with no change. `"t"` must not change.
  5. Add one sentence for the tester: the first Done on a passage migrated from the old highlights can re-sort it into allocation order, as on `main`.

### MINOR 1 — The heap check counts a create as a leak

- **Claim.** Spec `:818-819`: free heap is "flat across a picker enter/exit cycle, and across a keyboard round trip".
- **Problem.** A round trip that creates a tag grows the palette for good:
  - `entries_.push_back` and `rebuildDisplayOrder` (`TagPalette.cpp:55-57`);
  - `byName_.resize` (`:116`);
  - a heap buffer for any name over 15 bytes.
  A tester who types a new name sees free heap drop, and reports a leak that is not one.
- **Fix.** "…flat across a keyboard round trip cancelled with Back, and across one that names a tag already in the palette." The dedupe path returns before it adds or saves anything (`StudyStore.cpp:256`).

### MINOR 2 — The removal grep matches a generated header until the next firmware build

- **Claim.**
  - A20 (`:709-710`): "re-runs `grep -rnw STR_TAG_NEW src lib test` after the rewrite, and it must print nothing".
  - `:771-772` gives the same check.
- **Problem.**
  - `lib/I18n/I18nKeys.h` is generated and gitignored (`.gitignore:8`). Only the PlatformIO pre-step rewrites it (`platformio.ini:134`).
  - Run straight after the rewrite, as A20 says, the grep prints that file's line.
  - An implementer who treats the grep as the gate may then hand-edit a generated file, which `CLAUDE.md` forbids.
- **Evidence.** The spec's `:771` grep, run now, prints `lib/I18n/I18nKeys.h:490:  STR_TAG_NEW,` among its hits.
- **Fix.** Search tracked files only: `git grep -nw -e STR_TAG_NEW -e TagRowMapping -e 'TagRows::' -- src lib test`. Or state that the grep runs after `pio run`.

### MINOR 3 — Comments this change makes false

- **Claim.**
  - Non-goals (`:88-91`) file only `activeTags()` as follow-up, and leave `TagPalette.h:19-23` alone.
  - Spec `:389-390` keeps `reportAddTagFailure` "unchanged".
- **Problem.** After this change these comments describe a picker that no longer exists:
  - **`HighlightsActivity.h:72-75`.** It says the picker overrides `handleHomeGesture()` "because the screen has no OTHER way to leave carrying a result". After this change it does not override it (the base's is `final`), and Done does leave with a result.
  - **`HighlightsActivity.h:30`.** It says the picker "offers "New tag..."", under `HighlightDoc::MAX_TAGS_PER_HIGHLIGHT`.
  - **`TagPickerActivity.cpp:184`.** Spec `:389-390` keeps this comment unchanged. It reads "Mirrors HighlightDoc::addTag's own precedence", and `HighlightDoc` is gone.
- **Fix.**
  - In this change, reword the `reportAddTagFailure` comment, which is in the picker's own file: "Mirrors TagPalette::add's precedence: name checks before the full palette (`TagPalette.cpp:44`, `:52`)."
  - The brief keeps both callers unchanged. File the `HighlightsActivity.h` comments and `TagPalette.h:19-23` in the same discovery as `activeTags()`.

### MINOR 4 — A6's hit height leaves out the SDK's edge snap

- **Claim.** A6 (`:612-616`), citing `button.h:36-43`: "That is 44 px tall under Lyra (4 + 36 + 4) and 45 px under Classic (20 + 25 + 0)."
- **Problem.**
  - `button.h:41` passes the padded rect through `ensureMinTouchRect`.
  - That function snaps any edge within 12 px of the screen edge out to it (`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:818`, `:840-843`).
  - The band starts at `topPadding` = 5 (`HeaderLayoutTest.cpp:50`), so Done's hit starts at y = 0. It is 49 px tall under Lyra and 50 px under Classic.
  - The behaviour still covers what the brief asks for, and no chip comes near. Only the numbers and the mechanism are wrong.
- **Fix.**
  - Rewrite A6's numbers: "…grown to the band, then snapped to the screen top by `ensureMinTouchRect`: 49 px under Lyra, 50 under Classic."
  - Device check 5 may add that a tap just above the band also commits.

### MINOR 5 — The memory "Net" counts what is added but not what is freed

- **Claim.** Spec `:555-556`: "about 2.4 KB more internal SRAM than the `tags_` it replaces".
- **Problem.**
  - 2.4 KB is the gross cost of `chips_` plus `widths_`: 2,156 + 196 B.
  - The `tags_` it replaces is held for the whole visit. That is 48 `TagView`s (`StudyStore.h:45-48`): an id plus a 24 B `std::string`, about 28 B each, so about 1.3 KB of internal SRAM. Each name over 15 B adds its own heap buffer.
  - The spec lists `tags_` under "Removed" (`:551-552`) but does not subtract it. The net cost is about 1 KB.
  - The conclusion, within batch §6, still holds.
- **Fix.** "About 2.4 KB gross; about 1 KB net of the `tags_` it frees."

## Verdict

No BLOCKER. MAJOR 1 and the five MINORs are wording fixes in the spec. Make them inline; none needs another review pass.

VERDICT: CLEAR
