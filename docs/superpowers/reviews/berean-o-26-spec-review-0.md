Tier: standard

# Spec review 0: berean-o-26, the Tarjetas tab

Reviewed: `docs/superpowers/specs/2026-11-22-berean-o-26-design.md` against the brief
(`hpipe brief --task t2`), the research note, parent design §4–5 and parent plan Tasks 6–9, at
HEAD `020773fc`. The batch has not moved past HEAD (`git log HEAD..origin/batch/...` is empty).

## What was checked and holds

- **Every `file:line` citation in the spec resolves to the code it describes.** I checked
  `PreachingActivity.{h,cpp}`, `PreachingTopicActivity.cpp:163-167,202-232,275-276`,
  `PreachingContent.{h,cpp}`, `PreachingSession.{h,cpp}:143-153`,
  `PreachingHandouts.{h,cpp}:28,38-49,57,105,129-131,142-150`, `PreachingCardNav.h:40-45`,
  `ReaderEntryIntent.h:49-57`, `EpubReaderActivity.cpp:706,711-713`, `ActivityManager.cpp:232-233`,
  `UiTabListActivity.cpp:70-71`, both YAMLs (`english.yaml:498`, `spanish.yaml:12,445`) and
  `USER_GUIDE.md:335`.
- **The kind seams are complete.** Outside `src/activities/preaching/`, nothing reads
  `PreachingOrigin::kind`. The reader only copies the origin and tests `isSet()`
  (`EpubReaderActivity.h:233`, `.cpp:706,713`). No `switch` anywhere covers `PreachingOrigin::Kind`
  or `PreachingContent::LoadResult`, so a new enumerator cannot raise a `-Wswitch` warning. Inside
  the preaching code, the spec covers every place that branches on kind:
  - `originAt` and `openBible`;
  - `onEnter`, `seedSelection`, `onTopicResult` and `activateIndex`;
  - `openNextQuestion`, which already guards on `Question` (`PreachingTopicActivity.cpp:235`).

  Show mode returns only `verse` (`PreachingShowActivity.cpp:135-139`), so it needs no change.
- **A3: the Question origin gets an explicit tab.** Plan Task 8 Step 3 sets `Tab::Questions`
  before `if (tab == Tab::Handouts) session.ensureHandouts()`, which is what the brief asks for.
- **`ensureHandouts` outside the RenderLock is safe.** It loads once, and its first call always
  comes before any row points into `handoutSummaries`: in `onEnter` before the first
  `rebuildRows`, and in `showTab` while `tab` is still the previous tab. This is the same shape
  as `ensureCounts` (`PreachingActivity.cpp:126`).
- **The bounds claims hold.**
  - `Verse::section` is always a heading index that has already been pushed
    (`PreachingHandouts.cpp:143-144`).
  - `summaries` and `load` skip the same verses (`:105` against `:147-150`), so a row's count
    equals the number of Verse cards.
  - An index past the end returns `Unreadable` (`:129-131`).
  - `MAX_HREF_BYTES = 63` is less than `HREF_BYTES = 64`, the size `openHref` refuses at.
- **Comparing `uint16_t section` with `int section = -1` is sound.** The `uint16_t` promotes to
  `int`, so the first verse always opens a section.
- **A11 against the real file.** I parsed `/Volumes/stein/Documents/rfid-cards/out/preaching-cards.json`
  (65,151 B, v1, 8 cards). Every section of the two issue handouts has verses (2–18 each, 8
  headings apiece), and every single-article card has one empty heading. "Heading on a change of
  section" therefore gives the brief's 8 headings per issue handout. The largest handout has 77
  verses, the largest quote is 9,326 B, and the longest href is 45 B, all within the caps.
- **A tall quote does not break the layout.** `PreachingCardLayout.h:8-10` already continues a
  card that is taller than a page, and `Line::card` and `Segment::firstLine` (`uint16_t`) are far
  above what 77 verses produce.
- **A4 (keyed on the index) is sound.** The web server starts only from
  `CrossPointWebServerActivity` (`ActivityManager.cpp:200`, `SettingsActivity.cpp:317`), so the
  file cannot change while the reader holds an origin.
- **A13 is safe to keep.** Siblings t1 and t3 are done, and neither touched the translation YAMLs
  (`git log origin/main..HEAD -- lib/I18n/translations` is empty).
- **The three-way `stepTab` copies an existing pattern.** `TextSettingsActivity::switchTab`
  (`TextSettingsActivity.cpp:490-499`) already steps tabs as `(tab + direction + count) % count`.

## Findings

### MINOR 1 — "Left/Right on the tab bar" is not how tabs step; the device check needs a hold

- **Claim.** Goal 3 and the device checklist the spec adopts (plan item 2): "Left/Right on the
  tab bar cycle all three tabs in both directions".
- **Problem.** A press-and-release of Left/Right moves the ring through the tab band and the rows.
  Only a held press calls `stepTab`, and it does so from anywhere in the list, not just the tab
  bar. A tester who taps Right on the tab bar will see the first row selected, not Preguntas, and
  may fail a correct build.
- **Evidence.**
  - `UiTabListActivity.cpp:67-69`: `onNextRelease` / `onPreviousRelease` call `moveRingTo`.
  - `:70-71`: `onNextContinuous` / `onPreviousContinuous` call `stepTab(±1)`.
  - `ButtonNavigator.h:41`: continuous navigation starts after 500 ms.
- **Fix.** In the spec's Device line, and later in the plan's checklist, write it as "**hold**
  Left / Right to cycle all three tabs both ways; a tab tap or Confirm on the tab bar also works".
  The code does not change.

### MINOR 2 — A12 fixes "two tabs" but leaves "A topic" describing only questions and tags

- **Claim.** A12: §11 gets a Tarjetas bullet and "three tabs", so that the shipped guide stays
  true.
- **Problem.** The "A topic" subsection still says "A question or a topic opens as cards… The
  header names it, such as **Question 8** or the tag's name", and describes only question and tag
  content. Once handouts open on the same screen, with article headings, the guide is half-updated.
- **Evidence.** `USER_GUIDE.md:349-353`.
- **Fix.** Widen A12 by one sentence in "A topic": "A card opens as its verses, under a heading
  per article for a magazine issue; the header is the card's title."

### MINOR 3 — A handout with zero verses shows a tag-specific message

- **Claim.** Error table: "Handout with zero verses | card screen says `STR_PREACHING_TOPIC_EMPTY`
  | existing branch".
- **Problem.** That string reads "No Bible verses have this tag", which is wrong for a printed
  card.
- **Evidence.**
  - `english.yaml:493`.
  - `PreachingTopicActivity.cpp:275-276` draws it for any empty `cards`.
  - It is reachable only through a hand-edited file, or one whose every href is over 63 B. The
    generator refuses an empty card (plan, `test_a_page_with_no_links_and_no_articles_fails`).
- **Fix.** Either accept it and say so in the table ("tag wording, unreachable from the
  generator"), or have `activateIndex` refuse a row whose `Summary::verses == 0` with the
  `STR_PREACHING_CARDS_UNREADABLE` toast. The first option costs nothing and is enough.

VERDICT: CLEAR
