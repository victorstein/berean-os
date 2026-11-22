# Design: the Tarjetas tab in Predicación (berean-o-26)

Parent design: `docs/superpowers/specs/2026-11-22-preaching-cards-design.md` §4–5. Parent plan:
`docs/superpowers/plans/2026-11-22-preaching-cards.md`, Tasks 6–9. Research:
`docs/superpowers/research/2026-11-22-berean-o-26-research.md`.

In code a printed card is a **handout** (`PreachingOrigin::Kind::Handout`, `loadHandout`,
`Tab::Handouts`): "card" already names a layout card (`PreachingCardLayout.h`). The UI string is
"Tarjetas" / "Cards".

## Problem

The owner's printed preaching cards now have a device-side reader — `PreachingHandouts::summaries`
and `::load` over `/preaching-cards.json` (`src/study/PreachingHandouts.h:51-57`) — but nothing in
Predicación calls it. Predicación has two tabs, Preguntas and Mis temas
(`PreachingActivity.h:24`), and opens on Preguntas (`PreachingActivity.h:60`).

## Goal

1. Predicación gains **Tarjetas** as its first tab, open by default, one row per handout with its
   verse count.
2. A row opens the handout in the existing card screen (`PreachingTopicActivity`): a Heading card
   per non-empty section heading, then its Verse cards. Show mode, the reference tap into the
   Bible, and the reader's swipe back to the same card all work as for a question or a tag.
3. Left/Right on the tab bar cycle all three tabs in both directions.
4. A Question or Tag origin still reopens on its own tab.

## Non-goals

- `src/study/PreachingHandouts.*` (the previous task), the reader (`src/activities/reader/`, read
  only), the Home card, the generator script.
- Writing `/preaching-cards.json`, editing handouts on the device, or remembering which handout was
  last open across Predicación sessions.
- A "Para la revisita" (NextVisit) card for a handout.
- Reloading the list while Predicación is open; a re-uploaded file shows on the next entry.

## Model

The Mis temas tab is the model for every piece (`Kind::Tag`): `PreachingOrigin::ofTag`
(`PreachingOrigin.h:23-29`), `PreachingContent::loadTag` (`PreachingContent.cpp:79-103`),
`PreachingSession::ensureCounts`'s load-once read with a held status (`PreachingSession.cpp:143-153`,
`PreachingSession.h:51-54,67-69`), and the Tag branches in `PreachingActivity.cpp`
(`:42-47,57,116-118,319-323`) and `PreachingTopicActivity.cpp:165`. Each handout change below is that
code's sibling. No new pattern is introduced.

## Architecture

| File | Change |
|---|---|
| `lib/I18n/translations/english.yaml`, `spanish.yaml` | three keys after `STR_PREACHING_SAVE_FAILED` (`english.yaml:498`, `spanish.yaml:445`) |
| `src/activities/preaching/PreachingOrigin.h` | `Kind::Handout`, `uint8_t handout`, `ofHandout(handout, verse)` |
| `src/activities/preaching/PreachingContent.{h,cpp}` | `TopicCards` holds `handoutIndex` and a `PreachingHandouts::Handout`; `LoadResult::HandoutsUnavailable`; `loadHandout(index, out)`; `failureMessage` maps the new result |
| `src/activities/preaching/PreachingTopicActivity.cpp` | `originAt` maps a handout; `openBible` gives a handout verse the href path; two `static_assert`s |
| `src/activities/preaching/PreachingSession.{h,cpp}` | `ensureHandouts()`, `handoutsStatus()`, `handouts()` |
| `src/activities/preaching/PreachingActivity.{h,cpp}` | `Tab::Handouts` first and default; label, rows, notice, selection, open, return, three-way `stepTab` |
| `test/ui_layout/ReaderEntryIntentTest.cpp` | failing-first tests for `ofHandout` and its trip through `openHref` |
| `USER_GUIDE.md` §11 | "two tabs" becomes three, with a Tarjetas bullet (A12) |

The code for each is plan Tasks 6–8, verbatim except where this spec says otherwise.

## Data flow

```
/preaching-cards.json
  ├─ PreachingHandouts::summaries(PsramJsonAllocator::json())       → session.handoutSummaries
  │     (once per PreachingActivity, the first time Tarjetas shows)   → rowItems / rowValues
  └─ PreachingHandouts::load(index, PsramJsonAllocator::passageDoc()) → TopicCards::handout
        (each time a handout is opened)                               → TopicCards::cards / verseSource
```

- **Summaries.** `PreachingSession::ensureHandouts` reads them once, keeps the `Status`, and holds
  `std::vector<Summary>` until Predicación exits — the shape of `ensureCounts`
  (`PreachingSession.cpp:143-153`). Rows point at `Summary::title.c_str()` and at a per-row
  `std::string` in `rowValues`, both reserved before the push loops, both stable while the
  `RenderLock` held by the caller of `rebuildRows` is held (`PreachingActivity.h:47`).
- **One handout.** `loadHandout` fills `TopicCards::handout` (titles and headings in internal heap,
  verse texts in PSRAM through `passageDoc().text`, `PreachingHandouts.h:38-49,57`), then builds
  `cards` as views into it: for each verse, when its `section` differs from the previous verse's,
  a Heading card for `headings[section]` if non-empty; then a Verse card (`text.view()`,
  `reference`). `verseSource[ordinal]` is the verse's index in `handout.verses`. The views are safe
  because `TopicCards` is never copied or moved (`PreachingContent.h:17-35`).
- **Bounds.** Every `Verse::section` names a heading already pushed (`PreachingHandouts.cpp:142-144`);
  `summaries` and `load` skip the same unopenable hrefs (`:105`, `:146-150`), so a row's count is the
  number of Verse cards.

## Control flow

**Entry** (`PreachingActivity::onEnter`, today `:39-63`):

1. `tab` starts at `Handouts`. A Tag origin sets `Topics` (unchanged, `:42-47`); a Question origin
   now sets `Questions` explicitly — today it relies on the default (A3).
2. If `tab == Handouts`, `session.ensureHandouts()` before the first `rebuildRows`.
3. `seedSelection` selects `origin.handout`'s row for a Handout origin, as `origin.question - 1`
   is selected for a Question (`:114-115`).
4. After `seedSelection`, a Handout origin calls `openHandout(origin.handout, origin.verse)`, as a
   Question origin calls `openQuestion` (`:55-56`).

**Tabs.** `tabLabel` becomes a switch over three values. `showTab` calls `ensureHandouts` when
moving to Tarjetas, as it calls `ensureCounts` for Mis temas (`:126`). `onTabAction` already jumps
to any index (`:132-139`) and needs no change. `stepTab(direction)` (today `:141-148`, ignoring its
argument) steps `+1` or `-1` modulo `Tab::Count`; `UiTabListActivity.cpp:70-71` passes `+1`/`-1`,
and Confirm on the tab bar passes `+1` (`PreachingActivity.cpp:156-157`).

**Open.** `activateIndex` on Tarjetas → `openHandout(index, NO_VERSE)` → `makeUniqueNoThrow<TopicCards>`
→ `loadHandout` → `pushTopic` (`:300-309`); the twin of `openTag` (`:285-298`).

**Reference tap.** `PreachingTopicActivity::openBible` (today `:202-232`) takes an `href` pointer:
the question block's or the handout verse's. Either goes through `ReaderEntryIntent::openHref`
and `resolveHrefToSpineIndex`, today's question code; only a tag passage keeps the `OpenAt`
branch. Without this, a handout falls into the `else` and indexes an empty `passages`.

**Back from the card screen.** `originAt` returns `ofHandout(handoutIndex, verse)`;
`onTopicResult` (today `:311-324`) shows Tarjetas and moves the ring to that row.

**Back from the reader.** `openHref` copies the origin into the intent (`ReaderEntryIntent.h:55`);
the reader's swipe calls `goToPreaching(preachingOrigin)` when the origin is set
(`EpubReaderActivity.cpp:706,711-713`), which builds a new `PreachingActivity` with it
(`ActivityManager.cpp:232-233`). Entry steps 1–4 then reopen the handout at that verse; the card
screen's `selectVerse` places the cursor (`PreachingCardNav.h:40-45`). The reader never reads
`kind`, so it needs no change.

## Error handling

| Case | Seen by the owner | Mechanism |
|---|---|---|
| No file | Tarjetas: `STR_PREACHING_NO_CARDS` notice; other tabs work | `Status::Missing` |
| File valid, zero handouts | same `STR_PREACHING_NO_CARDS` notice | `Status::Ok`, empty list (A6) |
| Newer, malformed, oversized | Tarjetas: `STR_PREACHING_CARDS_UNREADABLE` notice; nothing written | `TooNew` / `Unreadable` |
| OOM reading summaries | same unreadable notice | `OutOfMemory` (A7) |
| Open fails: file changed, gone, unreadable | toast `STR_PREACHING_CARDS_UNREADABLE`; list stays | `LoadResult::HandoutsUnavailable` |
| Open fails: OOM | toast `STR_PREACHING_OPEN_FAILED` | `makeUniqueNoThrow` null or `LoadResult::OutOfMemory`; `LOG_ERR` |
| Handout with zero verses | card screen says `STR_PREACHING_TOPIC_EMPTY` | existing branch, `PreachingTopicActivity.cpp:275-276` |
| Reference not in this Bible | toast `STR_PREACHING_VERSE_NOT_FOUND`; stays on the card | existing question code, `:217-222` |
| Return to a handout index past the end | list on Tarjetas with the unreadable toast | `load` returns `Unreadable` for a past-the-end index (`PreachingHandouts.cpp:129-131`) |
| Return to a verse past the end | first Verse card | `selectVerse` clamps (`PreachingCardNav.h:42-43`) |

Nothing in this change writes to SD; the storage rules for stores do not apply.

## Assumptions

- **A1 — First and default.** `Tab::Handouts` is enum value 0 and `tab`'s initial value. *Source:*
  parent design D4.
- **A2 — Rows.** Label = `Summary::title`, value = `Summary::verses` as a decimal string, always
  shown (also `0`). Two-line labels, as today (`PreachingActivity.cpp:192-193`). No "+ choose" row.
- **A3 — Origins pick their tab.** None and Handout → Tarjetas; Question → Preguntas; Tag → Mis
  temas. Question needs a new explicit assignment.
- **A4 — Origin keys on the index.** A handout is identified by its position in the file, not its
  title. If the file is reordered between leaving for the Bible and swiping back, the return opens
  whatever handout now sits at that index. Accepted: the file changes only by a manual re-upload
  over File Transfer (Settings → System), and reaching Settings from the reader goes through Home,
  which drops the origin; the origin is RAM-only (`PreachingOrigin.h:6`).
- **A5 — `uint8_t` index.** `MAX_HANDOUTS = 200` (`PreachingHandouts.h:28`) fits;
  `static_assert(MAX_HANDOUTS - 1 <= UINT8_MAX)` keeps it so, and
  `static_assert(MAX_HREF_BYTES < ReaderEntryIntent::HREF_BYTES)` keeps every loaded href openable.
- **A6 — An empty valid file reads as "no cards yet".** The notice for `Ok` with no rows is
  `STR_PREACHING_NO_CARDS`, the same as no file.
- **A7 — OOM on the list reads as "can't read the file".** Only three cases are distinguished on
  screen (none, unreadable, the list); an OOM there is rare (the list is under 1 KB for eight
  handouts) and is logged by the reader. Not retried until the next entry.
- **A8 — Notice layout.** The Tarjetas notice is a top-of-list, centred, up-to-3-line text block
  (the English no-cards string runs longer than Mis temas' two-line one), modelled on the Mis temas
  notice (`PreachingActivity.cpp:176-184`); `buildScreen` returns after it, since there are no rows.
- **A9 — Summaries read once per PreachingActivity.** Not re-read on tab switches; a return from
  the reader builds a new activity and so reads them again, then `load`s the handout — two filtered
  parses of a file capped at 128 KiB.
- **A10 — Stepping.** `+1` forwards, any negative direction backwards, both wrapping. Confirm on
  the tab bar keeps stepping forwards.
- **A11 — Headings.** A Heading card is emitted on each change of section and only for a non-empty
  heading; a single-article handout (one empty heading) gets none. No Title card: the handout title
  is the screen header (`out.header = handout.title`), as a tag's name is (`PreachingContent.cpp:94`).
- **A12 — USER_GUIDE.** `USER_GUIDE.md:335` says Predicación opens "with two tabs"; this change
  makes that false, so §11 gets a Tarjetas bullet and "three tabs" in the same change. The plan
  omits it; the doc edit is the smallest way to keep the shipped guide true.
- **A13 — Strings in this change.** `.claude/agents/ui-dev.md` lists the translation YAMLs as
  report-don't-edit shared files, but the brief assigns the three keys to this task and no sibling
  in the batch adds `STR_PREACHING_*` keys; the edit stays here. Spanish uses File Transfer's own
  label, "Transferir archivos" (`spanish.yaml:12`).

## Testing strategy

- **Failing first (host).** In `test/ui_layout/ReaderEntryIntentTest.cpp`, beside
  `OriginsKeepTheirTopicAndVerse` (`:80-86`): `ofHandout(7, 4)` has `kind == Handout`,
  `handout == 7`, `verse == 4`; `ofHandout(0, -1).verse == 0`; and `openHref` with a handout origin
  copies kind, handout and verse into the intent, as the Question test does (`:57-62`). It fails to
  compile until `ofHandout` exists. The suite builds `PreachingOrigin` already
  (`test/ui_layout/CMakeLists.txt:85-99`).
- **No host seam** for `PreachingActivity`, `PreachingContent` or `PreachingTopicActivity`: none is
  built by a host suite. The `stepTab` arithmetic, rows, notices and returns are covered by the
  builds and the owner's device checklist (plan, "Device checklist"). Extracting them into a
  testable helper would be a new pattern, which this change does not add.
- **Reader data** is already covered by `test/storage_io/PreachingHandoutsTest.cpp`.
- **Whole-tree gates** (plan Task 9): all host tests pass; `pio run -e x4pro` and
  `pio run -e x4pro-gh_release` build with no new warnings in touched files (beware cached objects,
  memory note "pio build cache hides warnings"); `pio check` adds no defects in touched files;
  `./bin/clang-format-fix` leaves the tree clean.
- **Device (owner):** plan's checklist items 2–7 — default tab and notices, three-way stepping,
  eight rows with counts, headings on the magazine handouts, show mode, reference tap and swipe
  back to the same card, the `Handouts:` / `Handout I:` timing lines, free heap equal across
  Tarjetas → Bible → back, a `"v":2` file showing the unreadable notice.
