Tier: standard

# Issue #194 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-29-issue-194-design.md`. Checked against issue #194
(`gh issue view 194 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-29-issue-194-research.md`, and the code on
`feature/194-compact-metrics-no-progress` (base `a8461e94`).

## Summary

The design holds up. I re-checked the load-bearing mechanisms and they behave as the spec says:

- The shared-line header geometry (`BaseTheme.cpp:352-359, 381-385`). The component centres the
  title in the full band (`header.h:136-144`). The battery glyph is centred in its rect
  (`battery-indicator.h:77-78`). Once `batteryBarHeight == headerHeight`, both sit on the same
  centre line.
- The `rightLabel` route in D2. The component right-aligns the label inside
  `content.width - rightReserve`, so it lands left of the battery, and it bottom-aligns the label to
  the title (`header.h:145-157`).
- Every `drawHeader` call passes `metrics.headerHeight` as the rect height (24 call sites from
  `grep -rn "drawHeader(" src`). The D2 guard `batteryBarHeight < rect.height` therefore picks
  exactly the Lyra and Classic cases the spec intends.
- `props.rowHeight` really does fall back to the token when it is 0 (`FreeInkApp.h:283-284`). A-8
  is required, not optional.
- The A-10 arithmetic (small font 24 × 2 = 48 ≤ 50). `list()` insets rows only horizontally
  (`list.h:491`, `Insets{0, sidePad, 0, sidePad}`), and its label-only gate is
  `labelLh * maxLines > rowH` (`list.h:393-395`).
- The rows-per-page figures (703/50 and 743/50 both give 14; `listVisibleRows`,
  `FreeInkUI.cpp:853-859`).
- The `tabPillFullSlot` precedent (`BaseTheme.h:66`; no metrics table names it).
- The A-9 audit of callers without the flag. Of the files listed, only `PublicationsActivity`
  sets `subtitle`.

A-12's partial degradation to "Isaiah" is well-founded. The HTML cache that
`WhenMissing::Fail` reads persists once any build has read the chapter
(`SpineHtmlStream.cpp:28-29`, `Section.cpp:258-328`), so the book-name-only case is the rare one.
Device check 3 covers it.

There is one MAJOR and it can be fixed inline: a verification gate that can never pass. The
MINORs are small consistency and completeness fixes. None of them reverses a decision.

## Findings

### MAJOR 1: the grep gate matches `drawProgressBar`, so "returns nothing" can never hold

- **Claim.** Under "Build and quality gates":
  `grep -rn "applyChaptersReadSubtitle\|drawProgress\|STR_BIBLE_CHAPTERS_READ\|…" src lib` returns
  nothing.
- **Problem.** `drawProgress` is an unanchored substring. It also matches the theme's
  `drawProgressBar`, which this issue keeps and must keep. That leaves the implementer two bad
  options: treat a gate that can never pass as a failure, or "fix" it by touching unrelated code.
- **Evidence.** `grep -rn "drawProgressBar" src lib | wc -l` returns **9**. The hits include
  `BaseTheme.h:200`, `BaseTheme.cpp:115`, `BibleSearchActivity.cpp:994`,
  `OtaUpdateActivity.cpp:131`, `SdFirmwareUpdateActivity.cpp:237` and
  `CatalogSearchActivity.cpp:553`. None of them is in scope.
- **Fix.** Anchor the names, for example
  `grep -rnw -e applyChaptersReadSubtitle -e drawProgress -e loadCompletion -e STR_BIBLE_CHAPTERS_READ -e STR_BOOKS_FINISHED -e STR_CHAPTER_PREFIX -e STR_PAGES_SEPARATOR -e STR_BOOK_PREFIX src lib`.
  Adding `loadCompletion` means the gate also proves the sleep-screen loader is gone. Run it after
  `pio run`, because `lib/I18n/I18nKeys.h` and `I18nStrings.*` are generated and hold the old names
  until they are regenerated.

### MINOR 2: A-16 leaves `left` unused in `drawScreen`, which is a `-Wall` warning on a "clean build" gate

- **Claim.** A-16 deletes the strip "…the draw (`:361-364`) … No other layout change."
- **Problem.** The `drawProgress` call is the only reader of
  `const int left = viewLeft + SIDE_MARGIN;` (`StudySleepScreen.cpp:303`; the call is at `:363`).
  Delete the call and `-Wunused-variable` fires. `-Wall` is on for `src/`
  (`scripts/enable_repo_warnings.py`, `add_wall_to_repo_sources`). The issue asks for a clean
  build, and CLAUDE.md says not to commit while the build warns.
- **Evidence.** `grep -n "\bleft\b" src/activities/boot_sleep/StudySleepScreen.cpp` shows `left`
  used at `:255`/`:259` (inside `drawProgress`) and `:363` (the call), and declared in
  `drawScreen`. `width` is still used by `fitPassage` and the tag pill.
- **Fix.** Add "delete the `left` local in `drawScreen`" to A-16.

### MINOR 3: A-9 changes a non-touch row height, which contradicts the non-goal

- **Claim.** Non-goals: "every non-touch path. Nothing changes on a board without touch." A-9:
  `PublicationsActivity` passes `hasSubtitle = true`.
- **Problem.** On the non-touch branch the flag selects `listWithSubtitleRowHeight`
  (`UiListActivity.cpp:133`). Publications' non-touch rows would go from Lyra 40 to 60 (Classic
  30 to 50). That is arguably a correct fix, since its rows carry subtitles
  (`PublicationsActivity.cpp:59-60, 154`). But it is a non-touch change, and the X4 Pro takes this
  branch whenever `hasTouch()` is false (`HalGPIO.cpp:260`, for example if the GT911 fails to
  initialise).
- **Fix.** Qualify the non-goal: "except that Publications' non-touch rows grow to the subtitle
  height (A-9), which fixes a latent subtitle clip".

### MINOR 4: one planned test is a tautology, and the ≤ 0 fallback is specified in one place only

- **Claim.** Testing strategy: "Lyra rows per page: `(703 + 0) / 50 == 14` and
  `(743 + 0) / 50 == 14` … pinned as numbers". Error handling: "`ListRowHeight::resolve` never
  returns less than 1: a zero or negative token falls back to the dense row".
- **Problem.**
  - The rows-per-page test divides literals. No code runs, and it cannot fail when
    `LyraMetrics::values` changes, so it pads the suite without guarding anything. D5 keeps the
    host test away from `LyraTheme.h`, so it cannot bind to the real metrics either.
  - The ≤ 0 token fallback appears only under Error handling. It is not in the A-7 rule, and no
    listed test covers it, so the TDD step will not produce it.
- **Fix.** Drop the literal-arithmetic test and leave rows per page to device check 1. Put the
  ≤ 0 fallback into A-7, and add one test: touch, no subtitle, `touchSingleRow` 0, token 0 →
  `denseRow`, never < 1.

### MINOR 5: A-17's "all 32" is wrong for one key, and A-17 and A-19 treat the same shared-file rule differently

- **Claim.** A-17 deletes `STR_CHAPTER_PREFIX`, `STR_PAGES_SEPARATOR` and `STR_BOOK_PREFIX` "from
  **all 32**" files and edits the YAML in this branch. A-19 hands the `test/CMakeLists.txt` line to
  the orchestrator because of `ui-dev.md` "Shared files — report, do not edit".
- **Problem.**
  - `STR_PAGES_SEPARATOR` is in 31 files. `orangutan.yaml` lacks it
    (`grep -L STR_PAGES_SEPARATOR lib/I18n/translations/*.yaml`). The research table repeats the
    error.
  - `.claude/agents/ui-dev.md:22-27` names `test/CMakeLists.txt` and
    `lib/I18n/translations/*.yaml` in the same sentence. The spec obeys the rule for one file and
    overrides it for the other, and the reasoning it gives ("deletions do not collide") is a
    judgment the orchestrator owns. It is defensible: the edits are in-place deletions away from
    the append tails (`english.yaml:10, 300-302, 482`), and the issue orders the removal. But it
    should be recorded, not asserted.
- **Fix.** Say "every file that has it (31 for `STR_PAGES_SEPARATOR`)". Log the YAML exception as
  an `hpipe decide` for the orchestrator to answer. If it declines, apply A-19's hand-off pattern
  to the YAML deletions as well.

### MINOR 6: A-4's "none as a size that must hold content" misses the keyboard's touch slop

- **Claim.** A-4: `verticalSpacing` 16 → 8; "all read as 'gap', none as a size that must hold
  content."
- **Problem.** `KeyboardEntryActivity` uses `verticalSpacing` as a vertical hit tolerance for
  placing the cursor by tap (`KeyboardEntryActivity.cpp:435, 467-468`), as well as for its layout
  offsets (`:401-402, :698-699`). At 8 the line's tap band shrinks from 29 + 32 to 29 + 16 = 45
  px. That is still at least `minTouchSize` 44, so it is benign, but it is not a "gap". Device
  check 6 does not list the keyboard.
- **Fix.** Correct A-4's wording, and add the text-entry keyboard (tapping to place the cursor) to
  device check 6.

### MINOR 7: Home's tiles re-flow, so cover thumbnails regenerate once, and Resources/Data flow omit it

- **Claim.** Resources: "Home loses one completion-file load per `resolveTargets`". Data flow:
  Home "no longer opens the completion file".
- **Problem.** Home's content now starts 40 px higher (`LauncherActivity.cpp:303`), and the Bible
  tile absorbs the extra height (`:310-316`). Cover thumbnails are cached per height:
  `CoverThumb::pathFor` builds `getThumbBmpPath(height)` and, on a miss, runs `epub.load(true, true)`
  and `generateThumbBmp` (`src/util/CoverThumb.cpp:29-46`). So the first Home entry after the
  update regenerates the Bible and Meetings thumbnails, which means SD writes and a slower first
  paint. It also moves `APP_STATE.bibleCoverPath` (`LauncherActivity.cpp:136-141`), and leaves the
  old-height BMPs behind. This is expected, but the tester will see it.
- **Fix.** Add one line to Resources (a one-time thumbnail regeneration at the new tile heights;
  old thumbnails are orphaned under `/.crosspoint/epub_<hash>/`). Add to device check 5: "first
  Home after update may pause while covers regenerate".

### MINOR 8: comments the change makes stale, and a cost D2 takes back on without saying so

- **Claim.** Architecture §5 rewrites only the comment at `BaseTheme.cpp:327-332`. D2: "nothing new
  is invented."
- **Problem.**
  - Two comments become wrong in the merged state. `BaseTheme.cpp:376-380` says both layouts
    anchor the battery to "the band's top strip … it keeps the lower-right corner free for the
    manual right label", which is false for Lyra at 44/44. `LauncherActivity.h:86-88` documents
    `applyChaptersReadSubtitle` above the declaration A-15 deletes.
  - The comment D2 cites (`BaseTheme.cpp:322-326`) exists because the component route makes the
    label "shift with the percent label's width". D2 accepts that drift for Lyra (Settings'
    version text moves between "9%" and "100%") without naming it.
- **Fix.** Add the `:376-380` comment and the `.h:86-88` block to the edit list. State the
  accepted drift in D2, and have device check 2 look at Settings at a one-digit and a three-digit
  battery percent.

VERDICT: CLEAR
