# Design: Tarjetas, the preaching cards in Predicación

Surface: `ui` (a third tab in `PreachingActivity`), one read-only reader in `src/study`, and a
generator script that lives with the cards, outside this repo. Decisions were made with the owner
in a brainstorm on 2026-11-22.

## Problem

House to house, the owner hands out printed NFC/QR cards (`~/Documents/rfid-cards`, eight so far:
Esperanza, Familia, Trabajo, Salud mental, Estrés, Biblia útil, Dinero, Dormir). Each card links
to a jw.org article, and the conversation at the door follows that article's scriptures.
Predicación (v1.31.0, `docs/superpowers/specs/2026-10-14-preaching-tool-design.md`) has the
NWT's twenty questions and the owner's tagged verses, but not the scriptures of the card they
just handed over.

## Goal

1. A **Tarjetas** tab, **first** in Predicación and open by default, lists the cards.
2. A card opens as the same verse cards Preguntas and Mis temas use: the quote, its reference,
   show mode, and a tap on the reference opens the Bible at the verse.
3. A card whose link is a whole magazine issue (Salud mental, Biblia útil) holds every reference
   in the issue, under a heading per article.
4. Adding a card needs no firmware release: the list is a file on the SD card, uploaded through
   File Transfer.

## Non-goals

- No editing cards on the device, and no fetching from jw.org on the device.
- No NFC or QR reading.
- No verse text read from the installed Bible at open time (approach B, rejected below).
- No change to Preguntas, Mis temas, `preaching.json`, `tags.json` or any cache version.

## Decisions

| | Decision | Why |
|---|---|---|
| D1 | The list is **`/preaching-cards.json`**, at the SD root | Owner's choice: cards keep being added (Dormir, 2026-11-22). Not under `/.berean/`: File Transfer refuses every dot-directory path (`src/network/FileRoutes.cpp:124`), and uploading is how a new card arrives |
| D2 | A Mac script writes the file with the **verse text already in it** (approach A) | The device loads a card the way it loads a tag topic; no chapter parsing at open time. The file is the owner's, on their card, so NWT text never enters this public repo |
| D3 | The device only **reads** the file | No atomic write, save budget or owning writer task to build; the store rules that remain are the version refusal and the size cap (§2) |
| D4 | Tarjetas is a **new first tab**, open by default; Preguntas and Mis temas keep their content | Owner's choice |
| D5 | An issue-level link becomes **one section per article**, headed by the article's title | Owner's choice; headings already render (`CardKind::Heading`) |
| D6 | References open the Bible through the existing **`OpenHref`** path, with the EPUB's own `<file>#chapterC_verseV` href | The path Preguntas uses (`PreachingTopicActivity.cpp:212-222`); NWT filenames match across editions (2026-10-14 spec, D2) |

**Approach B, rejected:** the file holds references only and the device extracts the text with
`BibleSearch::VerseTextScanner` (`lib/BibleSearch/BibleSearch/VerseTextScanner.h`). It follows
the Bible's language, but a card such as Esperanza touches about 20 chapter files, each streamed
and parsed when the card opens, and that load time has never been measured. The owner reads one
language.

## Measured facts

**The cards.** Each printed card is a folder `rfid-cards/final/NN-slug/` whose `README.txt` holds
the title on line 1 and `QR / NFC: <url>` on line 2. `build.py` also has the URLs (`TOPICS`,
`:56-67`), but importing it runs `qrencode` for every design. `rfid-cards` is not a git repository.

**The articles** (fetched 2026-11-22). A scripture link is
`<a class="jsBibleLink" data-targetverses="BBCCCVVV[-BBCCCVVV]" …>`: book, chapter, verse, and an
optional end. Linked references per card: Esperanza 26, Familia 9, Trabajo 4, Estrés 17,
Dinero 5, Dormir 3. Salud mental (`wp23` 2023 no. 1) and Biblia útil (`wp18` 2018 no. 1) link to
an issue page that lists eight articles each, with 35 and 63 linked references.

Traps the script must handle:

- **Link text is not a label.** It is partial when a citation continues ("Salmo 37:10, 29" is two
  links, the second reading "29"; "Marcos 4:39; 6:41-44" has a link reading "6:41-44").
- Attributes appear with both `"` and `'` quoting. Parse the HTML; do not grep it.
- The same verse can be cited twice in one article (Estrés: Proverbios 17:27).
- Some articles quote scripture with no link (Biblia útil's "ayuda-a-resolver-problemas" has
  none). They contribute no references; the script does not guess.

## 1. The generator — `rfid-cards/src/cards_json.py`

Lives beside `build.py`. It reads the cards from `final/*/README.txt` in folder order, so a card is
declared once, by the folder that holds its print files. For each card:

1. Fetch the URL, following redirects. If it is an issue page, fetch each of its articles in page
   order and make one section per article, the heading being the article's `<h1>`; otherwise one
   section with an empty heading.
2. Collect `data-targetverses` in document order. Drop a range already collected in the same
   section.
3. Resolve each range against a local NWT EPUB (a path argument, `nwt_S.epub`): find the spine
   file holding `id="chapterC_verseV"` for that book, and take the visible text of verses V..V2.
   The book is found through the EPUB's own navigation, because chapter ids repeat across books.
4. Build the label from the EPUB's book name: `Mateo 6:34`, `Filipenses 4:6, 7`,
   `1 Juan 2:15-17` (two verses joined with ", ", three or more with "-").
5. Write the JSON (§2). Fail, writing nothing, if any range does not resolve or the result
   exceeds the device's size cap.

A self-check runs against a small fixture: one single-article page, one issue page and one
range, asserting the labels and the unresolved-range failure. It needs no network.

## 2. The file — `/preaching-cards.json`

```json
{ "v": 1,
  "cards": [
    { "title": "Estrés",
      "sections": [
        { "heading": "",
          "verses": [
            { "ref": "Mateo 6:34",
              "href": "1001061xxx-splitN.xhtml#chapter6_verse34",
              "text": "…" } ] } ] } ] }
```

- At the SD root, as `PreachingHandouts::FILE_PATH`. Not in `SdPaths.h`, which holds only paths
  under the firmware's two roots.
- `v` greater than 1 is refused, not reinterpreted.
- A file over **128 KB** is refused before parsing, with nothing partial. The eight cards come to
  about 50 KB.
- `href` over the intent's fixed `char href[64]` is rejected by the script, and logged and
  skipped by the device.

## 3. Device — `PreachingHandouts` (`src/study/PreachingHandouts.{h,cpp}`)

In code a printed card is a **handout**: "card" already names a layout card
(`PreachingCardLayout.h`), and one `TopicCards` holding a card's cards would not read.

Read-only, shaped like `PreachingPassages` (`src/study/PreachingPassages.h`): a stream through
ArduinoJson with `PsramJsonAllocator`, statuses `Ok | Missing | Unreadable | TooNew | OutOfMemory`.
At most 200 handouts are read, so an index fits `PreachingOrigin`'s `uint8_t`.

- `titles(out)`: each card's title and verse count, for the list. Held by `PreachingSession`
  until Predicación exits, read the first time the tab shows.
- `load(index, out)`: one card's sections and verses. Strings go through the passage-text
  allocator, as a tag topic's do.

Host test (`test/storage_io/PreachingHandoutsTest.cpp`, against `HalStorageFake`): round trip of a short fixture with
made-up text, an empty heading, `v: 2` refused, an over-cap file refused, a malformed file
`Unreadable`, a missing file `Missing`.

## 4. UI

`src/activities/preaching/`.

- `PreachingActivity::Tab` gains `Handouts` as the first value, the default. `stepTab`
  (`PreachingActivity.cpp:141-143`) cycles three tabs instead of toggling two; `tabLabel`
  (`:73`) gains `tr(STR_PREACHING_CARDS)`.
- Rows: the card's title, its verse count as the value. No file → one line,
  `tr(STR_PREACHING_NO_CARDS)` (it names the path). `Unreadable` / `TooNew` →
  `tr(STR_PREACHING_CARDS_UNREADABLE)`. The other tabs work either way.
- `PreachingContent::loadHandout(index, out)` builds `TopicCards` like `loadTag`
  (`PreachingContent.cpp:80-104`): a `Heading` card for each non-empty section heading, then a
  `Verse` card per verse. `TopicCards` gains the handout index and holds the loaded handout.
- `PreachingOrigin::Kind::Handout` with a `uint8_t handout` index, and `ofHandout`. Every place that
  switches on the kind gains the case: `PreachingActivity.cpp:42-57,114-118,314-320`,
  `PreachingTopicActivity.cpp:163-165`. A card index past the end on return (the file changed
  under it) leaves the list showing, with the unreadable-cards toast.
- `PreachingTopicActivity::openBible` (`:202`): a card verse builds its intent with
  `ReaderEntryIntent::openHref` and checks `resolveHrefToSpineIndex`, the question branch's code,
  which takes the href from the card instead of a parsed block.
- No "Para la revisita" card for a card topic.

Strings, English and Spanish: `STR_PREACHING_CARDS` (Tarjetas), `STR_PREACHING_NO_CARDS`,
`STR_PREACHING_CARDS_UNREADABLE`.

## 5. Errors and memory

| What fails | What the user sees |
|---|---|
| No file | Tarjetas says where to put it |
| File newer, malformed or over the cap | Tarjetas says it cannot read the cards; nothing is written |
| A card's href does not resolve in this Bible | `STR_PREACHING_VERSE_NOT_FOUND`; stay on the card |
| OOM | `makeUniqueNoThrow`, `LOG_ERR`, `STR_PREACHING_OPEN_FAILED` |

**Memory.** The list holds titles and counts (under 1 KB for eight cards). An open card holds its
verse texts in PSRAM through the passage-text allocator: Biblia útil, the largest, is about 15 KB.
Nothing stays resident after Predicación exits.

## 6. Verification

- Host: the `PreachingCards` suite (§3) and the script's self-check (§1).
- Build: `pio run`, `pio check`, `./bin/clang-format-fix`.
- On the device (the owner): upload the generated file to the SD root over File Transfer; Tarjetas opens first; each
  card opens; Salud mental shows its article headings; a reference opens the Bible and the
  left-edge swipe comes back to the card; free heap equal across Tarjetas → Bible → back.
