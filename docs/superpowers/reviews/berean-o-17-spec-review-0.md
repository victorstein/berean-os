Tier: standard

# berean-o-17 (t4): spec review 0

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-17-design.md`, checked against the brief
(`hpipe brief --task t4`), the research note `docs/superpowers/research/2026-10-15-berean-o-17-research.md`,
feature spec §5/§8/§9/§10 (`docs/superpowers/specs/2026-10-14-preaching-tool-design.md:206-219`,
`:271-283`, `:317`), and the code at `3e5d06bf`.

## What was verified and holds

- **The seam and the tap path.** `openShow` only logs (`PreachingTopicActivity.cpp:173-175`). It is reached
  through `ACTION_QUOTE` bound at `:85`, registered in `buildHits` with `page.cardVerse[...]` as the value
  (`:327`, `:339`), and forwarded at `:350`.
- **No carry-over tap.** FreeInkUI fires an action on touch release, not on press
  (`FreeInkUICore.h` `routeAgainst`: `touchPressed` only sets `active_`; events come from the `touchReleased`
  branch). `wasTouchTap` is an edge from `touchReleasedEvent` (`InputManager.cpp:566-568`), and it is
  refreshed by `gpio.update()` before each `activityManager.loop()` (`main.cpp:657`, `:800`). So the tap that
  opens show mode cannot also land in show mode's first `loop()` as a centre or side tap.
- **Ordinals (A1).** Question cards push a `verseSource` entry exactly when they push a Verse card
  (`PreachingContent.cpp:45-46`). A tag topic is all Verse cards (`:97-100`). `layoutCards` numbers
  `cardVerse` in the same order (`PreachingCardLayout.cpp:261`). So "the Nth Verse card in `cards`" is the
  ordinal that `ACTION_QUOTE` carries and that `selectVerse` takes (`PreachingCardNav.h:40-45`).
- **Lifetime of the views.** A push moves the topic onto `stackActivities` without calling `onExit`
  (`ActivityManager.cpp:152-158`). Only the current activity's `loop()` runs (`:90`). A Replace, such as
  sleep or Home, destroys the current activity (show mode) before the stack (`:146-151`). So show mode can
  never outlive `TopicCards`.
- **Result plumbing (A16, A15).** `PreachingResult` is already in `ResultVariant`
  (`ActivityResult.h:94-102`). On pop, the manager unlocks before it calls the handler, then calls
  `requestUpdate()` on the parent (`ActivityManager.cpp:127-134`). The handler therefore has to take
  `RenderLock` itself, which the spec says, and `halfRefreshPending` is consumed at
  `PreachingTopicActivity.cpp:260`.
- **Refresh (A14).** `displayWithRefreshCycle` is the reader's page-turn call (`ReaderUtils.h:165-177`;
  `EpubReaderActivity.cpp:1613`, `:1850`). Seeding it with 0 matches `disableFastInitialRefresh`
  (`ReaderActivity.cpp:41`).
- **Fit detection (A4).** `wrapText` breaks only at an `isBreak` byte, except for the over-wide fallback,
  which cuts at `cut < wordEnd`, so the next byte is not a break (`PreachingCardLayout.cpp:207-218`). The
  spec's predicate ("ends before the text does and not on a break byte") matches only that fallback. A
  run of 128 or more bytes falls into the same fallback at every rung, so it ends at 12, which A4 accepts.
- **Fonts (A3).** All four styles of Noto Serif 18 are registered (`main.cpp:88-93`, `:312`). Ids are at
  `fontIds.h:4-7`.
- **Footer (A2).** A tag name is capped at `MAX_TAG_NAME_BYTES = 24` (`TagPalette.h:38`) and the question
  header at `char[48]` (`PreachingContent.cpp:72`). "%d / %d · %s" cannot overflow `char[96]`, so
  `snprintf` never cuts a codepoint.
- **Input defaults.** `touchReaderControls = TOUCH_READER_ON` and `tapForReaderMenu = 1`
  (`CrossPointSettings.h:363`, `:366`). The outer thirds and the centre tap therefore work out of the box.
- **Tests.** `PreachingCardsTest` exists (`test/ui_layout/CMakeLists.txt:148-163`). The existing suites are
  named `PreachingCard*`, so `ctest -R Preaching` picks up a new suite named `PreachingShow*`.

## Findings

### MAJOR

**M1 — A9's reason for leaving out a page indicator is false, and the screen beneath shows it is.**

- **Claim.** A9 says: "A page counter for a long quote would need a new string, and spec §8 gives every
  string to T3", and so concludes that a paged quote gets no in-quote indicator.
- **Problem.** A page counter needs no translated string. Without one, a paged quote reads badly in two
  ways:
  - The footer "N / M · title" stays the same on every page of the verse, so the owner cannot tell
    whether Right will turn a page or move to the next verse.
  - Under A6, every page except the last shows a quote with no reference and no sign that it continues.

  This is what the owner and householder see. It is not just a wrong rationale.
- **Evidence.**
  - The topic screen formats its page counter with a bare `"%d/%d"` and no `tr()`
    (`PreachingTopicActivity.cpp:245-246`).
  - It draws its reference mark from a non-`tr` constant (`REFERENCE_MARK = "›"`, `:35`).
  - Digits and a slash are not user-facing text under CLAUDE.md rule 5.
- **Fix.**
  - In A9: when `layout.pageCount() > 1`, show the in-quote page as `"%d/%d"`, in the same form as the topic
    header. Either append it to the footer (for example `"<position>  ·  p/n"`, built with a second
    `snprintf` into the same buffer), or draw it right-aligned on the footer row.
  - Drop the "would need a new string" rationale.
  - Add a host test that `pageCount()` reports more than one page for the long-quote fixture. The test
    plan already builds that fixture.
  - Add "the footer shows 1/3 → 2/3 → 3/3 while paging" to the device check.

### MINOR

**m1 — A11 says show mode behaves exactly like the reader for a configured reader. That is not true when
touch reader controls are Off.**

- **Claim.** A11: "an owner who has configured the reader gets the same behaviour here."
- **Problem.**
  - With `touchReaderControls` Off, the reader ignores the centre tap. It calls `isTouchMenuGesture`
    (`EpubReaderActivity.cpp:612`, `:690`), which returns false first (`ReaderUtils.h:155-157`).
  - Show mode calls `isTouchMenuTap`, as the brief names it, and that call does not check the setting
    (`ReaderUtils.h:140-147`).
  - So with Off, a centre tap still returns from show mode. That is arguably the right behaviour, since it
    is a second way out, but a tester following A11 would expect the reverse.
- **Fix.** Keep `isTouchMenuTap`. Rewrite A11 to say that with touch reader controls Off the side taps are
  silent but the centre tap still returns, unlike the reader.

**m2 — A17 leaves the cursor write for a page step outside `RenderLock`.**

- **Claim.** A17 takes `RenderLock` for a verse change. For a page step it says only "a page step inside a
  verse changes the cursor only".
- **Problem.** `render()` reads `cursor` on the render task. A write from the loop task without the lock
  is a data race. The topic screen's `moveTo` takes the lock for exactly this kind of write
  (`PreachingTopicActivity.cpp:149-155`).
- **Fix.** State that every cursor write, page or verse, happens under `RenderLock`, as in `moveTo`, and is
  followed by `requestUpdate()`.

**m3 — The `layOut` contract must clear, not reassign, or the claim of "no heap operation per step"
fails.**

- **Problem.** The resource section reserves the three vectors once and reuses them with `clear()`. The
  nearest model, `layoutCards`, starts with `out = Layout{}` (`PreachingCardLayout.cpp:226`). That frees
  the capacity, so every verse change would reallocate. An implementer who mirrors that function will
  copy the line.
- **Fix.** In the pure-module section, say that `layOut` calls `clear()` on `out.quote`, `out.reference`
  and `out.pageFirstLine` (including between rung attempts) and never assigns a new `Layout`.

**m4 — "The reference lines are on the last page only" (test plan, A6) does not follow from A5's rules.**

- **Problem.** A5 only stops the reference from *starting* a page alone. Its rule is a copy of
  `firstReference` at `PreachingCardLayout.cpp:157`. A reference that wraps to two lines can still be
  split, with its second line on a page of its own. Then the test assertion fails on a fixture with a
  narrow column. A6's phrase "on the last page" is wrong in the same case.
- **Fix.** Assert instead that the first reference line shares its page with the quote's last line, and
  that no page after it holds a quote line. Or, if the reference must stay whole, add a rule that the
  reference lines never split, falling back to one line per page only when the reference alone is taller
  than the box.

VERDICT: CLEAR
