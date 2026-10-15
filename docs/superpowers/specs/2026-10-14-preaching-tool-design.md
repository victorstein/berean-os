# Design: Predicación, a door-to-door preaching tool

Surface: `ui` mostly, plus a parser in a new `lib/Preaching`, one small store in `src/study`, and
the reader's entry intent. No issue. Decisions were made with the owner in a brainstorm on
2026-10-14; mockups are in `.superpowers/brainstorm/80293-1792034719/content/` (not committed).
Implementation runs as one single-PR herdr pipeline batch of seven tasks (§10).

## Problem

House-to-house, the owner reaches for the same small set of scriptures: the ones the NWT itself
collects at its front, in "Introducción a la Palabra de Dios" — twenty questions a householder
might ask, each answered with a handful of quoted verses. On the device today that section is only
reachable through the Bible's table of contents, and it reads as an ordinary chapter: there is no
quick way in from Home, no way to put one verse in front of a householder, no thread from one
question to the next for a return visit, and the owner's own tagged verses on a subject are spread
across the tag filter rather than gathered as something to preach from.

## Goal

1. A **Predicación** card on Home opens a two-tab screen: the twenty questions (**Preguntas**) and
   the owner's own topics (**Mis temas**).
2. A question opens as **verse cards** — the quote and its reference — with the question as the
   title and a **"Para la revisita"** card naming the natural next question.
3. Tapping a quote opens **show mode**: one verse, full screen, large type, for the householder.
4. Tapping a reference opens the **Bible at that verse**; the left-edge swipe comes back to the
   card it came from.
5. **Mis temas** lists tags the owner picks (**Elegir temas**); each is a topic of the **Bible
   verses** carrying that tag, shown with the same cards.
6. Home's cover still reads **"LA BIBLIA"** after the card takes its height.

## Non-goals

- No other publication's paragraphs in Mis temas. Tags are global, but there is no cross-
  publication tag lookup: `StudyStore::passagesWithTag` searches only the open publication
  (`src/study/StudyStore.cpp:160`), and the reverse index the founding design planned for it
  (`docs/superpowers/specs/2026-09-13-berean-os-design.md:438`) was never built.
- No notes, return-visit records or householder data. Notes are out of scope for the device, and
  personal data about householders does not belong on an unencrypted SD card.
- No meeting-workbook ministry parts, no second-language Bible, no new font, no ministry report.
- No change to `tags.json`, the passages files, `PassageDoc::FORMAT_VERSION`, `TagPalette`, or
  any cache version.
- No change to what the reader records as a place: a verse opened from Predicación is recorded
  like any other visit, so it can become Continuar and appear in Recientes.

## Decisions

| | Decision | Why |
|---|---|---|
| D1 | Content is read from the installed Bible EPUB on demand (approach 1) | No new cache, no format version, always in the Bible's language; a question is one 2–5 KB file |
| D2 | The section is found by **filename**, `1001061250`–`1001061270.xhtml` | Identical in `nwt_S` and `nwt_E` (both checked 2026-10-14); titles are translated |
| D3 | Home entry is a **card below Reuniones**, not a fifth icon tile or a plate button | Most visible; room for a subtitle; the cover keeps "LA BIBLIA" (§2) |
| D4 | Mis temas tags are **picked inside Predicación**, stored as tag ids in their own file | 48 study tags would bury the 20 questions; `tags.json` keeps its format |
| D5 | Mis temas shows **Bible verses only** | One file to read (`bible.json`); the cross-publication index does not exist (Non-goals) |
| D6 | Show mode's largest size is the built-in **Noto Serif 18** | No flash cost; legibility is checked on the device |
| D7 | A follow-up table names the next question for a return visit | Fixed by question number, so it holds in any language (§4) |
| D8 | Opening the Bible **replaces** Predicación; the reader carries an origin back | Keeps the reader the only heavy screen in memory; one 5 KB re-parse on return |
| D9 | A Bible opened from Predicación records places like any visit | Owner's choice |

## Measured facts the design rests on

**The section.** `nwt_S.epub` (2019 Spanish NWT) and `nwt_E.epub` both carry
`OEBPS/1001061250.xhtml` (the index: the 20 questions and "Cómo buscar versículos") and one file
per question, `1001061251` … `1001061270`. Each body's class names it: `docId-1001061251`. The TOC
labels them "Pregunta N: <question>". The files are 1.7–4.7 KB; the twenty hold about 115
scriptures.

**A question file**, in order:

- `<p class="contextTtl">` — the "PREGUNTA N" label.
- `<h1>` — the question.
- Repeated: a paragraph holding `<q class="scrp">` (the quote, already excerpted with `[...]` by
  the publisher), then a paragraph holding one or more `<a href="<file>#chapterC_verseV">` links.
  One reference paragraph can carry several links: "Eclesiastés 9:5, 10" is two `<a>`, and
  "Mateo 26:14, 15; 27:5" points into two chapters. A quote paragraph can hold several quoted
  verses for those links.
- `<h2>` headings in three questions: Q6 alternates *Profecía* / *Cumplimiento*, Q17 has three,
  Q19 has ten.
- Q19 (the books of the Bible) and Q20 (how to read) are mostly prose: `<h2>` plus plain `<p>`,
  Q19 with no scripture at all.
- Images (`<figure>`) and page markers (`<span class="pageNum">`) appear throughout.

The link anchors use the `chapterN_verseM` grammar `VerseAnchors` already parses.

**Home today** (`test/home_layout/HomeLayoutTest.cpp:64-65`): the cover hero is 250 px (Lyra) and
272 px (Base); the plate across its foot is 101 / 102 px; its thumbnail is 773 px tall for the
464 px band (`:49`). The crop aims at `BOOK_TITLE_BAND = 0.25f`
(`src/components/CoverBandGeometry.h:19`), row 193.

**The NWT cover in that thumbnail:** "LA BIBLIA" ink occupies rows **167–214** (measured from the
cover JPEG scaled to 535×773). The rule under it is rows 233–234, "TRADUCCIÓN DEL NUEVO MUNDO"
252–266.

**Fonts and flash.** One Noto Serif 18 face is ~62 KB of flash (`firmware.map`: glyph bitmaps
0xad97, glyph table 0x42e0, intervals 0xf0). The 2026-10-11 `x4pro` build is 5,559,440 B of a
6,553,600 B app partition, leaving ~994 KB.

## 1. `lib/Preaching` — the section and its parser

No Arduino, no `HalStorage`; host-tested in `test/preaching/`.

**`IntroSection`**

- The 21 basenames as `constexpr` data, question `n` (1–20) at index `n`, the index page at 0.
- `nextQuestion(n)` → the follow-up table (§4), 0 for none.
- `stripTocPrefix(label)` → the question text from a TOC label: drop a leading run of non-space
  characters, a space, digits, `": "`. Anything that does not match is returned whole.

**`QuestionParser`** — a streaming pass over one question's XHTML, fed in chunks (the
`SpineHtmlStream::ChunkSink` shape, `src/activities/reader/SpineHtmlStream.h:40`). It emits:

| Block | From | Holds |
|---|---|---|
| title | `<h1>` | text |
| `Heading` | `<h2>` | text |
| `Verse` | a quote paragraph and the reference paragraph after it | quote text; the reference label (all link texts joined); the **first** link's href |
| `Text` | any other `<p>` | text |

Skipped: `contextTtl`, `<figure>`, `pageNum` spans. Entities and the `[...]` markers pass through
as text. A reference paragraph with no quote before it becomes a `Verse` with an empty quote; a
quote with no reference after it becomes `Text`.

Bounds: a question's whole text is under 5 KB, so the result is plain strings. The parser refuses
(returns false, nothing partial) past **64 blocks** or **8 KB** of block text. The largest file,
Q6, is 4.7 KB and about 20 blocks.

Fixtures are **short excerpts**, not whole files (the repo is public): a quote with its reference,
a multi-link reference, Q6's headings, a stretch of Q19 prose.

## 2. Home card

`src/activities/launcher/HomeLayout.h`, `HomeTargets.h`, `LauncherActivity.cpp`.

- A `preaching` box below `meetings`, **63 px**: `PAD + max(ICON, ui10 + small) + PAD`
  (8 + 47 + 8). One more `gap` between sections: `compute` subtracts five gaps, not four
  (`HomeLayout.h:120`).
- The hero becomes **179 px** (Lyra) and **199 px** (Base). Artwork above the plate: 78 / 97 px.
  The crop window with the card is rows 154–231 (Lyra), which holds "LA BIBLIA" with 13 px above
  and 17 px below; the rule and subtitle leave the frame.
- Content: a new 32×32 icon, `tr(STR_PREACHING)`, and the subtitle `tr(STR_PREACHING_SUBTITLE)`
  ("Preguntas de la Biblia · tus temas"). No counts.
- `Target::Preaching` between `Meetings` and `Tags` in the ring; `Action::OpenPreaching`. With no
  Bible it routes to `DownloadBible`, as `Tags` and `Search` do.
- **Tests** (`test/home_layout/`): the new heights and hero for both themes; route and ring; and
  the cover guard — for both themes, `CoverBandGeometry::crop` at the new hero's visible height
  yields a window containing rows 167–214 of a 773-row cover. If the card ever grows, this fails
  instead of the title being cut.

## 3. `PreachingActivity` — the two tabs

A `UiTabListActivity` in `src/activities/preaching/`.

**Entry.** Takes the Bible's `Epub` from `ParkedBook` (`src/activities/reader/ParkedBook.h:23`)
or opens it, resolves the 21 basenames in one pass with `Epub::resolveFilenamesToSpineIndices`
(`lib/Epub/Epub.h:93`), and parks the `Epub` again in `onExit` so the reader's load takes it. The
plan confirms in `ActivityManager` that the old activity's `onExit` runs before the new reader
loads; if not, the reader reopens the Bible instead (correct, slower).

**Preguntas.** One row per resolved question: the number, then the question from its TOC entry
(`getTocIndexForSpineIndex`, `Epub.h:81`) through `stripTocPrefix`; no TOC entry →
`tr(STR_PREACHING_QUESTION_N)` ("Pregunta %d"). Fewer than 21 resolved → the tab is one line,
`tr(STR_PREACHING_NO_SECTION)`. From Home the list opens at the first row; coming back from a
topic, or rebuilt from the reader's origin (§6), it selects that question or tag.

**Mis temas.** The picked tags still active, in `TagPalette::activeIdsByName` order, each with the
number of Bible passages carrying it; then **＋ Elegir temas**. Counts come from one streaming pass
over `/.berean/passages/bible.json` (the path `HomeVerse` reads, `src/activities/launcher/HomeVerse.cpp:33`),
made the first time the tab opens and held until Predicación exits. Nothing picked → one line,
`tr(STR_PREACHING_NO_TOPICS)`, and the Elegir row. A tag with 0 verses stays listed.

**Input.** Left/Right walk the ring (tab bar, rows) as every tab list does; a tap opens; the
left-edge swipe goes Home; the Home key goes Home.

## 4. `PreachingTopicActivity` — a question or a topic as cards

**Source.**

- A question: `SpineHtmlStream::stream` over its spine index into `QuestionParser`.
- A tag topic: a second streaming pass over `bible.json`, keeping each passage that carries the
  tag — its `reference`, its whole text (`displayText`, filled whole by `StudyStore::addPassage`,
  `src/study/StudyStore.cpp:319-347`), and its start `Unit` and spine hint. Bible order. The texts
  allocate in PSRAM (the passage-text allocator); fifty passages are ~15 KB.

**Layout.** Header: "Pregunta 8" or the tag name, and a page counter. Page 1 carries the question
title in Noto Serif 14 bold. Quotes in Noto Serif 12 (Home's verse card face); references bold,
right-aligned, with "›"; headings as small uppercase labels; `Text` in Noto Serif 12. Lines are
measured once; pages break between blocks where they can, and a card taller than a page continues
on the next — nothing is cut. The last page of a question ends with the "Para la revisita" card;
a tag topic has none. An empty topic: `tr(STR_PREACHING_TOPIC_EMPTY)`.

**Input.** Left/Right select the previous / next `Verse` card, turning the page at its edge
(headings and text are not selectable); a vertical swipe turns the page. Tap a quote → show mode
at that card (§5). Tap a reference → the Bible (§6). Tap "Para la revisita" → the next question
in place; Back still reaches the list. The left-edge swipe → the list.

**Follow-up table** (D7). N → N+1, except:

| From | To |
|---|---|
| 7 ¿Qué dicen sobre nuestros días las profecías bíblicas? | 10 ¿Qué promete la Biblia para el futuro? |
| 12 ¿Es posible que los muertos vuelvan a vivir? | 18 ¿Cómo podemos acercarnos más a Dios? |
| 16 ¿Cómo manejar los problemas y preocupaciones? | 18 |
| 17 ¿Cómo tener una familia feliz? | 18 |
| 18 | 20 ¿Cómo aprovechar al máximo la lectura de la Biblia? |
| 20 | none |

## 5. `PreachingShowActivity` — show mode

Pushed with `startActivityForResult` over the topic screen, which stays alive beneath it: it takes
a read-only view of the topic's `Verse` cards and a start index, and returns the index it finished
on, which the topic screen selects.

- No header or chrome. The quote centred vertically; the reference bold below it; a small footer,
  "2 / 4 · <question or tag>".
- Sizes step down until the quote fits: **Noto Serif 18 → 14 → 12**, as `HomeVerse::FONT_IDS`
  does (`src/activities/launcher/HomeVerse.h:24`). Past 12 the quote pages within itself at 12;
  Left/Right page through it before moving on. Nothing is cut.
- Tap zones are the reader's (`ReaderUtils::isTouchMenuTap`, `src/activities/reader/ReaderUtils.h:140`):
  outer thirds or Left/Right → previous / next verse; centre or the left-edge swipe → back.
- A verse change refreshes as a reader page turn does. Auto-sleep is unchanged.

## 6. Opening the Bible and coming back

**`ReaderEntryIntent`** (`src/activities/reader/ReaderEntryIntent.h:13`) gains:

- `Kind::OpenHref` with a fixed `char href[64]` — the link as the question file has it. The
  longest in the section is ~44 bytes (`1001061123-split146.xhtml#chapter146_verse4`); a longer one
  is logged and not opened. Once the Bible loads, the reader calls
  `navigateToHref(href, /*savePosition=*/false)` (`src/activities/reader/EpubReaderActivity.cpp:2125`).
- `PreachingOrigin`: none, or a question number, or a tag id; plus the card index. Unlike the rest
  of the intent the reader **copies it into a member for its whole life**; it is not consumed when
  the book loads.

Tag topics open with the existing `Kind::OpenAt` (the passage's `Unit` and spine hint), so they
resolve in any edition.

**Return** (beside the branch at `EpubReaderActivity.cpp:694`):

1. Swipe with entries in `returnStack` → pop, as today.
2. Swipe with `returnStack` empty and an origin → `activityManager.goToPreaching(origin)`:
   Predicación is rebuilt with the topic screen pushed at that card.
3. No origin → unchanged: the swipe is inert, as `ReaderUtils::handleBackNavigation` makes it
   (`ReaderUtils.h:255`).

The Home key, and sleep (wake reopens the book without an origin), drop it.

## 7. `PreachingTopicsFile` — the picked tags

`/.berean/preaching.json`, **not** under `/.berean/passages/` — the passage scan reads every
`*.json` there as a publication (`lib/Serialization/SdPaths.h:26`). Add the path to `SdPaths.h`.

```json
{ "v": 1, "tags": [12, 31, 7, 40] }
```

- Written through `writeDocToFileAtomic`, never `writeDocToFile`.
- `SAVE_BYTE_BUDGET = 2048`, checked before writing; refuse and report. 200 ids of up to five
  digits fit in about 1.3 KB.
- `v` greater than 1 is refused, not reinterpreted.
- A failed load latches saving off for the session (the load-refused guard the core stores use),
  so the picks are never overwritten with an empty list.
- Owned by the loop task; writes hold `storageMutex` through `HalStorage`.
- Tag ids not active in the palette are dropped on the next save. Order is not meaningful;
  display follows `activeIdsByName`.

The list is capped at `TagPalette::MAX_ACTIVE_TAGS` (200); a longer list on load is refused.

**Elegir temas** is a `TagChipGridActivity` subclass modelled on `TagPickerActivity`: multi-select,
active chips ticked, **no tag creation**. The left-edge swipe saves and returns; a failed save
shows a popup and keeps the previous selection.

## 8. Strings

All in task T3, so T4–T7 never touch the translation files. English and Spanish:

`STR_PREACHING` (Predicación), `STR_PREACHING_SUBTITLE` (Preguntas de la Biblia · tus temas),
`STR_PREACHING_QUESTIONS` (Preguntas), `STR_PREACHING_MY_TOPICS` (Mis temas),
`STR_PREACHING_CHOOSE_TOPICS` (Elegir temas), `STR_PREACHING_QUESTION_N` (Pregunta %d),
`STR_PREACHING_NEXT_VISIT` (Para la revisita), `STR_PREACHING_NO_SECTION`,
`STR_PREACHING_NO_TOPICS`, `STR_PREACHING_TOPIC_EMPTY`, `STR_PREACHING_SHOW_POSITION` (%d / %d · %s),
`STR_PREACHING_OPEN_FAILED`, `STR_PREACHING_VERSE_NOT_FOUND`, `STR_PREACHING_SAVE_FAILED`.
Final wording is the T3 worker's, against these meanings.

## 9. Errors, memory, testing

| What fails | What the user sees |
|---|---|
| No Bible | The card downloads it |
| The section is missing | Preguntas says so; Mis temas still works |
| A question will not read or parse | The list stays, with `STR_PREACHING_OPEN_FAILED` |
| `bible.json` missing or unreadable | Mis temas shows no counts; a topic says it cannot read the verses; nothing is written |
| `preaching.json` corrupt or newer | Saving latched off for the session |
| Saving the picks fails | A popup; the previous selection stays |
| A reference does not resolve | `STR_PREACHING_VERSE_NOT_FOUND`; stay on the card |
| OOM | `makeUniqueNoThrow`, `LOG_ERR`, a message — no abort |

**Memory.** A question: under 5 KB of strings plus line offsets, internal heap. A tag topic: its
texts in PSRAM. The `bible.json` counts pass streams; it holds one count per picked tag. Show mode
holds no text of its own. The `Epub` is the parked one, not a second copy.

**Host tests.** The parser and `IntroSection` (§1); Home's layout, route, ring and cover guard
(§2); `PreachingTopicsFile` against `HalStorageFake` — round trip, budget refusal, newer-version
refusal, retired-tag pruning, the latch (§7); `ReaderEntryIntent` routing of `OpenHref`.

**On the device** (the owner): the flow end to end; Noto Serif 18 at arm's length; the time of the
`bible.json` pass from the serial log, with a "Cargando…" line added if it is slow; free heap
equal across Predicación → Bible → back.

## 10. Pipeline tasks

One single-PR batch. T1 and T2 start together; T4–T7 run in parallel once T3 lands.

| | Task | Agent | Tier | Files | After |
|---|---|---|---|---|---|
| T1 | `lib/Preaching`: `IntroSection`, follow-up table, `QuestionParser` (§1) | epub-dev | standard | `lib/Preaching/`, `test/preaching/` | — |
| T2 | `PreachingTopicsFile` (§7) and the `bible.json` passes for counts and a tag's passages (§3, §4) | data-dev | standard | `src/study/Preaching`, `lib/Serialization/SdPaths.h`, its test suite | — |
| T3 | `PreachingActivity`, `PreachingTopicActivity` (§3, §4); the `ReaderEntryIntent` fields and `goToPreaching` (§6, contract only); every string (§8) | ui-dev | heavy | `src/activities/preaching/`, `src/activities/ActivityManager.`, `src/activities/reader/ReaderEntryIntent.h`, `lib/I18n/translations/` | T1, T2 |
| T4 | Show mode (§5) | ui-dev | standard | `src/activities/preaching/PreachingShow`, `src/activities/preaching/PreachingTopicActivity.` | T3 |
| T5 | Elegir temas (§7) | ui-dev | standard | `src/activities/preaching/PreachingTopicPicker`, `src/activities/preaching/PreachingActivity.` | T3 |
| T6 | Home card and cover guard (§2) | ui-dev | standard | `src/activities/launcher/`, `test/home_layout/` | T3 |
| T7 | The reader honours `OpenHref` and the origin (§6) | ui-dev | heavy | `src/activities/reader/` (not `ReaderEntryIntent.h`'s fields, which T3 adds) | T3 |

New test suites are handed off for `test/CMakeLists.txt` in each PR body; the orchestrator applies
them at landing.
