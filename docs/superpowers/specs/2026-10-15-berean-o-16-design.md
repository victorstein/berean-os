# Design: Predicación's screens and the reader entry contract (berean-o-16, T3)

Parent design: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` (§3, §4, §6 contract, §8,
§9; D1, D4, D5, D8). Research: `docs/superpowers/research/2026-10-15-berean-o-16-research.md`.
Surface: `ui`. Tier: heavy. Every `file:line` below was read at `6b8a9bd0` (batch tip `aed8879a` plus
the research note).

## Problem

T1 landed the question parser (`lib/Preaching`) and T2 the picked-tags store and the `bible.json`
passes (`src/study/Preaching*`), but nothing on the device uses them. There is no screen that lists
the twenty questions or the owner's topics, no card view of a question, no way for the reader to be
told "open this href and remember you came from Predicación", and no translated string for any of it.
T4–T7 each need a fixed contract from this task before they can start in parallel.

## Goal

1. `PreachingActivity` — tabs **Preguntas** and **Mis temas** — and `PreachingTopicActivity` — a
   question or tag topic as paged cards — work end to end, with every error state of parent §9.
2. The Bible's `Epub` is taken from `ParkedBook` on entry and parked again on exit; the reader takes it.
3. `ReaderEntryIntent` carries `Kind::OpenHref` (`char href[64]`, Bible-only route) and a
   `PreachingOrigin`; question cards open the Bible with `OpenHref`, tag cards with `OpenAt`, both
   carrying the origin.
4. `ActivityManager::goToPreaching(origin)` opens the list, or rebuilds it with the topic pushed at the
   origin's card.
5. Two single, named seams: `PreachingTopicActivity::openShow(int verse)` (T4) and
   `PreachingActivity::openTopicPicker()` (T5).
6. Every `STR_PREACHING*` string in English and Spanish, including the ones T4–T7 will need.

## Non-goals

- Show mode (T4), the Elegir temas picker (T5), the Home card (T6), and the reader honouring
  `OpenHref` and the origin (T7). Until T6 lands nothing on Home opens Predicación; until T7 lands a
  question reference opens the Bible at its last position (A21).
- Any change to `tags.json`, the passage files, `preaching.json`'s format, any cache version, or
  what the reader records as a place (parent non-goals).
- A "Cargando…" line for the counts pass: parent §9 adds it only if the device log shows the pass is
  slow.

## Modelled on

| Piece | Model | What is copied |
|---|---|---|
| `PreachingActivity` | `src/activities/settings/SettingsActivity.{h,cpp}` (and `TextSettingsActivity.cpp:178-181` for Back) | `UiTabListActivity` contract, `buildScreen` with `setContentMargin` + `buildTabBar` + `syncTabListViewport` (`SettingsActivity.cpp:435-469`), `onTabAction` clearing the tap flash (`:160-167`) |
| `PreachingTopicActivity` | `src/activities/settings/ClockOffsetActivity.{h,cpp}` | `Activity, private UiAppHost`; legacy `renderer` draws with `UiAppHost` hit rects (`ClockOffsetActivity.h:14-16`, `.cpp:61-70,180-205,285-292`) |
| Tappable drawn regions | `EpubReaderMenuActivity.cpp:396,480-487` | `screen.frame().hit(rect, action, value, fui::InputTouch)` inside the screen function |
| Pure layout + host test | `src/activities/boot_sleep/StudySleepFit.h`, `test/study_sleep_pick/StudySleepFitTest.cpp`; `ReaderMenuSheetLayout.h` (`test/ui_layout`) | `MeasureFn` function pointer, greedy wrap on U+0020 only, plain ints, Arduino-free |
| `goToPreaching` | `ActivityManager::goToReader` (`ActivityManager.cpp:208-229`) | `makeUniqueNoThrow`, `LOG_ERR` on OOM, `replaceActivity` |
| Opening a fresh `Epub` | `EpubReaderActivity::loadFreshEpub` (`EpubReaderActivity.cpp:190-215`) | popup and framebuffer loan only when `book.bin` is absent |
| Tag card → Bible | `LauncherActivity::openReader` (`LauncherActivity.cpp:404-420`) | `Kind::OpenAt` with `unit` and `spineHint` from the passage |
| Transient errors | `PostedMessage::post` (`src/activities/PostedMessage.h`, used at `SettingsSave.h:18`) | toast drawn after the next render (`UiListActivity.cpp:170`) |
| Page indicator | `TagChipGridActivity.cpp:128` | `"%d/%d"` with `snprintf` |

## Architecture

### Files

| File | New / changed | Contents |
|---|---|---|
| `src/activities/preaching/PreachingOrigin.h` | new | `PreachingOrigin`, Arduino-free |
| `src/activities/preaching/PreachingSession.{h,cpp}` | new | the Bible, the 21 spine indices, question titles, the picked topics and counts; owned by `PreachingActivity` |
| `src/activities/preaching/PreachingContent.{h,cpp}` | new | loads one topic's cards: a question through `SpineHtmlStream` + `QuestionParser`, or a tag through `PreachingPassages::passagesWithTag` |
| `src/activities/preaching/PreachingCardLayout.{h,cpp}` | new | pure: wrap, page breaking, Latin uppercase for headings |
| `src/activities/preaching/PreachingCardNav.h` | new | pure: Left/Right/swipe rules over a layout |
| `src/activities/preaching/PreachingActivity.{h,cpp}` | new | the two tabs |
| `src/activities/preaching/PreachingTopicActivity.{h,cpp}` | new | the cards |
| `src/activities/reader/ReaderEntryIntent.h` | changed | `Kind::OpenHref`, `Route::Href`, `href`, `origin`, `openHref()` |
| `src/activities/reader/EpubReaderActivity.cpp:1104` | one case | `Route::Href` logs and opens normally (A21) |
| `src/activities/ActivityResult.h` | one struct | `PreachingResult` in `ResultVariant` (A16) |
| `src/activities/ActivityManager.{h,cpp}` | changed | `goToPreaching` |
| `lib/I18n/translations/{english,spanish}.yaml` | changed | §Strings |
| `test/ui_layout/ReaderEntryIntentTest.cpp` | changed | `OpenHref` |
| `test/ui_layout/PreachingCardLayoutTest.cpp`, `PreachingCardNavTest.cpp`, `test/ui_layout/CMakeLists.txt` | new / changed | pure logic suites |

### `PreachingOrigin` (contract for T4, T7)

```cpp
// src/activities/preaching/PreachingOrigin.h — no Arduino, no StudyStore
struct PreachingOrigin {
  enum class Kind : uint8_t { None, Question, Tag };
  Kind kind = Kind::None;
  uint8_t question = 0;  // 1..IntroSection::QUESTION_COUNT when kind == Question
  uint16_t tag = 0;      // study::toRaw(TagId) when kind == Tag
  uint16_t verse = 0;    // ordinal among the topic's Verse cards
  static PreachingOrigin ofQuestion(int question, int verse);
  static PreachingOrigin ofTag(uint16_t tag, int verse);
  bool isSet() const;
};
```

- **A1.** The tag is the raw `uint16_t`, not `study::TagId`: `TagId` lives in `TagPalette.h:23`, which
  includes `<ArduinoJson.h>` (`:3`); the intent stays light and trivially copyable (it is copied by
  value at `EpubReaderActivity.cpp:1102`).
- **A2.** "Card index" (parent §6) means the ordinal among `Verse` cards, not among all cards or
  pages. It survives a re-layout, a theme change and a different screen size, and it is the index T4's
  show mode takes and returns (parent §5: "a read-only view of the topic's `Verse` cards and a start
  index").

### `ReaderEntryIntent` (contract for T7)

```cpp
enum class Kind : uint8_t { None, OpenAt, GoTo, Search, Tags, OpenHref };
enum class Route : uint8_t { None, Locate, ChapterGrid, TocList, Search, Highlights, Href };
static constexpr size_t HREF_BYTES = 64;
char href[HREF_BYTES] = {};      // OpenHref only, NUL-terminated, verbatim from the question file
PreachingOrigin origin{};         // any kind; see the header comment below
// False, with `out` untouched, for an empty href or one of HREF_BYTES bytes or more.
static bool openHref(std::string_view href, const PreachingOrigin& origin, ReaderEntryIntent& out);
// route(): case Kind::OpenHref: return isBible ? Route::Href : Route::None;
```

The header comment changes from "consumed once the book has loaded" (`ReaderEntryIntent.h:9-11`) to
say: `kind` and its fields are consumed once; `origin` is **kept by the reader for its whole life**
and must be copied out before `onBookLoaded` clears `entryIntent` (`EpubReaderActivity.cpp:1102-1103`).

- **A3.** An href that does not fit is refused by `openHref`, logged by the caller, and reported to the
  user as `STR_PREACHING_VERSE_NOT_FOUND` — the user cannot tell "too long" from "missing", and both
  mean the card cannot open the Bible. The longest real one is ~44 B (parent §6).
- **A4.** `origin` sits on every intent, not only `OpenHref`, because tag cards open with `OpenAt`
  (parent §6).
- **A21.** A new `Route` makes the `switch` at `EpubReaderActivity.cpp:1104` (no `default`) warn under
  `-Wall`/`-Wswitch` (`scripts/enable_repo_warnings.py:30`). T3 adds `case Route::Href:` that logs
  "not honoured" with `LOG_INF` and returns, as `Route::None` does (`:1105-1110`). T7 replaces it with
  `navigateToHref(href, false)`. The brief allows this widening.

### `PreachingSession` — state shared by the two screens

Owned by `PreachingActivity` as a member; `PreachingTopicActivity` receives a `PreachingSession&`.

| Field | Source |
|---|---|
| `std::string biblePath` | `BibleFinder::find()` after `RECENT_BOOKS.loadFromFile()` (`BibleFinder.h:18-22`, `LauncherActivity.cpp:106,115`) |
| `ParkedBook::Held bible` | `ParkedBook::take(biblePath, SETTINGS.embeddedStyle == 0)` (`ParkedBook.h:23`, `EpubReaderActivity.cpp:218-219`); else a fresh `Epub` as `loadFreshEpub` does, with `nav` null |
| `int spines[21]` | one `resolveFilenamesToSpineIndices` over `IntroSection::BASENAMES` (`Epub.h:93`) |
| `bool sectionFound` | all 21 ≥ 0 |
| `std::string titles[21]` | slots 1..20: `getTocItem(getTocIndexForSpineIndex(spine)).title` through `stripTocPrefix`; empty when the TOC index is < 0 |
| `PreachingTopicsFile topics` + `topicsLoaded` | loaded on first need (Mis temas, or a tag origin) |
| `std::vector<PreachingPassages::TagCount> counts` + `countsStatus` + `countsLoaded` | `countByTag(activeTopics, PsramJsonAllocator::json(), counts)` the first time Mis temas opens (`PreachingPassages.h:43`) |
| `std::vector<study::TagId> activeTopics` | `topics.palette().activeIdsByName()` filtered to `topics.tags()` (`TagPalette.h:61`) |

- **A5.** The 21 basenames become `std::string`s in a `makeUniqueNoThrow<std::string[]>(21)` buffer
  for the one resolve call (the API takes `const std::string*`, `Epub.h:93`): 21 `std::string`s on the
  stack are ~672 B, over the 256-B local limit.
- **A6.** **The topic screen holds a reference to its parent's session.** Lifetime: the topic is only
  ever pushed over its own `PreachingActivity`; a Replace destroys the current activity before the
  stack (`ActivityManager.cpp:145-150`), and a Pop destroys the child before the parent resumes
  (`:103-106`). Parent §5 already prescribes the same shape for show mode (a read-only view of the
  topic's cards). Nothing else gets a pointer into the session.
- **A7.** `onExit` parks `{bible.book, bible.nav}` with `ParkedBook::park(biblePath, …)`
  (`ParkedBook.h:19`). A fresh book parks with a null `nav`; the reader builds it lazily
  (`EpubReaderActivity.cpp:1012-1014`).
- **A8.** No Bible found, or the `Epub` will not load: `sectionFound = false` (Preguntas shows
  `STR_PREACHING_NO_SECTION`), `bible` empty, and a tag card's reference posts
  `STR_PREACHING_VERSE_NOT_FOUND`. Mis temas still lists and counts — counts read `bible.json`, not the
  EPUB. Parent §9 handles "no Bible" at the Home card (T6); this is the fallback.

**Confirmed: the old activity's `onExit` runs before the reader loads.** `goToReader` builds the reader
immediately but its constructor stores only the path and intent (`EpubReaderActivity.h:223-227`);
`replaceActivity` defers (`ActivityManager.cpp:184-196`); the Replace branch runs the topic's
`onExit`/destructor, then each stacked activity's `onExit`/destructor (`:143-150`), and only then the
reader's `onEnter` → `loadBook` → `ParkedBook::take` (`:157-160`, `ReaderActivity.cpp:43-59`). The
reader therefore takes the parked Bible; it does not reopen it. The reverse also holds: the reader
parks in its destructor (`EpubReaderActivity.cpp:165-188`), which runs before `goToPreaching`'s
activity enters.

### `PreachingContent` — one topic's cards

```cpp
struct TopicCards {
  PreachingOrigin::Kind kind;
  int question = 0;              // Question
  study::TagId tag{};            // Tag
  preaching::Question parsed;    // Question: title + blocks, internal heap
  std::vector<PreachingPassages::Passage> passages;  // Tag: texts in PSRAM
  std::vector<preaching::CardInput> cards;           // views into the two above
  uint16_t verseCount = 0;
};
enum class LoadResult : uint8_t { Ok, Unreadable, OutOfMemory, VersesUnavailable };
LoadResult loadQuestion(PreachingSession&, GfxRenderer&, int question, TopicCards& out);
LoadResult loadTag(PreachingSession&, study::TagId, TopicCards& out);
```

- A question: `SpineHtmlStream::stream(bible.book, spines[n], renderer, sink, &parser)` with a static
  trampoline calling `QuestionParser::feed` (`SpineHtmlStream.h:20,40`, `QuestionParser.h:49`), then
  `parser.take(parsed)`. `!parser.valid()` or `outOfMemory()` → `OutOfMemory`; a failed stream or take
  → `Unreadable`. Cards: `Title`, then one card per block in order, then `NextVisit` when
  `nextQuestion(n) != 0` (`IntroSection.h:28-31`).
- A tag: `passagesWithTag(tag, PsramJsonAllocator::passageDoc(), passages)` (`PreachingPassages.h:47`).
  `Missing`/`Unreadable` → `VersesUnavailable`; `OutOfMemory` → `OutOfMemory`. Each passage is a
  `Verse` card (quote = the text, reference = `reference`). No title card, no `NextVisit`.
- **A9.** Loading runs on the loop task only: `stream` takes the render lock itself
  (`SpineHtmlStream.cpp:37`), so it can never run in `render()` or under a held `RenderLock`.
- **A10.** The list loads a topic **before** pushing the topic screen, so a failure leaves the list in
  place with a toast (parent §9). The topic screen loads only for "Para la revisita".

### `PreachingCardLayout` — measure once, page between cards

Arduino-free, renderer-free (`MeasureFn` as in `StudySleepFit.h:15`), host-tested.

```cpp
enum class CardKind : uint8_t { Title, Heading, Verse, Text, NextVisit };
enum class Face : uint8_t { Title, Heading, Quote, Body, Reference, Label, COUNT };
struct CardInput { CardKind kind; std::string_view body; std::string_view reference; };
struct Metrics { int width, pageHeight, cardPadding, cardGap; int lineHeight[Face::COUNT]; };
struct Line { uint16_t card; uint16_t begin; uint16_t length; Face face; bool reference; };
struct Segment { uint16_t card; uint16_t firstLine; uint16_t lineCount; int16_t y; int16_t height;
                 bool opensCard; bool closesCard; };
struct Layout { std::vector<Line> lines; std::vector<Segment> segments; std::vector<uint16_t> pageFirstSegment; };
bool layoutCards(const CardInput*, size_t count, const Metrics&, MeasureFn, const void* ctx, Layout& out);
```

Faces (A11): Title = Noto Serif 14 bold; Heading = UI 10 bold, uppercased; Quote = Noto Serif 12
italic (Home's verse face, `HomeVerse.h:24-26`); Body (`Text`) = Noto Serif 12 regular; Reference =
Noto Serif 12 bold, right-aligned, prefixed `"› "`; Label (the "Para la revisita" caption) = UI 10
bold. Font ids from `src/fontIds.h:4-5,12`; widths from `UITheme` metrics (`contentSidePadding`,
`verticalSpacing`), never 800/480.

Rules:

- **Wrap** greedily on U+0020 only, as `StudySleepFit` and `GfxRenderer::wrappedText` do
  (`StudySleepFit.h:43-56,67-88`): U+00A0 and U+202F in the publisher's markup keep their words
  together. Lines are byte spans into the card's own strings — no copies.
- **A12.** A word wider than the column is broken at the last whole UTF-8 codepoint that fits and
  continues on the next line. It is never cut; this differs from `StudySleepFit`, which refuses
  (`:84`) because it may choose another size.
- **A13.** A line is at most 127 bytes, so the draw path copies it into a `char[128]` on the stack
  (NUL-terminated for `drawText`). A longer run is broken like A12.
- **Pages.** Cards are placed in order with `cardGap` between them. A card that fits the rest of the
  page goes there; one that does not but fits an empty page starts the next page; one taller than a
  page starts a new page (unless the current one is empty) and continues line by line on the
  following pages. Nothing is dropped.
- **A14.** Keep-with-next: a `Heading` or the `Title` never ends a page; it moves to the next page with
  the card after it. A `Verse` card's reference line never starts a page alone: it carries at least
  its last quote line with it.
- **A15.** Headings are uppercased by a small Latin rule (ASCII a–z and U+00E0–U+00FE except U+00F7,
  minus 0x20), enough for Spanish and English; other codepoints pass through. No uppercase helper
  exists in `lib/Utf8` (`grep -n "upper\|Upper" lib/Utf8/*.h` is empty). The uppercased copy is a
  `std::string` per heading held beside the cards (three questions have headings, Q19 ten).
- Layout runs once per topic on the loop task: in `PreachingTopicActivity::onEnter` and after "Para la
  revisita" swaps the content. Measuring on the loop task in `onEnter` follows
  `LauncherActivity::computeLayout` (`LauncherActivity.cpp:77-103`). The revisita swap installs the
  new cards and layout under `RenderLock lock(*this)`, as `moveSelectionTo` guards nav state
  (`UiListActivity.cpp:72-81`).

### `PreachingCardNav` — Left/Right and swipes

State: `page`, `selected` (Verse ordinal, −1 for none). Pure functions over a `Layout`.

- **Right**: the next `Verse` card that begins on the current page after `selected` → select it.
  Otherwise, if there is a next page → turn to it and select the first `Verse` card that begins on it
  (−1 when none). Otherwise nothing.
- **Left**: the mirror — previous `Verse` beginning on this page, else the previous page selecting the
  last `Verse` that begins on it.
- **Swipe up / down**: next / previous page, selecting the first `Verse` that begins on it (−1 if none).
- **Select(verse)**: `page` = the page where that card begins; out of range → page 0, first verse.
- **A17.** Headings, `Text`, the title and "Para la revisita" are not selectable (parent §4). Left/Right
  still reach every page, so prose-only Q19 is readable with buttons, and the revisita card is reached
  by paging and opened by touch — the device has no Confirm button (`CLAUDE.md` input hardware).
- **A18.** A selected card that continues onto the next page is drawn selected on every page it spans.

### `PreachingActivity`

`final : public UiTabListActivity`; constructor takes a `PreachingOrigin` (default none).

- `onEnter`: open the session (Bible, spines, titles), then `UiTabListActivity::onEnter()`
  (`UiTabListActivity.cpp:16-22`), then seed the selection — after the base, because it resets
  `activeNav()` (`UiListActivity.cpp:20-27`):
  - no origin → Preguntas, ring 1 (the first row), or ring 0 when the tab has no rows;
  - a question origin → Preguntas, ring `n`;
  - a tag origin → Mis temas (loading topics and counts), the tag's row if it is still an active pick,
    else ring 1.
  Then, with an origin, load the topic (`PreachingContent`) and push `PreachingTopicActivity`
  selecting `origin.verse`. A failed load posts `STR_PREACHING_OPEN_FAILED` and stays on the list.
- `onExit`: park the Bible (A7), then `UiTabListActivity`'s exit.
- **Preguntas**: `sectionFound` → 20 rows, label `"%d. %s"` from `titles[n]`, or
  `tr(STR_PREACHING_QUESTION_N)` when the title is empty. Not found → `listCount() == 0` and
  `screen.centeredText(tr(STR_PREACHING_NO_SECTION), …)` as `HighlightsActivity.cpp:623` does.
- **Mis temas**: one row per active pick — label the tag name (`palette().name(id)`, `TagPalette.h:53`),
  `value` the count when `countsStatus` is `Ok`, else no value — then the Elegir row. Nothing picked →
  `STR_PREACHING_NO_TOPICS` drawn as a line above the list, which holds only the Elegir row.
- **A19.** The Elegir row's label is `"+ "` then `tr(STR_PREACHING_CHOOSE_TOPICS)`: an ASCII plus,
  because the built-in UI font's coverage of U+FF0B is not established. The row prefix and the
  `"%d. "` number prefix are symbols, not translatable text.
- **A20.** Counts are digits only in the `value` slot (≤ 5 characters), so the slot cannot starve the
  label (`highlights-row-value-slot-width-steal` memory; `SettingsActivity.cpp:459` sets `valueInset`).
- Tabs: `onTabAction` / `stepTab` switch tabs (wrap over two), seed ring 0 on a tab tap and keep each
  tab's own `ListNav`, as `SettingsActivity::onTabAction` does (`:160-167`). The first switch to Mis
  temas runs `topics.load()` and `countByTag` on the loop task (A9), and the result is held until exit.
- `activateIndex`: a question row → load and push; a tag row → load and push; the Elegir row →
  `openTopicPicker()`.
- `handleButtons` (pure in the base): Back (the left-edge swipe, `MappedInputManager.cpp:222-223,257`)
  → `finish()`; Predicación is the stack's root, so the pop goes Home (`ActivityManager.cpp:109-113`).
  This is Text Settings' one-stage Back (`TextSettingsActivity.cpp:179-180`), not Settings' two-stage
  one (`SettingsActivity.cpp:222-229`); parent §3 says the swipe goes Home. The Home key is handled
  by `ActivityManager::loop` (`:75-81`). Left/Right walk the ring through the base
  (`UiTabListActivity.cpp:64-72`).
- Result handler of the pushed topic: read `PreachingResult`, switch to that tab, select that row
  (the question the topic **ended** on, which "Para la revisita" may have changed), request an update.
- **Seam (T5)**: `void openTopicPicker();` — logs `"Elegir temas: not built yet"` and returns. Beside
  it, `void onTopicsChanged();` reloads `topics`, drops the held counts and recounts, so T5's result
  handler is one call.

### `PreachingTopicActivity`

`final : public Activity, private UiAppHost` (ClockOffsetActivity's shape). Constructed with the
session, a `std::unique_ptr<TopicCards>` (the list loaded it) and a start Verse ordinal.

- `onEnter`: lay out (layout `Metrics` from the body rect below `GUI.drawHeader` and above
  `metrics.buttonHintsHeight`, as `SettingsActivity.cpp:437-438` sets its content margin), `Select`
  the start verse, `resetUi`, register actions, `setScreen`, `requestUpdate` (`ClockOffsetActivity.cpp:61-70`).
- `render`: `clearScreen`; `GUI.drawHeader` with the title — `tr(STR_PREACHING_QUESTION_N)` formatted
  with the number, or the tag name — and the page indicator `"%d/%d"` as its trailing label
  (`SettingsActivity.cpp:483-484` passes a trailing label); `renderUi()`; `displayBuffer(FAST_REFRESH)`
  (HALF once after entry, as `UiListActivity::render` uses `halfRefreshPending`, `UiListActivity.cpp:169`);
  `PostedMessage::drawNext` (`:170`).
- The screen function draws the current page's segments with `renderer` (card border with
  `drawRoundedRect`, thicker when selected, as the menu tiles do, `EpubReaderMenuActivity.cpp:397-398`)
  and registers hits: `ACTION_QUOTE` over a Verse segment's quote lines, `ACTION_REFERENCE` over its
  reference line, `ACTION_NEXT_VISIT` over the revisita card — `value` = Verse ordinal.
  `UiAppHost::MAX_INTERACTIONS` is 96 (`UiAppHost.h:35`); a page holds a handful of cards.
- `loop`: Back → `finish()` with `PreachingResult`; touch routed through `routeTouch`; vertical swipe
  (`mappedInput.wasSwipe()`, `MappedInputManager.h:67`) → page; `ButtonNavigator` next/previous
  release → `PreachingCardNav` Right/Left. Any change → `requestUpdate()`.
- `ACTION_QUOTE` → **seam (T4)** `void openShow(int verse);` — logs and returns; T4 pushes show mode
  and selects the verse it returns (A2).
- `ACTION_REFERENCE` → `openBible(verse)`:
  - question card: href empty, `openHref` refuses it (A3), or
    `bible.book->resolveHrefToSpineIndex(href) < 0` (`Epub.h:88`) → `LOG_ERR`,
    `PostedMessage::post(tr(STR_PREACHING_VERSE_NOT_FOUND))`, stay. Else
    `activityManager.goToReader(biblePath, false, intent)` with
    `origin = ofQuestion(n, verse)`.
  - tag card: no Bible → the same toast; else `ReaderEntryIntent::of(Kind::OpenAt)` with
    `unit = passage.start`, `spineHint = passage.spine` (`LauncherActivity.cpp:409-410`) and
    `origin = ofTag(raw, verse)`.
  - **A22.** `allowFastInitialRefresh = false`: Home passes its route's flag, whose default is `false`
    (`HomeTargets.h:42`); a UI screen into the reader takes the clean refresh.
  - **A23.** `resolveHrefToSpineIndex` walks the whole spine on a miss (`Epub.cpp:949-974`). It runs once
    per tap, not at layout.
- `ACTION_NEXT_VISIT` → `loadQuestion(next)`; on success swap the cards and layout under the render
  lock, select verse 0 on page 0, and remember the question for the result; on failure post
  `STR_PREACHING_OPEN_FAILED` and stay. Back still returns to the list (the topic was replaced in
  place, not pushed).

### `PreachingResult` and `goToPreaching`

- **A16.** `struct PreachingResult { PreachingOrigin at; };` joins `ResultVariant`
  (`ActivityResult.h:90-92`), as `TagSelectionResult` did (`:86-88`). The topic returns the question or
  tag and verse it ended on; T4's show mode returns its verse the same way. This edits
  `ActivityResult.h`, outside T3's declared files: one struct and one variant member, needed by T3 and
  T4 only, and no sibling runs while T3 implements. The alternatives — encoding a selection in
  `PageResult`/`IntervalResult`, or a back-pointer into the parent — hide the meaning or invent a
  pattern.
- `ActivityManager::goToPreaching(const PreachingOrigin& origin = {})`:
  `makeUniqueNoThrow<PreachingActivity>(renderer, mappedInput, origin)`; null → `LOG_ERR("ACT", "OOM:
  preaching activity")` and return (the caller stays where it is); else `replaceActivity`. Its
  `onEnter`'s push runs in the same `loop()` pass (`ActivityManager.cpp:159-163`).

## Data and control flow

```
Home card (T6) ── goToPreaching() ──► PreachingActivity.onEnter
                                          ├─ BibleFinder::find → ParkedBook::take | fresh Epub
                                          ├─ resolveFilenamesToSpineIndices(21) → titles from TOC
                                          └─ select row 1
   tap question n ─► loadQuestion(n) ─► push PreachingTopicActivity(cards, verse 0)
   tap tag t      ─► loadTag(t)      ─► push PreachingTopicActivity(cards, verse 0)
   Mis temas first open ─► topics.load() + countByTag (held until exit)

PreachingTopicActivity
   Back ─► finish(PreachingResult{question|tag, verse}) ─► list selects that row
   tap reference ─► goToReader(biblePath, false, OpenHref|OpenAt + origin)
                    Replace: topic.onExit → list.onExit (parks Epub) → reader.onEnter (takes Epub)
Reader (T7): swipe with empty returnStack and origin ─► goToPreaching(origin)
                    Replace: reader dtor parks Epub → PreachingActivity.onEnter (takes Epub)
                    → select origin's row → load topic → push topic at origin.verse
```

## Error handling (parent §9)

| What fails | Detection | What the user sees |
|---|---|---|
| No Bible / `Epub` will not load | `find()` empty; `load()` false | Preguntas: `STR_PREACHING_NO_SECTION`; Mis temas works; a tag card's reference: `STR_PREACHING_VERSE_NOT_FOUND` (A8) |
| Section missing (fewer than 21) | any spine `-1` | Preguntas: `STR_PREACHING_NO_SECTION`; Mis temas works |
| A question will not read or parse | `LoadResult::Unreadable` | list (or current topic) stays; toast `STR_PREACHING_OPEN_FAILED` |
| `bible.json` missing or unreadable | `countByTag` / `passagesWithTag` not `Ok` | Mis temas rows without counts; opening a tag: toast `STR_PREACHING_VERSES_UNREADABLE`, list stays; nothing written |
| Tag with no verses | `passages.empty()` | the topic opens with `STR_PREACHING_TOPIC_EMPTY` centred |
| `preaching.json` / `tags.json` corrupt or newer | `PreachingTopicsFile::load` latches `saveRefused()` | the list shows what loaded (possibly nothing); T5 reports on save |
| Href too long or unresolved | `openHref` false; `resolveHrefToSpineIndex < 0` | toast `STR_PREACHING_VERSE_NOT_FOUND`; stay on the card |
| OOM anywhere | `makeUniqueNoThrow` null, `outOfMemory()`, `LoadResult::OutOfMemory` | `LOG_ERR`, toast `STR_PREACHING_OPEN_FAILED`; never abort |
| `goToPreaching` OOM | null activity | `LOG_ERR`; the caller's screen stays |

- **A24.** An OOM toast reuses `STR_PREACHING_OPEN_FAILED` rather than a separate "out of memory"
  string: the user's action is the same (try again, or go back).
- **A25.** A topic that is not a question and whose verses cannot be read gets its own string,
  `STR_PREACHING_VERSES_UNREADABLE`, because parent §9 says "a topic says it cannot read the verses",
  which `STR_PREACHING_TOPIC_EMPTY` ("no verses carry this tag") would misstate.

## Memory

| Item | Size | Where | Lifetime |
|---|---|---|---|
| The Bible `Epub` | the parked one | as allocated by `Epub` | taken in `onEnter`, parked in `onExit`; never a second copy |
| Basename strings for the resolve | 21 × ~17 B | heap, `makeUniqueNoThrow` | the resolve call |
| Question titles | 20 × < 100 B | internal heap | list lifetime |
| Picked topics, palette, counts | palette as `tags.json` loads it; 4 B per count | internal heap | list lifetime, from first Mis temas open |
| A question's cards | < 5 KB strings (parent §1) + spans | internal heap (each string < 4,096 B, under the PSRAM threshold) | topic lifetime |
| A tag's passages | ~15 KB for fifty (parent §4) | texts in PSRAM via `PsramJsonAllocator` | topic lifetime |
| Layout | 8 B per line, 12 B per segment | internal heap, `reserve`d from a line estimate | topic lifetime |
| `ReaderEntryIntent` | +64 B href, +6 B origin | by value | as today |
| Draw scratch | `char[128]` | render-task stack | one line |

No new task, no new SD writes. Every allocation is `makeUniqueNoThrow` or a `reserve`d vector, with
a `LOG_ERR` on failure.

## Strings (§8 plus what T4–T7 need)

| Key | English | Spanish | Used by |
|---|---|---|---|
| `STR_PREACHING` | Preaching | Predicación | T6 card, T5 |
| `STR_PREACHING_SUBTITLE` | Bible questions · your topics | Preguntas de la Biblia · tus temas | T6 |
| `STR_PREACHING_QUESTIONS` | Questions | Preguntas | tab |
| `STR_PREACHING_MY_TOPICS` | My topics | Mis temas | tab |
| `STR_PREACHING_CHOOSE_TOPICS` | Choose topics | Elegir temas | row, T5 title |
| `STR_PREACHING_QUESTION_N` | Question %d | Pregunta %d | rows, header |
| `STR_PREACHING_NEXT_VISIT` | For the return visit | Para la revisita | card |
| `STR_PREACHING_NO_SECTION` | This Bible doesn't have the preaching questions | Esta Biblia no tiene las preguntas para predicar | Preguntas |
| `STR_PREACHING_NO_TOPICS` | No topics yet. Choose tags to preach from. | Aún no hay temas. Elige etiquetas para predicar. | Mis temas |
| `STR_PREACHING_TOPIC_EMPTY` | No Bible verses have this tag | Ningún versículo de la Biblia tiene esta etiqueta | topic |
| `STR_PREACHING_VERSES_UNREADABLE` | Can't read your tagged verses | No se pueden leer tus versículos etiquetados | A25 |
| `STR_PREACHING_SHOW_POSITION` | %d / %d · %s | %d / %d · %s | T4 footer |
| `STR_PREACHING_OPEN_FAILED` | Couldn't open this topic | No se pudo abrir este tema | §9 |
| `STR_PREACHING_VERSE_NOT_FOUND` | Verse not found in this Bible | No se encontró el versículo en esta Biblia | §9 |
| `STR_PREACHING_SAVE_FAILED` | Couldn't save your topics | No se pudieron guardar tus temas | T5 |

Other languages fall back to English. After editing, `python scripts/gen_i18n.py lib/I18n/translations
lib/I18n/`. No comment names an unlanded `STR_` key (`gen-i18n-scans-comments-for-str-keys` memory).

## Testing

**Host (TDD, failing test first):**

- `test/ui_layout/ReaderEntryIntentTest.cpp` (existing target, `test/ui_layout/CMakeLists.txt:85-99`):
  - `route(OpenHref, true) == Route::Href`, `route(OpenHref, false) == Route::None`;
  - `openHref` accepts a 63-byte href and copies it NUL-terminated with the origin;
  - refuses a 64-byte href and an empty one, leaving `out` untouched;
  - a default intent has `origin.kind == None`;
  - `static_assert(std::is_trivially_copyable_v<ReaderEntryIntent>)`.
- `test/ui_layout/PreachingCardLayoutTest.cpp` (new executable in `test/ui_layout/CMakeLists.txt`, not the
  shared `test/CMakeLists.txt`), with a fixed-width fake `MeasureFn`:
  - wraps on U+0020 only; U+00A0 keeps words together;
  - an over-wide word breaks at a codepoint boundary, never mid-sequence (A12); no line over 127 B (A13);
  - cards that fit share a page; a card that does not fit starts the next page;
  - a card taller than a page continues across pages and every line appears exactly once;
  - a heading never ends a page; a reference line never starts one alone (A14);
  - the revisita card lands after the last block; an empty input yields one empty page;
  - uppercase: "Profecía" → "PROFECÍA", "ñ" → "Ñ", non-Latin passes through (A15).
- `test/ui_layout/PreachingCardNavTest.cpp`: Right/Left within a page, across pages, past prose-only
  pages, at both ends; swipes; `Select` of an out-of-range verse.

The PreachingActivity/PreachingTopicActivity glue (SD, Epub, renderer) has no host harness in this
repo; it is covered by the device checks below. Every host suite touched passes; `pio run -e x4pro`
builds with no new warnings; `./bin/clang-format-fix` (whole tree) is clean.

**On the device (the owner; flagged in the hand-off):**

- From a temporary entry (until T6) or the reader origin path (after T7): Preguntas lists 20 titles in
  the Bible's language; Mis temas shows picks with counts.
- A question pages without cutting text; Q6's headings, Q19's prose, the revisita card.
- A reference opens the Bible; serial shows `PARK: Reusing the parked book` (`ParkedBook.cpp:63`),
  not a fresh load.
- Free heap (`LOG_DBG("MEM", …)`) equal across Predicación → topic → back → Home.
- The `PREACH: Counts: … ms` line (`PreachingPassages.cpp:185`) for the counts pass.
