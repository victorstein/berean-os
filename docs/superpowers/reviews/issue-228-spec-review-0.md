Tier: light

# Issue #228 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-30-issue-228-design.md`.
Checked against issue #228 (`gh issue view 228 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-30-issue-228-research.md`, and the code at `1b1b6839`.

## What was checked and holds

- **Line anchors.** `BaseTheme.cpp:583` (`sb = SETTINGS.statusBarSpec()`), `:584`
  (`showStatusBarTextLane`), `:595` / `:602` / `:605` (the three `showBookProgressPercent` reads),
  `:625-626` (the `BOOK_PROGRESS` branch) and `:627-630` (the chapter branch with the `pageCount > 0`
  guard at `:629`) are all correct. `EpubReaderActivity.cpp:1796-1826` (`renderStatusBar`), `:1799`,
  `:1800`, `:1824-1825`, `:1788`, `:274`, `:857`, `:938`, `:768-773` and `:1944` are correct.
  `BaseTheme.h:239-242`, `CrossPointSettings.h:93-97` / `:265-270`, `UITheme.cpp:132-147`,
  `StatusBarSettingsActivity.cpp:279` and `USER_GUIDE.md:379-381` are correct. The spec's
  correction of the research note's BaseTheme ranges is right: the text block is `:595-615` and the
  bar block `:617-634`, not `594-610` / `614-630`.
- **Callers.** `grep -rn drawStatusBar src lib` finds only the reader (`EpubReaderActivity.cpp:1824`)
  and the preview (`StatusBarSettingsActivity.cpp:279`). The preview passes 8 arguments, so a
  trailing `= true` default leaves it byte-for-byte unchanged, as the spec claims.
- **Rule correctness.** Replacing `:595` with `progressView.showBookPercent || sb.showChapterPageCount`
  means that in a Bible with only the percent enabled, the text block is skipped entirely (no stray
  `0%` or empty string), and `rightClusterWidth` stays 0. With `showWholeBookProgress == true`,
  `resolve` is the identity on both outputs, so non-Bible output is provably unchanged for all
  2 × 3 setting combinations. That covers the issue's second acceptance criterion.
- **A1.** `getBibleBookNavSpineIndex()` is memoised in a `mutable std::optional<int>`
  (`lib/Epub/Epub.cpp:980-984`, `Epub.h:36`), so it is callable from the `const` `renderStatusBar`,
  and after the first call it costs one comparison per status-bar draw. `epub` is a
  `std::shared_ptr<Epub>` (`EpubReaderActivity.h:24`), so `epub && ...` is well-formed.
- **"Chapter" means Bible chapter.** One spine item is one Bible chapter
  (`resolveBibleChapterNumber`, `EpubReaderActivity.cpp:1763-1780`, keys the chapter number per
  spine index). The chapter bar the Bible falls back to therefore really does reset at each chapter,
  as the device check expects.
- **A4.** The bookmark icon still gates on `showStatusBarTextLane` (`BaseTheme.cpp:674`), which
  keeps reading the unadjusted `sb`. That matches the reserved lane, which `UITheme.cpp:132-139`
  also computes from `sb`. So the "empty lane" corner case is internally consistent: the icon draws
  into space that is actually reserved. The defaults claim holds: page count 1, title
  `CHAPTER_TITLE`, battery 1 (`CrossPointSettings.h:265-270`). Keeping the reservation
  Bible-agnostic, so that `EpubReaderActivity.cpp:1136-1140` never changes the viewport, is the
  correct trade-off.
- **A2 / A3.** `drawStatusBar` has one definition (non-virtual, `BaseTheme.h:239`). The
  `ListRowHeight.h` pattern (header-only, `constexpr`, plain types) is accurately mirrored.
  `test/ui_layout/CMakeLists.txt` is not on `ui-dev.md`'s shared-file list
  (`.claude/agents/ui-dev.md:22-27` names only `test/CMakeLists.txt`, the translation YAMLs and
  `src/main.cpp`), and `ui_layout` is already added at `test/CMakeLists.txt:102`. So registering the
  new test there needs no hand-off. This resolves the research note's "one shared-file append"
  concern.
- **A7.** `estimatePrefix` is used only in the two page-count formats (`BaseTheme.cpp:600-608`).

## Findings

### MINOR 1: The "default-constructed Inputs" test guards a path that never uses it

- **Claim.** The testing strategy says: "Default-constructed `Inputs` has
  `showWholeBookProgress == true` (the preview path's default)."
- **Problem.** The preview path's default is the `drawStatusBar` parameter default
  (`const bool showWholeBookProgress = true`), not `Inputs`'s member initialiser. `drawStatusBar`
  always builds `Inputs` explicitly, as `{showWholeBookProgress, sb.showBookProgressPercent, ...}`
  (spec, "Changed: BaseTheme::drawStatusBar"). The `Inputs` default is never consulted in firmware.
  The test would pass even if the parameter default were flipped to `false`, which is the
  regression it claims to catch.
- **Evidence.** Spec, Architecture section: the positional brace-init of `Inputs` inside
  `drawStatusBar`. The preview call at `StatusBarSettingsActivity.cpp:279` passes 8 arguments and
  relies on the parameter default.
- **Fix.** Drop that case, or relabel it as a check on the struct's own default, without the
  preview claim. The preview guarantee is a device/visual check, and the spec already lists it
  ("The Settings preview still shows `8/32  75%`").

### MINOR 2: Positional init of three adjacent bools can be silently transposed

- **Claim.** `StatusBarProgress::resolve({showWholeBookProgress, sb.showBookProgressPercent, sb.progressBarMode == ...BOOK_PROGRESS})`.
- **Problem.** All three fields are `bool`. If the order is swapped at the one call site, it still
  compiles, and the host test cannot catch it because the test never sees the call site. With
  `showWholeBookProgress` and `showBookPercent` both true by default, a swap would be invisible on
  non-Bible books and wrong only in a Bible.
- **Evidence.** The `Inputs` field order in the spec's header sketch. The build is `-std=gnu++2a`
  (`platformio.ini:40`), so C++20 designated initialisers are available.
- **Fix.** Write the call with designated initialisers:
  `resolve({.showWholeBookProgress = showWholeBookProgress, .showBookPercent = sb.showBookProgressPercent, .barTracksBook = sb.progressBarMode == ...BOOK_PROGRESS})`.

### MINOR 3: The screenshot filename still carries whole-Bible %, which is worth naming for the owner

- **Claim.** Non-goals: the `calculateProgress` use at `:1944` is "not a status-bar indicator".
- **Problem.** That claim is accurate, and excluding it is within the issue's scope. But the
  issue's goal line is "Remove the last Bible reading-progress indicator", and `:1944` feeds
  `ScreenshotInfo::progressPercent` ("whole-book progress", `src/util/ScreenshotInfo.h:11`) into a
  user-visible filename, `…_ch%d_p%d_%dpct_…bmp` (`src/util/ScreenshotUtil.cpp:32-41`). A reader
  who takes the spec's wording at face value could believe no Bible % survives anywhere.
- **Fix.** Reword the non-goal to say what `:1944` is: "the screenshot filename's `%dpct` field
  (`ScreenshotUtil.cpp:40`), which is file metadata, not on-screen UI; a follow-up if the owner
  wants it gone." Do not change scope here.

### MINOR 4: Function range cited as `BaseTheme.cpp:576-630`

- **Claim.** "Changed: `BaseTheme::drawStatusBar` (`BaseTheme.h:239-242`, `BaseTheme.cpp:576-630`)."
- **Problem.** The function runs well past `:630`: battery `:636`, clock `:655`, bookmark `:673`,
  title `:682` onwards. All edits do fall inside `:583-630`, so this is imprecise rather than wrong.
- **Fix.** Cite `BaseTheme.cpp:576` (definition) and `:583-630` (edited region).

VERDICT: CLEAR
