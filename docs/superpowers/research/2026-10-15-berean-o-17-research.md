# berean-o-17 (t4) — Show mode: research

Branch `feature/preaching-show-mode`, cut from `batch/20261015-predicaci-n-193x` at `61bb11f9`
(`git merge-base --is-ancestor origin/batch/20261015-predicaci-n-193x HEAD` → true; the batch tip
is still `61bb11f9` after `git fetch`). Spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md`
§5, D6, §10 row T4.

## 1. Who owns the behaviour today

No `PreachingShow*` file exists (`ls src/activities/preaching/`: `PreachingActivity`,
`PreachingCardLayout`, `PreachingCardNav.h`, `PreachingContent`, `PreachingOrigin.h`,
`PreachingSession`, `PreachingTopicActivity`). The only hook is T3's seam:

- `PreachingTopicActivity.cpp:173` — `openShow(int verse)` logs "Show mode is not built yet" and
  returns. Declared `PreachingTopicActivity.h:46` with the comment "Show mode at `verse` (T4)".
- It is reached from a quote tap: `buildHits` registers `ACTION_QUOTE` with the card's Verse
  ordinal as the event value (`PreachingTopicActivity.cpp:339`), `onEnter` binds it
  (`:85`), and `onQuoteTapped` forwards `event.value` to `openShow` (`:350`). Only touch reaches
  it; there is no button path to show mode today.

### The data show mode will view

`TopicCards` (`PreachingContent.h`) — never copied or moved (all four copy/move members deleted),
lives behind `std::unique_ptr` in the topic screen (`PreachingTopicActivity.h`, member `topic`):

- `cards` — `std::vector<preaching::CardInput>`; each is `{kind, string_view body, string_view
  reference}` (`PreachingCardLayout.h`). For a Verse card `body` is the quote and `reference` the
  scripture reference. A question's cards view into `parsed` (`PreachingContent.cpp:40-46`); a
  tag's into `passages[i].text` (PSRAM) and `passage.reference` (`:95-99`).
- Verse cards are interleaved with Title/Heading/Text/NextVisit cards in a question; a tag topic
  is Verse cards only (`PreachingContent.cpp:96-98`).
- `verseCount()` = `verseSource.size()`; `verseSource` maps a Verse ordinal to its block/passage,
  **not** to its index in `cards`. The ordinal → card index map exists only inverted, as
  `Layout::cardVerse` (per card: its ordinal or -1, `PreachingCardLayout.h`).
- `header` — "Pregunta %d" for a question (`PreachingContent.cpp:72-74`, `STR_PREACHING_QUESTION_N`)
  or the tag's name (`:94`). The question's full title is the `Title` card's body, not `header`.

Lifetime: while show mode is on top, the topic screen is on the activity stack and its `loop()`
does not run, so `openNextQuestion` (the only thing that swaps `topic`, `:231-236`) cannot run
under it. Views into `*topic` stay valid for show mode's whole life.

### Strings (T3 owns them; already landed)

`STR_PREACHING_SHOW_POSITION: "%d / %d · %s"` — `lib/I18n/translations/english.yaml:495`,
`spanish.yaml:442`. No other show-mode string exists, and none is needed by the brief.

## 2. Control flow show mode plugs into

### Pushing and returning

- `Activity::startActivityForResult` stores the handler and calls `activityManager.pushActivity`
  (`src/activities/Activity.cpp:17-20`). `pushActivity` only sets `pendingActivity` /
  `PendingAction::Push` (`ActivityManager.cpp:257-265`); the push moves the current activity onto
  `stackActivities` without `onExit` (`:152-155`), so the topic screen stays alive.
- `finish()` → `popActivity()` (`Activity.cpp:24`, `ActivityManager.cpp:267-274`). On pop the
  manager moves `result` out, destroys the child (`exitActivity`), restores the parent, **unlocks
  the render lock**, calls the handler, then `requestUpdate()`s the parent (`ActivityManager.cpp:104-133`).
  So the handler runs on the loop task without the render lock, and must take `RenderLock` itself
  to mutate render state — as `PreachingTopicActivity::moveTo` already does (`:149-155`).
- `ActivityResultHandler` is `std::function<void(const ActivityResult&)>` (`ActivityResult.h:114`).
  That is the existing API every caller uses (e.g. `HighlightsActivity.cpp:418-428`); a lambda
  capturing only `this` is the established shape.
- Result type: `PreachingResult { PreachingOrigin at; }` — "The Predicación topic, and the Verse
  card within it, a screen ended on, for the screen beneath to select" (`ActivityResult.h:92-96`).
  `PreachingOrigin::verse` is the Verse ordinal (`PreachingOrigin.h:13`). This fits show mode's
  return exactly, so no new variant member is needed (`ActivityResult.h` is outside T4's files).
  `PreachingActivity.cpp:271` already reads it with `std::get_if<PreachingResult>`.
- Selecting the returned card: `preaching::selectVerse(layout, verse)` returns the cursor on the
  page where that Verse card begins, selected (`PreachingCardNav.h`, "selectVerse"); out of range
  falls back to the first Verse card.
- `UiAppHost` tap flash: every existing exit from the topic screen calls `app.clearTapFlash()`
  first (`PreachingTopicActivity.cpp:205`, `:237`). A push should too, or the flashed quote rect
  is drawn when the screen comes back.

### Input the topic screen and the reader use

- Back: `mappedInput.wasReleased(Button::Back)` (`PreachingTopicActivity.cpp:122`). On this
  device Back *is* the left-edge swipe: `wasReleased` returns true on `wasBackGesture()`
  (`MappedInputManager.cpp:265`), which is `wasEdgeSwipe(ScreenEdge::Left)` (`:222-227`).
- Left/Right in the topic screen: `ButtonNavigator::onNextRelease/onPreviousRelease`
  (`PreachingTopicActivity.cpp:139-147`), which act on `Button::NavNext/NavPrevious`
  (`ButtonNavigator.h`, `NEXT_BUTTONS`/`PREVIOUS_BUTTONS`). `ButtonNavigator::Callback` is
  non-allocating (`ButtonNavigator.h:9-26`).
- The reader's page turn has two helpers in `src/activities/reader/ReaderUtils.h`:
  - `detectPageTurn` (`:53`) — Left/Right/PageBack/PageForward, honouring
    `isNavDirectionSwapped`, tilt, the power-button page turn and `longPressButtonBehavior`.
  - `detectTouchPageTurn` (`:79`) — **outer thirds, full height** (`:104-112`); returns nothing
    when `SETTINGS.touchReaderControls` is Off, uses horizontal swipes instead of taps when it is
    `TOUCH_READER_SWIPE` (`:85-95`), and swaps sides when it is `TOUCH_READER_INVERTED_TAP` (`:108`).
- The reader's centre tap: `isTouchMenuTap` (`ReaderUtils.h:140-147`) is the **centre third in
  both axes** (`isInMenuZone`, `:132-138`), and returns false when `SETTINGS.tapForReaderMenu` is
  off. The centre column's top and bottom thirds are therefore dead in the reader. The reader
  reads both helpers in one `loop()` (`EpubReaderActivity.cpp:590`, `:612`): `wasScreenTapped`
  → `HalGPIO::wasTouchTap` (`MappedInputManager.cpp:136-143`, `HalGPIO.cpp:268`) is a per-frame
  query, not a consuming one.
- `ReaderUtils.h` is included outside `src/activities/reader/` already
  (`src/activities/boot_sleep/SleepActivity.cpp`, `src/CrossPointSettings.h`), so show mode may
  include it.

### Refresh

- Reader page turn: `ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh)`
  (`ReaderUtils.h:165-177`; called at `EpubReaderActivity.cpp:1613` and `:1850`). It shows
  `FAST_REFRESH`, and `HALF_REFRESH` when the counter reaches 1, then reloads it from
  `SETTINGS.getRefreshFrequency()` (1/5/10/15/30, `CrossPointSettings.cpp:330-342`).
- The reader seeds the counter in `ReaderActivity`'s constructor: `refreshFrequency > 1 ?
  refreshFrequency : 2` when a fast first refresh is allowed, else 0 (= HALF first)
  (`ReaderActivity.cpp:19-23`, `:41`).
- The topic screen today: `HALF_REFRESH` once on entry/new question, `FAST_REFRESH` otherwise,
  via `halfRefreshPending` (`PreachingTopicActivity.cpp:260`, `.h:72`). After a pop the manager
  just `requestUpdate()`s it, so the screen returning from a full-screen large-type view would
  redraw with `FAST_REFRESH` unless the result handler sets `halfRefreshPending`.
- Reader text anti-aliasing is a separate grayscale pass (`EpubReaderActivity.cpp:1835-1880`);
  the brief asks for the reader's refresh *mode*, and the topic screen draws B/W only.

### Auto-sleep

`Activity::preventAutoSleep()` defaults to false (`Activity.h:45`); not overriding it leaves
auto-sleep unchanged.

## 3. Fonts and the size ladder

- `NOTOSERIF_18_FONT_ID`, `_14_`, `_12_` exist (`src/fontIds.h:4-7`) and are registered at boot
  (`src/main.cpp:88-92`, `:312`) with regular/bold/italic/bold-italic. D6: no new font.
- `HomeVerse::FONT_IDS`/`FONT_STYLES` (`src/activities/launcher/HomeVerse.h:23-26`) is a
  `static constexpr` 3-rung table — `{NOTOSERIF_14, NOTOSERIF_12, UI_10}`, italic/italic/regular —
  i.e. the *pattern* the brief names, not the sizes. Show mode needs its own table
  `{18, 14, 12}`.
- The fit routine both HomeVerse and the study sleep screen use is
  `study_sleep::fitPassage` (`src/activities/boot_sleep/StudySleepFit.h:101-119`): first rung whose
  greedy wrap fits width and height wins; nothing cut; `fits == false` otherwise. It **copies each
  line into a `std::string`** (`fit_detail::setLines`, `:90-94`), and splits on U+0020 only.
- `preaching::wrapText` (`PreachingCardLayout.h`, impl `PreachingCardLayout.cpp:174`) returns
  `Span{begin, length}` byte spans with no copy; breaks at U+0020 and tab/CR/LF (which the topic
  screen draws as spaces, `PreachingTopicActivity.cpp:290-295`), keeps U+00A0/U+202F together,
  and breaks an over-wide word at a codepoint boundary; a line is at most `LINE_BUFFER_BYTES`
  (128) bytes. Its `MeasureFn` is `(ctx, Face, text)`; the `ctx` pointer can carry the font size,
  so it can measure at any of the three rungs without changing its signature.
  Using spans keeps show mode's "holds no text of its own" (spec §5, §9 "Show mode holds no text
  of its own") — `fitPassage` would not.

## 4. Nearest existing examples

| Need | Model | Where |
|---|---|---|
| Centred passage + bold reference + small footer, sized from `getOrientedViewableTRBL`, rung ladder 18→…→12 | `StudySleepScreen` | `src/activities/boot_sleep/StudySleepScreen.cpp:47-75` (rung table), `:104-122` (layout from TRBL), `:177-221` (draw: block height → vertical centre, `drawCenteredText`) |
| Rung table as `static constexpr` font ids + styles | `HomeVerse` | `HomeVerse.h:23-26` |
| Pure, host-tested layout free of Arduino, widths via `MeasureFn` | `PreachingCardLayout` / `PreachingCardNav` | `PreachingCardLayout.h` header comment; tests `test/ui_layout/PreachingCardLayoutTest.cpp` (fake measure `widthByBytes`, 10 px/byte), `PreachingCardNavTest.cpp` |
| Child pushed for a result, parent acts on it | `HighlightsActivity::openLinks` | `HighlightsActivity.cpp:415-428` |
| Reader-style refresh cadence | `ReaderUtils::displayWithRefreshCycle` | `ReaderUtils.h:165-177` |

## 5. Host tests

- Suite: `test/ui_layout/`, added at `test/CMakeLists.txt:102`. Its own `CMakeLists.txt` already
  has `PreachingCardsTest` built from `PreachingCardLayoutTest.cpp`, `PreachingCardNavTest.cpp`
  and `src/activities/preaching/PreachingCardLayout.cpp`. A new test file joins that target (or a
  sibling target in the same file) — no new top-level suite, and `test/CMakeLists.txt` (a shared
  append point, `ui-dev.md`) is untouched.
- Build/run per `test/README`: `cmake -S test -B build/test && cmake --build build/test &&
  ctest --test-dir build/test --output-on-failure -j`.

## 6. Installed tools

```
$ ~/.platformio/penv/bin/pio --version   → PlatformIO Core, version 6.1.19
$ which pio                              → pio not found   (use the penv path)
$ cmake --version                        → cmake version 4.4.2
$ c++ --version                          → Apple clang version 21.0.0 (clang-2100.0.123.102)
platformio.ini:15  platform = pioarduino platform-espressif32 55.03.37
test/CMakeLists.txt:17  googletest GIT_TAG v1.17.0
```

This worktree has no `.pio/` and no `build/` yet; `./bin/bootstrap` (or
`./.claude/pipeline-bootstrap`) runs before the first build in `implement`.

## 7. Open points for the spec (not decided here)

1. **View shape.** Show mode needs ordinal → card. Options: take `const TopicCards&` and the
   topic's `Layout::cardVerse`, or build a small `uint16_t` card-index table (one entry per Verse
   card, no text). Either holds no text.
2. **Footer title.** `topic->header` is "Pregunta 8" / the tag name; the question's title (the
   Title card) is long. The brief says "N / M · title", the spec "<question or tag>". `header`
   fits a small footer and is the same string the topic screen's header shows.
3. **Settings the reader helpers honour.** `detectTouchPageTurn` is silent with touch reader
   controls Off and swipe-based in Swipe mode; `isTouchMenuTap` is silent with `tapForReaderMenu`
   off. Using them verbatim makes show mode behave exactly as the reader does for that user
   (the brief says "use the reader's zones"); the left-edge swipe Back always works.
4. **Where the reference sits when a quote pages.** Below the quote on every page, or only on
   its last page.
5. **Refresh on return.** Whether the topic screen's handler sets `halfRefreshPending` so the
   return from large type clears ghosting.
6. **Quote face.** Italic, as the topic screen's Quote face (`PreachingTopicActivity.cpp:46`) and
   HomeVerse; reference bold (brief).

## 8. Tier

Standard holds. The change stays inside `src/activities/preaching/PreachingShow*`,
`PreachingTopicActivity.*`, and `test/ui_layout/` (the brief's named suite). It reuses
`PreachingResult` and `ReaderUtils` as they are: no new result variant, no string, no on-disk
format, no input-layer change, no other surface.
