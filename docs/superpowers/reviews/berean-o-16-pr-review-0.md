Tier: heavy

# PR review 0 — berean-o-16 (T3): Predicación's screens and the reader entry contract

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-screens` at `b73e28eb`.
Read against the brief (`hpipe brief --task t3`), the task spec
`docs/superpowers/specs/2026-10-15-berean-o-16-design.md`, the plan
`docs/superpowers/plans/2026-10-15-berean-o-16-plan.md`, and the parent spec §3, §4, §6, §8 and §9.
The branch has no empty reply commits from the worker.

What I ran myself:

- The `PreachingCardsTest` (22 tests) and `ReaderEntryIntentTest` (11 tests) host suites pass.
- `pio run -e x4pro` succeeds. I touched the changed `.cpp` files to force a recompile, and none of
  them raises a warning.
- `clang-format --dry-run --Werror` reports nothing on any changed source file.
- Every non-trivial line in `src/activities/preaching/*` appears verbatim in the plan, so the code
  does not drift from the plan.

## Intent

**Acceptance criteria — all met.**

- **`PreachingActivity`** (`src/activities/preaching/PreachingActivity.{h,cpp}`) is a
  `UiTabListActivity` with Preguntas and Mis temas.
  - Preguntas shows 20 rows of `"N. <TOC title>"`. It uses the title only when the TOC entry belongs
    to that spine, and otherwise falls back to `STR_PREACHING_QUESTION_N`
    (`PreachingSession.cpp:labelFor`). When fewer than 21 files resolve, the tab shows
    `STR_PREACHING_NO_SECTION` and Mis temas still works.
  - Mis temas lists the active picks in `activeIdsByName` order with digit counts. A count of 0 is
    still shown, and an unavailable `bible.json` gives rows with no value. The "+ Elegir temas" row
    comes last, and an empty pick list shows `STR_PREACHING_NO_TOPICS` above that row.
  - The counts pass runs on the first switch to Mis temas and is held for the rest of the session.
    For a tag origin it is deferred to the topic's result.
  - Selection:
    - From Home, ring 1 is selected.
    - On a return, `moveRingTo` selects the question or tag row.
    - From an origin, `seedSelection` selects the row and the topic is pushed at `origin.verse`.
  - Back is `finish()`. Predicación is the root of the stack, so the Pop branch goes Home
    (`ActivityManager.cpp:110-114`).
- **The `Epub`** is taken through `ParkedBook::take`, or opened fresh as `loadFreshEpub` does, and
  parked in `onExit` (`PreachingSession::close`). The spec's claim that `onExit` runs before the reader
  loads holds:
  1. The Replace branch exits the current activity and every stacked one (`ActivityManager.cpp:144-152`).
  2. Only after that does it call the new activity's `onEnter` (`:161`).
- **`PreachingTopicActivity`**:
  - Layout:
    - The header shows "Pregunta N" or the tag name with a `"%d/%d"` counter.
    - The title is Noto Serif 14 bold. Quotes and text are Noto Serif 12. References are bold,
      right-aligned and carry "›".
    - Lines are measured once. Pages break between cards, and an over-tall card continues on the
      next page.
    - The revisita card is placed last when `nextQuestion != 0`. An empty tag topic shows
      `STR_PREACHING_TOPIC_EMPTY`.
  - Input:
    - Left/Right step through Verse cards and turn the page at an edge. A vertical swipe turns the
      page.
    - Tapping a quote goes to `openShow`. Tapping a reference goes to `openBible`. The revisita card
      swaps the topic in place.
    - Back returns `PreachingResult`.
- **`ReaderEntryIntent`** gains:
  - `Kind::OpenHref`, which carries `char href[64]` and routes to `Route::Href` in the Bible only.
  - `PreachingOrigin`, documented as kept by the reader for its whole life.
  - A `static_assert` that the struct is trivially copyable.

  Question cards open with `OpenHref` after `resolveHrefToSpineIndex`; an href that is refused or does
  not resolve posts `STR_PREACHING_VERSE_NOT_FOUND` and stays on the card. Tag cards open with
  `OpenAt` using the passage's unit and spine hint. Both carry the origin.
- **`goToPreaching(origin)`** follows `goToReader`'s OOM handling and uses `replaceActivity`.
- **Seams:**
  - T4: `PreachingTopicActivity::openShow(int)`.
  - T5: `PreachingActivity::openTopicPicker()`, with `onTopicsChanged()` beside it for T5's result.

  Each one logs and returns.
- **Strings:** all 14 §8 keys plus `STR_PREACHING_VERSES_UNREADABLE` (spec A25) exist in English and
  Spanish, and no comment names an unlanded key.
- **§9 error states:**
  - A question that cannot be read or parsed leaves the list in place with `OPEN_FAILED`.
  - Unreadable verses show `VERSES_UNREADABLE`.
  - OOM goes through `makeUniqueNoThrow`, then `LOG_ERR`, then a toast.
  - A layout failure finishes back to the list with a toast.

**Scope.**

- Nothing outside the brief was built. The two edits outside the declared files are both declared in
  the spec and both minimal:
  - `ActivityResult.h` gains one struct (A16).
  - `EpubReaderActivity.cpp` gains one `case Route::Href` (A21).
- No reader behaviour was added; T7 owns it.
- Nothing was dropped silently. The two deliberate departures are explained in the spec:
  - Headings are drawn verbatim (A15). The markup is already uppercase.
  - The Elegir row uses an ASCII "+" (A19).

**Tests.**

- `PreachingCardLayoutTest` exercises the rules the spec sets with a fixed-width measure:
  - wrapping on U+0020 only;
  - breaking at codepoint boundaries;
  - the 127-byte line cap;
  - the page rules: sharing, continuation, keep-with-next, a reference never alone, and the
    heading-before-over-tall precedence;
  - verse ordinals.
- `PreachingCardNavTest` drives the navigation contract through `verseFirstPage`; it does not restate
  the implementation.
- `ReaderEntryIntentTest` covers `OpenHref` routing in both directions, the 63/64-byte boundary, the
  empty href, and that `out` is left untouched on refusal.
- The activities' glue has no host harness in this repo. The spec routes it to device checks, which is
  honest.

## Quality

- **Patterns.**
  - `PreachingActivity` mirrors `TextSettingsActivity`/`SettingsActivity`: `handleButtons`,
    `onTabAction` resetting to the tab bar, `buildScreen` with `setContentMargin` + `buildTabBar` +
    `syncTabListViewport`, and `valueInset`.
  - `PreachingTopicActivity` is `ClockOffsetActivity`'s shape: it draws with the renderer and
    registers hit-only rects. Raw `frame().hit` regions paint nothing on a tap flash
    (`FreeInkUICore.h:1417-1421`), so this shape is safe.
  - The loop order (route touch, then swipe, then `ButtonNavigator`) matches `UiListActivity::loop`.
  - Loading goes through `SpineHtmlStream` on the loop task and is never under a held `RenderLock`.
    The revisita swap exchanges `unique_ptr`s under the lock.
- **Lifetimes.**
  - `TopicCards` is non-copyable and non-movable, and its cards are built after `parsed`/`passages`
    are final.
  - Row labels point into session strings that outlive the rows. `rowValues` is `reserve`d before its
    `c_str()`s are taken.
- **Error handling** matches the established `LOG_ERR` + `PostedMessage` shape.
- **Code hygiene.** There is no dead code and no commented-out code. Comments are about *why*, apart
  from the drift noted below.

## Findings

No BLOCKER and no MAJOR. Three MINORs, most consequential first:

1. **MINOR — the measured line and the drawn line can differ by a space.**
   - `wrapText` treats `\t`/`\n`/`\r` as break points (`PreachingCardLayout.cpp:8`), but it measures
     the raw bytes (`PreachingCardLayout.cpp:180-184`).
   - `drawLine` replaces every byte below 0x20 with a space before drawing
     (`PreachingTopicActivity.cpp:290-293`).
   - So a line that keeps an embedded control byte between two words is measured with whatever the
     font gives that byte (it has no glyph), but drawn with a space's width. It can overrun the column
     by a few pixels. This is the measure/draw split the repo has already been bitten by
     (`renderer-newline-measure-draw-divergence`).
   - Fix: apply the same mapping to bytes below 0x20 when `fits` fills its scratch buffer.
2. **MINOR — `wrapText` allocates a heap `std::string scratch` on every call**
   (`PreachingCardLayout.cpp:178-179`).
   - That is two allocate/free pairs per Verse card (body and reference) during layout.
   - `fits` caps the run at `LINE_BUFFER_BYTES` (128), so a stack `char[LINE_BUFFER_BYTES]` would do
     the job with no heap traffic. It mirrors `drawLine` and stays under the 256-byte local limit
     (`CLAUDE.md` resource protocol rules 1–2).
3. **MINOR — a data race in the T5 seam, `onTopicsChanged`.**
   - It empties the rows under the lock, then calls `session.reloadTopics()` outside it
     (`PreachingActivity.cpp:210-221`).
   - `reloadTopics` rewrites `activeTopics` and the palette (`PreachingSession.cpp:130-135`).
   - Meanwhile `buildScreen` on the render task reads `session.topics().empty()` (`:172`) whenever Mis
     temas is the active tab, which it always is when T5 calls this.
   - It is a narrow race on a vector's size, but T5 will call this as-is. Fix: hold the `RenderLock`
     across the reload, or have `buildScreen` read a flag that `rebuildRows` sets under the lock.

VERDICT: CLEAR
