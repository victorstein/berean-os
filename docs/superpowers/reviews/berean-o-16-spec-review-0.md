Tier: heavy

# Spec review 0: berean-o-16 (Predicación screens and the reader entry contract)

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-16-design.md`, checked against the brief
(`hpipe brief --task t3`), the research note
`docs/superpowers/research/2026-10-15-berean-o-16-research.md` and the parent spec
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §3–§6, §8 and §9. The code was read at
`bd60929c`. The NWT measurements come from `~/Downloads/nwt_S.epub`, extracted to a scratch directory:
`content.opf`, `toc.ncx` and the 21 question files.

| Severity | Count |
|---|---|
| BLOCKER | 0 |
| MAJOR | 2 |
| MINOR | 5 |

Assumptions:

- **Hold:** A1, A2, A3, A4, A6, A7, A8, A9, A10, A11, A12, A13, A16, A18, A19, A20, A21, A22, A24
  and A25.
- **Holds, with a wrong figure:** A5. 21 `std::string`s are 504 B on the 32-bit device (24 B each),
  not 672 B. That is still over the 256-B limit.
- **Underspecified:** A14 (m2).
- **Wrong premise:** A15 (m1).
- **Holds, with an inexact premise:** A17. The power key can be set to Confirm
  (`SettingsList.h:373-376`).
- **Wrong rationale:** A23 (m3).

## What holds up

I checked these claims against the code and they are correct:

- **`onExit` runs before the reader loads.**
  - In the Replace branch, `exitActivity` runs the current activity's `onExit` and destructor. Each
    stacked activity's `onExit` and destructor follows. Only then does the pending activity get its
    `onEnter` (`ActivityManager.cpp:143-160`, `:176-182`).
  - The reader's constructor stores only the path and the intent (`EpubReaderActivity.h:224-227`).
    `loadBook` takes the parked book (`EpubReaderActivity.cpp:217-219`).
  - The reverse direction also holds: the reader parks in its destructor (`:165-188`).
- **A6, the lifetimes.**
  - A Replace destroys the current activity before the stack (`ActivityManager.cpp:145-150`).
  - A Pop moves the result out, destroys the child and then calls the parent's handler (`:103-128`).
  - A push from `onEnter` is handled in the same `loop()` pass (`:159-163`).
- **A21, the reader case.**
  - The only `switch` over `Route` is `onBookLoaded`, and it has no `default`
    (`EpubReaderActivity.cpp:1104-1132`).
  - `-Wall` is added to repo sources per file (`scripts/enable_repo_warnings.py:27-30`).
  - The only `switch` over `Kind` is `route()` itself (`ReaderEntryIntent.h:36-50`).
    `HomeTargets.h:39-78` only compares and assigns `Kind`.
  - So one case in each switch is necessary and enough.
- **A1, the include chain.**
  - `TagPalette.h:3` includes ArduinoJson, and `TagId` is declared at `:23`.
  - Two host suites compile `ReaderEntryIntent.h`: `ReaderEntryIntentTest`, and `HomeLayoutTest`
    through `HomeTargets.h`. Both put `src` on the include path (`test/ui_layout/CMakeLists.txt:89-92`,
    `test/home_layout/CMakeLists.txt:11-14`), so a `PreachingOrigin.h` that uses only std headers
    resolves in both.
  - `HomeLayoutTest` therefore counts as a touched suite.
- **The 64-byte href.** I measured all 109 question hrefs in `nwt_S`:
  - the longest is 45 B;
  - every one resolves to a spine item.
- **The section's position.** `1001061250`–`1001061270` sit at spine 5–25 of 3,937. The
  21-name resolve stops after 26 items (`Epub.cpp:980-990`).
- **Glyphs** (from the interval tables in `lib/EpdFont/builtinFonts/`):
  - U+203A "›" is present in `notoserif_12_bold`, `notoserif_12_italic`, `ubuntu_10_regular` and
    `ubuntu_10_bold`.
  - U+FF0B "＋" is in none of them. A19's ASCII "+" is required, not just cautious.
- **Faces.** Noto Serif 12 and 14 are registered in all four styles, and UI 10 in bold
  (`src/main.cpp:69-81,125-127`).
- **A16.** It follows the `TagSelectionResult` precedent (`ActivityResult.h:86-92`). No `std::visit`
  over `ResultVariant` exists in `src`.
- **Strings.** All 14 §8 keys are in the table. `STR_PREACHING_VERSES_UNREADABLE` is within the
  brief's "add any string they will need".

## MAJOR

### M1. The Preguntas fallback "no TOC entry → `STR_PREACHING_QUESTION_N`" can never fire

**Claim** (spec :136, :275-276):

- `titles[n]` is `getTocItem(getTocIndexForSpineIndex(spine)).title` through `stripTocPrefix`.
- It is "empty when the TOC index is < 0", and an empty title shows `tr(STR_PREACHING_QUESTION_N)`.

**Problem.** A spine item with no TOC entry of its own does not report −1. It **inherits the previous
spine's TOC index**.

- A question without its own entry would show the previous question's text beside its own number.
  For Q1 that text would be "Introducción a la Palabra de Dios".
- On a preaching tool, that points the user at the wrong question.
- The fallback that the brief and parent §3 require is unreachable as designed.

**Evidence.**

- `Epub.cpp:899`: `getTocIndexForSpineIndex` returns `getSpineItem(spineIndex).tocIndex`.
- `BookMetadataCache.cpp:337-346`:
  `if (spineEntry.tocIndex == -1) { … "using title from last section" …; spineEntry.tocIndex = lastSpineTocIndex; }`.
- In `nwt_S` the index page `1001061250` (spine 5) has its own `navMap` entry, so every later spine
  has an index ≥ 0.
- Brief: "No TOC entry → `STR_PREACHING_QUESTION_N`". Parent §3: lines 159-160.
- Both editions checked do have an entry per question, so this is latent on the owner's device.
  Still, it is the specified behaviour.

**Fix.** Accept a TOC entry only when it belongs to that spine:

```
const int toc = epub.getTocIndexForSpineIndex(spines[n]);
if (toc >= 0) {
  const auto entry = epub.getTocItem(toc);
  if (entry.spineIndex == spines[n]) titles[n] = stripTocPrefix(entry.title);
}
```

`TocEntry::spineIndex` is at `BookMetadataCache.h:32-38`. Write this rule into the `PreachingSession`
table.

### M2. The free-heap device check reports a leak when nothing was parked

**Claim** (spec :471): "Free heap … equal across Predicación → topic → back → Home."

**Problem.** The check's result depends on what ran before it.

- When the parked slot is empty or holds another book (after boot, or after reading a meeting
  publication), Predicación opens a fresh `Epub` (:133) and parks it in `onExit` (A7).
- Only the reader takes or clears that slot, so the `Epub` stays resident at Home.
- From a cold slot, the owner therefore sees Home's free heap drop by the size of the parked book and
  reads it as a leak. From a warm slot the same check passes.

**Evidence.**

- `ParkedBook.cpp:43-51`: `park` keeps the book.
- The only `ParkedBook::` callers in `src` are `EpubReaderActivity.cpp:174,181,186,219`
  (`grep -rn "ParkedBook::" src`).
- `CLAUDE.md`, the held-across-screens table: the parked Bible is released only by "opening another
  book, deleting its cache".

**Fix.** Rewrite the check with its precondition:

1. Open the Bible in the reader and go Home.
2. Take the baseline at Home.
3. Enter Predicación and expect `PARK: Reusing the parked book` (`ParkedBook.cpp:63`).
4. Go Predicación → topic → back → Home, and compare.

List the cold-slot case separately: an expected one-time drop the size of the parked `Epub`, not a
leak.

## MINOR

### m1. `TopicCards` holds views into its own strings, so the revisita swap must not move it

**Claim.** `cards` are "views into the two above" (:169-177). On revisita, "swap the cards and layout
under the render lock" (:339-342). A15 keeps an uppercased `std::string` per heading "beside the
cards" (:235-238).

**Problem.** `TopicCards` is an ordinary movable struct, and `loadQuestion` fills a caller's
`TopicCards&` (:179).

- The natural revisita code loads into a local, then `std::swap`s or move-assigns it into the current
  object.
- That relocates every string libstdc++ keeps inline (15 bytes or fewer), and the `CardInput` views
  into those strings dangle.
- Two places are exposed:
  - `parsed.title`, a direct member (`QuestionParser.h:31-34`), in any edition whose question title
    is 15 bytes or fewer;
  - A15's copies, if they are held anywhere but a vector buffer: "PROFECÍA" is 9 B, "LAS ESPOSAS"
    11 B.
- The Spanish titles happen to be at least 17 B (the shortest is "¿Quién es Dios?"), which hides the
  bug on the owner's device.

**A15's premise is wrong.** The publisher's headings are already uppercase in the markup:

- `1001061256.xhtml`: `<h2 …><strong>PROFECÍA</strong></h2>`;
- Q17: "LAS ESPOSAS";
- Q19: "EL PENTATEUCO (5 LIBROS):".

The copies, and the "Profecía" → "PROFECÍA" test input (:456), model text that never occurs.

**Fix.**

- Delete `TopicCards`'s copy and move operations, and create it only through
  `makeUniqueNoThrow<TopicCards>()`.
- Load the revisita into a new instance, then swap the `unique_ptr`s and layouts under `RenderLock`.
- Build `cards` only once `parsed` and `passages` are final. `passagesWithTag` sorts its output
  (`PreachingPassages.cpp:200`).
- For A15, either:
  - keep the transform as a defensive pass-through, with its outputs in a vector reserved before any
    view is taken; or
  - drop it and draw headings verbatim.

### m2. A14's keep-with-next and the over-tall-card rule have no precedence

**Claim** (:228-234):

- A card taller than a page "starts a new page (unless the current one is empty)".
- A `Heading` or the `Title` "never ends a page; it moves to the next page with the card after it".

**Problem.** Take a heading followed by a card taller than a page:

1. The heading's page is not empty, so the tall card starts the next page.
2. The heading now ends its page, so A14 moves it forward.
3. On the new page it again makes the page non-empty, and the cycle repeats.

An implementation either loops or strands the heading, and the spec does not say which rule wins.
`Title` followed by an over-tall first card on page 1 has the same conflict.

**Evidence.** The largest real question block is 617 B (`1001061257.xhtml`), well under a page, and
tag topics have no headings. Real data will not trigger this. The issue is that the host tests need a
defined answer.

**Fix.**

- Add the rule: "a page holding only `Title`/`Heading` cards counts as empty for the over-tall rule".
- Add a `PreachingCardLayoutTest` case: a heading and an over-tall card → the heading shares its page
  with the card's first lines.

### m3. A23 understates the per-tap cost: a hit walks the spine too, and T7 walks it again

**Claim** (:337-338): `resolveHrefToSpineIndex` "walks the whole spine on a miss … once per tap".

**Problem.** The function scans from spine 0 until it finds the match (`Epub.cpp:964-972`).

- Each item costs two seeks and reads of `book.bin` (`BookMetadataCache.cpp:588-593`), plus a heap
  string (`Epub.h:90-92`).
- In `nwt_S` the 109 question hrefs land at a median spine of 1,095; the furthest is at 1,342.
- So a typical reference tap reads about 1,100 spine entries before `goToReader`. T7's
  `navigateToHref` then resolves the same href again.

The conclusion (check on the tap, not at layout) is right; the latency is unmeasured.

**Fix.**

- Correct A23.
- Add a device check: `LOG_DBG` the milliseconds spent in the pre-check, and time tap-to-page for the
  furthest reference.

### m4. Returning to a tag topic pays two `bible.json` passes before anything shows

**Claim** (:270-273): a tag origin switches to Mis temas, "loading topics and counts", then loads the
topic.

**Problem.** On every return from a tag card's verse, `onEnter` runs both passes before the topic
screen appears:

- `countByTag` (`PreachingPassages.cpp:161-188`);
- `passagesWithTag` (`:190-…`).

Each one streams `bible.json`. The counts are not visible until the user backs out of the topic.

- Parent D8 budgets the return at "one 5 KB re-parse".
- Parent §9 already flags the counts pass as possibly slow.

**Fix.**

- On a tag origin, load `topics` (needed for the row and the header name).
- Defer `countByTag` to the topic's result handler, when Mis temas is first actually shown. That
  still satisfies "the first time the tab opens".

### m5. The topic's result handler has to pull the viewport to the row

**Claim** (:297-298): "switch to that tab, select that row … request an update".

**Problem.** By the time the topic returns, `followOnBuild` was already consumed on the list's first
build. `syncTabListViewport` then only clamps (`UiTabListActivity.cpp:92-104`).

- After a few "Para la revisita" hops (for example Q7 → Q10 → Q11 → Q12 → Q18), the selected row can
  sit below the viewport.
- It stays off-screen until the next button press.
- The origin path is unaffected, because its first build still follows.

**Fix.** After switching tabs, call `moveRingTo(ring)` (`UiTabListActivity.cpp:49-62`). It sets the
selection, pulls the viewport and requests the update.

VERDICT: CLEAR
