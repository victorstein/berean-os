# berean-o-17 (t4) — Show mode: design

Source: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §5, D6, §9, §10 row T4.
Research: `docs/superpowers/research/2026-10-15-berean-o-17-research.md` (cited below as "R§n").
Surface: ui. Tier: standard. Files: `src/activities/preaching/PreachingShow*`,
`src/activities/preaching/PreachingTopicActivity.{h,cpp}`, `test/ui_layout/` (its own
`CMakeLists.txt` and a new test file).

Modelled on:
- **Screen:** `src/activities/boot_sleep/StudySleepScreen.cpp` — a centred passage with a bold
  reference below and a small footer, sized from `getOrientedViewableTRBL`, on a
  `constexpr` font-rung ladder that starts at Noto Serif 18 (`:47-75`, `:104-122`, `:177-221`).
- **Pure layout and navigation:** `src/activities/preaching/PreachingCardLayout.{h,cpp}` and
  `PreachingCardNav.h` — Arduino-free, widths through a `MeasureFn`, tested in
  `test/ui_layout/PreachingCardLayoutTest.cpp` / `PreachingCardNavTest.cpp`.
- **Push for a result:** `HighlightsActivity::openLinks` (`src/activities/reader/HighlightsActivity.cpp:415-428`).
- **Refresh cadence:** `ReaderUtils::displayWithRefreshCycle` (`src/activities/reader/ReaderUtils.h:165-177`).

## Problem

A quote tap on a topic card is wired up but goes nowhere. `PreachingTopicActivity::openShow`
logs "Show mode is not built yet" and returns (`PreachingTopicActivity.cpp:173-175`); the tap
reaches it through `ACTION_QUOTE` (`:85`, `:339`, `:350`). The owner has no way to turn the
device toward a householder and show one verse large.

## Goal

Tapping a quote opens `PreachingShowActivity` at that Verse card:

- no header, no chrome; the quote centred vertically, the reference bold below it, a small
  footer "N / M · title" (`STR_PREACHING_SHOW_POSITION`, `english.yaml:495`);
- the quote set in Noto Serif 18, else 14, else 12 — the first that fits; past 12 it pages at
  12; nothing is cut;
- Left/Right and the outer thirds step page/verse; the centre tap and the left-edge swipe return;
- it returns the Verse ordinal it ended on, and the topic screen selects that card;
- it holds no copy of the text, and a verse change refreshes as a reader page turn does.

## Non-goals

- No new string (all are T3's, spec §8); no new font (D6); no change to `ActivityResult.h`,
  `ReaderUtils.h`, `MappedInputManager`, `PreachingCardLayout.*` or any other surface.
- No button path *into* show mode. Today only a touch on a quote reaches `openShow`
  (R§1); the brief asks for a quote tap.
- No grayscale anti-aliasing pass. The topic screen draws 1-bit (`PreachingTopicActivity.cpp:260`);
  the brief asks for the reader's refresh *mode*, not its text rendering.
- No change to auto-sleep: `preventAutoSleep()` is not overridden (`Activity.h:45`).

## Architecture

```
PreachingTopicActivity ──openShow(verse)──► startActivityForResult(PreachingShowActivity)
        ▲                                        │ reads (const) TopicCards::cards, header
        │ PreachingResult{at.verse}              │ lays out the current verse with
        └──────── handler: selectVerse ◄── finish│   preaching::show::layOut (pure)
                                                 │ steps with preaching::show::step (pure)
```

### New files

| File | What |
|---|---|
| `src/activities/preaching/PreachingShowLayout.h` / `.cpp` | Pure: the deck view, the rung fit, the in-quote paging, cursor stepping. No Arduino, no renderer. |
| `src/activities/preaching/PreachingShowActivity.h` / `.cpp` | The screen: geometry, input, drawing, refresh, result. |
| `test/ui_layout/PreachingShowLayoutTest.cpp` | Host tests for the pure module. |

### Changed files

| File | Change |
|---|---|
| `src/activities/preaching/PreachingTopicActivity.cpp` | `openShow` builds and pushes the show screen; its handler selects the returned card and asks for a half refresh. |
| `src/activities/preaching/PreachingTopicActivity.h` | Drops "(T4)" from the `openShow` comment; no new members. |
| `test/ui_layout/CMakeLists.txt` | `PreachingShowLayoutTest.cpp` and `PreachingShowLayout.cpp` join the `PreachingCardsTest` target. |

`test/CMakeLists.txt` already adds `ui_layout` (`:102`) and is not touched.

## The pure module — `preaching::show` (`PreachingShowLayout.h`)

```cpp
namespace preaching::show {

inline constexpr uint8_t RUNG_COUNT = 3;          // Noto Serif 18, 14, 12 — indices into the screen's table
inline constexpr uint8_t PAGING_RUNG = RUNG_COUNT - 1;

// A read-only view of a topic's cards. Views only: TopicCards outlives the show screen.
struct Deck {
  const CardInput* cards = nullptr;
  size_t count = 0;
};
int verseCount(const Deck& deck);
const CardInput* verseCard(const Deck& deck, int ordinal);   // nullptr out of range

// Width of `text` at `rung` in `face` (Face::Quote or Face::Reference).
using MeasureFn = int (*)(const void* ctx, uint8_t rung, Face face, const char* text);

struct Metrics {
  int width = 0;                       // the text column, px
  int height = 0;                      // the box the quote and reference share, px
  int referenceGap = 0;                // between the last quote line and the reference
  int lineHeight[RUNG_COUNT] = {};
};

struct Layout {
  uint8_t rung = 0;
  std::vector<Span> quote;             // spans into the card's body
  std::vector<Span> reference;         // spans into the card's reference
  std::vector<uint16_t> pageFirstLine; // page p starts at line pageFirstLine[p] of quote+reference
  int pageCount() const;
  // ...page height for vertical centring, and whether a page carries the reference.
};

void layOut(const CardInput& card, const Metrics& metrics, MeasureFn measure, const void* ctx, Layout& out);

struct Cursor { int verse = 0; int page = 0; };
// direction > 0: next page, else next verse at page 0. False at either end.
bool step(Cursor& cursor, int pagesInVerse, int verses, int direction);

}  // namespace preaching::show
```

Exact member names are the plan's; the contract is above. `layOut` **reuses** `out`: it calls
`clear()` on `out.quote`, `out.reference` and `out.pageFirstLine` before each rung attempt and never
assigns a fresh `Layout` — unlike `layoutCards`, which starts with `out = Layout{}`
(`PreachingCardLayout.cpp:226`) and so drops the capacity reserved in `onEnter` (review 0, m3). `Span`, `CardInput`, `Face`,
`LINE_BUFFER_BYTES` and `wrapText` come from `PreachingCardLayout.h` unchanged.

**Wrapping reuses `preaching::wrapText`** (`PreachingCardLayout.h`, impl `PreachingCardLayout.cpp:174-222`):
it returns `{begin, length}` spans with no copy, keeps U+00A0/U+202F words together, breaks at
space/tab/CR/LF, and breaks an over-wide word at a codepoint boundary (R§3). Its `MeasureFn` is
`(ctx, Face, text)`, so `layOut` passes it a small adapter struct `{show MeasureFn, ctx, rung}`
as `ctx` — `wrapText`'s signature is untouched.

## Assumptions (each is a reviewable decision)

**A1 — The view is `Deck{topic->cards.data(), topic->cards.size()}`.** Show mode walks the cards
to find the Nth `CardKind::Verse` (`verseCard`) and counts them (`verseCount`). It allocates no
table. Verse ordinals counted this way are the topic's ordinals: `PreachingContent` pushes a
`verseSource` entry exactly when it pushes a Verse card (`PreachingContent.cpp:45-46`, `:96-98`),
and `Layout::cardVerse` numbers them the same way. A linear scan over a topic's few dozen cards per
verse change is negligible next to a panel refresh. Rejected: passing `const TopicCards&`
(couples the pure module to `PreachingContent.h`, which pulls in `QuestionParser` and
`StudyStore` types) and building an ordinal→card table (an allocation to save a scan of tens).

**A2 — The footer title is `topic->header`.** That is "Pregunta 8" for a question
(`PreachingContent.cpp:72-74`) or the tag's name (`:94`) — the same text the topic screen's header
shows (`PreachingTopicActivity.cpp:247-248`). It matches spec §5's "<question or tag>". The
question's full title (its Title card) is long and would not fit a small footer line. The show
screen keeps a `const char*` to `header`'s buffer, valid for the same reason as A1's views. If the
formatted footer is wider than the column, it is shortened with `renderer.truncatedText`
(`GfxRenderer.h:297`) — a rare path, and a footer draw is once per refresh, not a hot loop.

**A3 — The rung table is `{NOTOSERIF_18, NOTOSERIF_14, NOTOSERIF_12}`**, a `static constexpr`
pair of arrays in `PreachingShowActivity.cpp`, as `HomeVerse::FONT_IDS`/`FONT_STYLES`
(`HomeVerse.h:23-26`) and `StudySleepScreen`'s `PASSAGE_FONT_IDS` (`:50-53`). All three fonts are
registered at boot (`src/main.cpp:88-92`, `:312`; ids `src/fontIds.h:4-7`). The quote is **italic**
(the topic screen's Quote face, `PreachingTopicActivity.cpp:46`, and HomeVerse's style); the
reference is **bold** at the same rung's size, so the pair scales together.

**A4 — "Fits" at a rung means:** the wrapped quote, the gap and the wrapped reference stack within
`Metrics::height` on one page, **and** no word of the quote was broken mid-word by `wrapText`'s
over-wide fallback. 18 and 14 are tried in order; the first that fits wins. A word wider than the
column at 18 therefore steps the size down rather than splitting the word — the same rule
`study_sleep::fitPassage` applies ("A word too wide for any line marks the wrap as overflowing",
`StudySleepFit.h:67-68`, `:111`). A broken word is detected as a span that ends before the text
does and not on a break byte (space/tab/CR/LF, `PreachingCardLayout.cpp:6`; the show module keeps
its own one-line copy of that predicate, since the original is file-local). 12 is the floor
and always accepted.

**A5 — Past 12, the quote pages at 12.** The quote lines and then the reference lines form one
column, broken into pages by `Metrics::height`, never inside a line. Nothing is cut. Three rules,
the first two from the topic layout (`PreachingCardLayout.cpp:146-161`):
- every page takes at least one line, so a box smaller than one line still advances;
- **the reference never starts a page alone**: if a page would end exactly before the first
  reference line, its last quote line moves to the next page with the reference;
- **the reference is never split across pages** (review 0, m4): the quote's last line, the gap and
  every reference line go on one page together. Only when that block alone is taller than the box
  does the column fall back to filling pages line by line (the first rule), so it still advances.
The gap above the reference is only counted on the page that carries both.

**A6 — The reference is drawn only after the quote's last line** (on the last page when the quote
pages), not on every page. This mirrors a Verse card continued across topic pages, where the
reference closes the card (spec §4, "a card taller than a page continues on the next"). Repeating
it on every page would take lines from an already-overflowing quote.

**A7 — Every page is centred vertically** in the box: its content height
(lines × line height, plus the gap when it carries the reference) offset by `(height − content) / 2`,
as `StudySleepScreen` centres its block (`StudySleepScreen.cpp:187-190`). Lines are centred
horizontally with `drawCenteredText` (`GfxRenderer.h:280`), as there (`:202`, `:207`).

**A8 — Geometry from the viewable area and theme metrics, no hardcoded pixels.**
`getOrientedViewableTRBL` (`GfxRenderer.h:205`) gives the insets; `UITheme` metrics give the
spacing (`contentSidePadding`, `verticalSpacing`, `BaseTheme.h:32,37`), as the topic screen uses
them (`PreachingTopicActivity.cpp:92-106`):
- column: `screenWidth − viewLeft − viewRight − 2·contentSidePadding`, starting at
  `viewLeft + contentSidePadding`;
- footer: `UI_10_FONT_ID` (the topic screen's small label face, `:45`, `:50`), its baseline row at
  `screenHeight − viewBottom − verticalSpacing − lineHeight(UI_10)`, centred;
- box: from `viewTop + verticalSpacing` to `footerY − verticalSpacing`;
- reference gap: `verticalSpacing`.

**A9 — A paged quote shows its page in the footer.** When `layout.pageCount() > 1`, the footer is
`STR_PREACHING_SHOW_POSITION` followed by ` · %d/%d` (page, pages), appended with a second
`snprintf` into the same buffer; a one-page verse shows the position alone. The bare `"%d/%d"` is the
form the topic screen's own page counter already uses without `tr()`
(`PreachingTopicActivity.cpp:245-246`): digits and punctuation are not user-facing text, so no new
string is needed. Without it the footer would stay the same on every page of a verse, and the owner
could not tell whether Right turns a page or moves on (review 0, M1). Buffer: position ≤ ~10 B,
title ≤ 47 B (`PreachingContent.cpp:72`) or a 24 B tag name (`lib/StudyStore/StudyStore/TagPalette.h:38`), page ≤ 10 B — well
inside `char[96]`.

**A10 — Input, read once per `loop()` in this order:**
1. `mappedInput.wasReleased(Button::Back)` → finish. On this device that includes the left-edge
   swipe (`MappedInputManager.cpp:222-227`, `:265`). Same call as the topic screen (`:122`).
2. `ReaderUtils::isTouchMenuTap(renderer, mappedInput)` → finish. Centre third in both axes
   (`ReaderUtils.h:132-147`).
3. `ReaderUtils::detectTouchPageTurn(renderer, mappedInput)` → `prev`/`next` step
   (`ReaderUtils.h:79-122`): outer thirds, full height.
4. `buttonNavigator.onNextRelease` / `onPreviousRelease` → step, as the topic screen
   (`PreachingTopicActivity.cpp:139-147`); `NavNext`/`NavPrevious` are Right/Left, with the
   orientation swap (`MappedInputManager.cpp:104-112`).

Reading both touch helpers in one loop is what the reader does (`EpubReaderActivity.cpp:590`,
`:612`); `wasScreenTapped` is a per-frame query (`MappedInputManager.cpp:136-143`).

**A11 — Show mode uses the reader's touch helpers, so it mostly follows the reader's touch
settings.** With touch reader controls Off, side taps do not page (`ReaderUtils.h:81-83`) — buttons
still do. In Swipe mode a horizontal swipe pages instead of a tap (`:85-95`). With
`tapForReaderMenu` off the centre tap does nothing (`:142`); the left-edge swipe still returns. The
centre column's top and bottom thirds are dead, as in the reader.
**One difference from the reader, kept on purpose:** with touch reader controls Off the reader
also ignores the centre tap, because it calls `isTouchMenuGesture`, which checks that setting first
(`ReaderUtils.h:155-157`; `EpubReaderActivity.cpp:612`, `:690`). Show mode calls `isTouchMenuTap`
as the brief names it, which does not (`ReaderUtils.h:140-147`), so its centre tap still returns —
a second way out of a screen with no chrome (review 0, m1). Defaults are touch On and centre tap
On (`CrossPointSettings.h:363`, `:366`).

**A12 — Buttons use `ButtonNavigator`, not `ReaderUtils::detectPageTurn`.** `detectPageTurn`
also turns on tilt (`ReaderUtils.h:55-56`) and on the power button when so configured (`:64-65`).
Turning the device toward a householder is exactly a tilt; a verse change from that is wrong
here. The brief names Left/Right only; the topic screen beneath already uses `ButtonNavigator`.

**A13 — Stepping.** `next`: the next page of this verse, else the next verse at page 0; at the
last page of the last verse, nothing. `previous`: the previous page of this verse, else the
**previous verse at page 0**; at the first page of the first verse, nothing. Going back opens a
verse at its start, because the householder reads a verse from its beginning. A step that moves
nowhere draws nothing and refreshes nothing.

**A14 — Refresh.** A member `int pagesUntilFullRefresh = 0`; every render shows the frame with
`ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh)` (`ReaderUtils.h:165-177`).
Starting at 0 makes the first frame — the switch from small cards to large type — a
`HALF_REFRESH`, as the reader does when a fast first refresh is not allowed
(`ReaderActivity.cpp:41`); after that it is `FAST_REFRESH` with a `HALF_REFRESH` every
`SETTINGS.getRefreshFrequency()` changes, as a reader page turn.

**A15 — The topic screen half-refreshes on return.** The result handler sets
`halfRefreshPending = true` (`PreachingTopicActivity.h:72`, consumed at `.cpp:260`); the manager
then re-renders it (`ActivityManager.cpp:130-133`). Without it, the return from full-screen large
type would be a `FAST_REFRESH` and ghost.

**A16 — The result is `PreachingResult` with only `at.verse` set.** `PreachingResult` already
means "the Verse card a screen ended on, for the screen beneath to select"
(`ActivityResult.h:92-96`); `PreachingOrigin::verse` is the ordinal (`PreachingOrigin.h:13`).
Show mode does not know the topic's kind, and its only reader is the topic screen's handler, which
reads `at.verse` alone. No new variant member (`ActivityResult.h` is not T4's). Every way out —
Back, swipe, centre tap — sets the result, so the topic always follows the verse.

**A17 — Layout runs on the loop task, under `RenderLock`.** On enter and on each verse change, the
loop takes `RenderLock` (as `PreachingTopicActivity::moveTo`, `:149-155`), sets the cursor and
lays out the new verse into the member `Layout`, then `requestUpdate()`s. `render()` only draws
from the member layout. **Every cursor write — a page step as well as a verse change — happens
under `RenderLock`** and is followed by `requestUpdate()`, because `render()` reads the cursor on
the render task (review 0, m2). A page step changes the cursor only; it does not re-lay out.

**A18 — The topic screen clears its tap flash before pushing**, as every other exit does
(`PreachingTopicActivity.cpp:205`, `:237`), so the flashed quote rect is not drawn on return.

## Data and control flow

1. **Tap.** `buildHits` → `ACTION_QUOTE` with the Verse ordinal → `onQuoteTapped` →
   `openShow(verse)` (`PreachingTopicActivity.cpp:339`, `:350`).
2. **`openShow(verse)`**: returns if `verse` is outside `[0, topic->verseCount())`. Builds
   `makeUniqueNoThrow<PreachingShowActivity>(renderer, mappedInput, Deck{…}, topic->header.c_str(), verse)`;
   null → `LOG_ERR` + `reportFailure(tr(STR_PREACHING_OPEN_FAILED))` (`:168-171`), stay. Else
   `app.clearTapFlash()` (A18) and `startActivityForResult(std::move(show), handler)`; the handler
   is a lambda capturing `this` only, the shape `HighlightsActivity.cpp:418-428` uses.
3. **Push.** The manager stacks the topic screen without `onExit` and enters show mode
   (`ActivityManager.cpp:152-160`). The topic's `TopicCards` is untouched while it is stacked:
   its `loop()`, the only path to `openNextQuestion`'s swap (`:231-236`), does not run (R§1).
4. **`onEnter`**: computes geometry (A8) and metrics (line heights from
   `renderer.getLineHeight` per rung), reserves the layout vectors once
   (quote spans, reference spans, page starts), lays out the start verse (A17), requests an update.
5. **`render`**: `clearScreen`; draws the current page's lines (A7) — each span copied into a
   `char[LINE_BUFFER_BYTES]` with bytes below 0x20 drawn as spaces, as the topic screen draws a
   line (`PreachingTopicActivity.cpp:287-295`) and as `wrapText` measured it
   (`PreachingCardLayout.cpp:178-181`); the footer (A2, A8, A9); then `displayWithRefreshCycle` (A14).
6. **`loop`**: A10. A step calls `preaching::show::step`; a verse change re-lays out (A17).
7. **Exit**: `setResult(PreachingResult{at})` with `at.verse = cursor.verse`, `finish()`. The
   manager destroys show mode, restores the topic, unlocks, calls the handler, re-renders
   (`ActivityManager.cpp:104-133`).
8. **Handler** (topic): if the result holds a `PreachingResult` and `layout` exists, take
   `RenderLock`, `cursor = preaching::selectVerse(*layout, at.verse)` (`PreachingCardNav.h`; turns
   to the card's page, out of range → first Verse card), `halfRefreshPending = true` (A15).

## Resource justification

- **No text copy.** `Deck` and the footer title are views into `TopicCards`; the layout holds
  `Span`s (8 bytes each with padding) and page starts. A question is under 5 KB of text and a
  topic's longest passage a few hundred bytes, so a verse is tens of spans.
- **The activity** is one heap object per show, freed by the manager on exit — required by the
  `ActivityManager` ownership model (`Activity.cpp:17-20`). Its three `std::vector` members are
  reserved once in `onEnter` (quote 64 spans, reference 4, pages 8) and reused with `clear()` on
  each verse change, so a step in the usual case does no heap operation. A longer quote grows the
  vector, as `PreachingCardLayout` does for a long topic.
- **Stack.** The largest locals are the `char[LINE_BUFFER_BYTES]` (128 B) line buffer in render
  and a `char[96]` footer buffer — not live together; each under the 256 B rule.
- **No `std::function` added** beyond the existing `startActivityForResult` handler type
  (`ActivityResult.h:114`), which every caller uses. The measure adapter is a plain function
  pointer plus a context struct.
- **Fonts**: built-in, already in flash and registered (A3). Rendering Noto Serif 18 fills the
  glyph cache with larger glyphs while show mode is up; the topic screen beneath uses 12/14
  and UI 10, which `HomeVerse`/`StudySleepScreen` already mix with 18 on other screens.

## Error handling

| What fails | What happens |
|---|---|
| `openShow` with an ordinal out of range | Returns; nothing pushed (no log spam: a tap on a stale rect is the only way) |
| OOM constructing the show screen | `LOG_ERR("PREACH", …)`; `STR_PREACHING_OPEN_FAILED` toast; topic screen stays |
| Start ordinal out of range inside show mode | Clamped to `[0, verseCount)`; a deck with no Verse card finishes at once with the start ordinal (unreachable: `openShow` requires one) |
| A Verse card with an empty quote | Only the reference is drawn, centred |
| A Verse card with an empty reference | Only the quote; no gap |
| Text that fits no line (box narrower than one codepoint) | `wrapText` still takes one codepoint per line (`PreachingCardLayout.cpp:207-215`), A5 still advances one line per page; nothing loops |
| The topic's `layout` is null when the result arrives | Handler does nothing (the topic draws its empty state) |

Nothing here writes to SD, so the storage rules do not apply.

## Testing strategy

**Host, TDD** — `test/ui_layout/PreachingShowLayoutTest.cpp`, joining `PreachingCardsTest`
in `test/ui_layout/CMakeLists.txt` with `src/activities/preaching/PreachingShowLayout.cpp`. Fake
measure as `PreachingCardLayoutTest.cpp`'s `widthByBytes` (10 px per byte), scaled per rung so
rung 0 is widest (e.g. 14/12/10 px per byte), line heights 30/24/20.

- `Deck`: counts Verse cards among mixed kinds; `verseCard(n)` is the nth Verse; out of range →
  nullptr; an all-Verse deck (a tag topic) maps n → n.
- Fit: a short quote takes rung 0; one too tall for 18 takes 14; too tall for 14 takes 12.
- A4: a word too wide for the column at 18 but not at 14 takes 14 even though the height fits at 18.
- A5: a quote too tall for 12 pages at rung 2; the pages' spans, concatenated, reproduce every
  non-break byte of the quote and reference in order (nothing cut); each page's content height ≤
  `height`; the first reference line shares its page with the quote's last line.
- A5 orphan rule: a quote whose lines exactly fill N pages puts its last line with the reference.
- A5 whole reference: a reference that wraps to two lines (narrow column) is never split — the
  quote's last line and both reference lines share the last page, and no page after it holds a
  quote line; only a reference taller than the box alone falls back to line-by-line pages.
- A9: the long-quote fixture reports `pageCount() > 1`; a short quote reports 1.
- A5 degenerate: `height` smaller than one line → one line per page, terminates.
- Empty quote → reference only, one page; empty reference → quote only, no gap counted.
- `step` (A13): next within pages, then next verse page 0; previous within pages, then previous
  verse page 0; false at both ends with the cursor unchanged.

Run: `cmake -S test -B build/test && cmake --build build/test --target PreachingCardsTest &&
ctest --test-dir build/test -R Preaching --output-on-failure`.

**Build and format** — `./bin/bootstrap` once (no `.pio/` yet, R§6); `~/.platformio/penv/bin/pio
run -e x4pro` with no new warnings; `./bin/clang-format-fix` (whole tree) after `git add`.

**On the device** (the owner's, end of batch): tap a quote → the verse at 18; a long tag passage
pages at 12 with its reference on the last page, the footer reading `… · 1/3` → `2/3` → `3/3`;
Left/Right and the side taps step; centre tap and
left-edge swipe return with that card selected and its page shown; Noto Serif 18 legible at arm's
length (D6); free heap equal across topic → show → topic (serial `LOG_DBG("MEM", …)`).

## Changes after spec review 0

Review: `docs/superpowers/reviews/berean-o-17-spec-review-0.md` (CLEAR, 1 MAJOR, 4 MINOR). All accepted.

| Finding | Change |
|---|---|
| M1 — A9's "needs a new string" was false | A9 now shows ` · p/n` in the footer when a quote pages, in the topic screen's untranslated `"%d/%d"` form; non-goal removed; host test and device check added |
| m1 — A11 overstated parity with the reader | A11 states the one difference: with touch controls Off the centre tap still returns here |
| m2 — page-step cursor write outside the lock | A17: every cursor write is under `RenderLock` |
| m3 — `layOut` must not reassign `out` | Pure-module section: `clear()` the three vectors, never `out = Layout{}` |
| m4 — "reference on the last page only" did not follow from A5 | A5 gains a third rule: the reference is never split; tests assert it |
