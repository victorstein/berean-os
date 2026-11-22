# Tarjetas tab (berean-o-26) Implementation Plan

**Goal:** Predicación gains a first, default **Tarjetas** tab listing the owner's printed preaching
cards from `/preaching-cards.json`; each opens in the existing card screen, and the reader's swipe
back returns to the same card.

**Spec:** `docs/superpowers/specs/2026-11-22-berean-o-26-design.md` — read it first; its
assumptions A1–A13 are the behaviour this plan implements. Parent plan:
`docs/superpowers/plans/2026-11-22-preaching-cards.md` Tasks 6–9, whose code this plan reproduces.

**Model file:** the Mis temas tab (`Kind::Tag`) — `PreachingOrigin::ofTag`,
`PreachingContent::loadTag`, `PreachingSession::ensureCounts`, and the Tag branches of
`PreachingActivity.cpp`. Every change below is that code's sibling.

**Naming:** in code a printed card is a **handout** (`Kind::Handout`, `loadHandout`,
`Tab::Handouts`). "Card" already means a layout card (`PreachingCardLayout.h`). UI text is
"Cards" / "Tarjetas".

**Code style (also for any subagent):** names carry the meaning; no comments that restate code,
only a non-obvious *why*. Follow repo `CLAUDE.md`: `makeUniqueNoThrow`, `LOG_*`, `tr()`,
`.reserve()` before push loops, row `c_str()` pointers kept stable under the `RenderLock`. Never
add Claude/assistant attribution to commits. **Commit after each task; never push.**

**Environment:** run everything from
`/Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-tarjetas-tab`. `pio` is
`/Volumes/stein/.platformio/penv/bin/pio`. The bootstrap has run (`freeink-sdk/` populated,
`.venv/bin/clang-format` present). `ninja` is not installed: configure CMake with its default
generator. Re-read each file before editing it — a sibling task may have changed it.

FILES: test/ui_layout/ReaderEntryIntentTest.cpp, src/activities/preaching/PreachingOrigin.h
FILES: lib/I18n/translations/english.yaml, lib/I18n/translations/spanish.yaml
FILES: src/activities/preaching/PreachingContent.h, src/activities/preaching/PreachingContent.cpp
FILES: src/activities/preaching/PreachingTopicActivity.cpp
FILES: src/activities/preaching/PreachingSession.h, src/activities/preaching/PreachingSession.cpp
FILES: src/activities/preaching/PreachingActivity.h, src/activities/preaching/PreachingActivity.cpp
FILES: USER_GUIDE.md

Out of bounds: `src/study/PreachingHandouts.*`, `src/activities/reader/*` (read only), the Home
card, `test/CMakeLists.txt`.

---

## Task 1: `PreachingOrigin::ofHandout` (TDD)

**Files:** `test/ui_layout/ReaderEntryIntentTest.cpp`, `src/activities/preaching/PreachingOrigin.h`

- [ ] **Step 1: Write the failing tests.** Append to the end of
  `test/ui_layout/ReaderEntryIntentTest.cpp`:

```cpp
TEST(ReaderEntryIntent, HandoutOriginsKeepTheirIndexAndVerse) {
  const PreachingOrigin handout = PreachingOrigin::ofHandout(7, 4);
  EXPECT_EQ(handout.kind, PreachingOrigin::Kind::Handout);
  EXPECT_EQ(handout.handout, 7);
  EXPECT_EQ(handout.verse, 4);
  EXPECT_TRUE(handout.isSet());
  EXPECT_EQ(PreachingOrigin::ofHandout(0, -1).verse, 0);
}

TEST(ReaderEntryIntent, OpenHrefCarriesAHandoutOrigin) {
  ReaderEntryIntent intent;
  ASSERT_TRUE(ReaderEntryIntent::openHref("bi12_S_19.xhtml#chapter23_verse4", PreachingOrigin::ofHandout(5, 76), intent));
  EXPECT_EQ(intent.kind, Kind::OpenHref);
  EXPECT_EQ(intent.origin.kind, PreachingOrigin::Kind::Handout);
  EXPECT_EQ(intent.origin.handout, 5);
  EXPECT_EQ(intent.origin.verse, 76);
}
```

- [ ] **Step 2: Run it and see it fail.**

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target ReaderEntryIntentTest -j
```

Expected: compile error, `no member named 'ofHandout' in 'PreachingOrigin'` (and `Handout` not in
`Kind`).

- [ ] **Step 3: Implement.** Replace the whole of `src/activities/preaching/PreachingOrigin.h` with:

```cpp
#pragma once

#include <cstdint>

// Where in Predicación a screen, or the reader, came from: a question, a tag topic or a handout,
// and the Verse card within it. RAM only, and free of Arduino and StudyStore so test/ui_layout
// builds it.
struct PreachingOrigin {
  enum class Kind : uint8_t { None, Question, Tag, Handout };

  Kind kind = Kind::None;
  uint8_t question = 0;  // 1..IntroSection::QUESTION_COUNT when kind == Question
  uint16_t tag = 0;      // the raw study::TagId when kind == Tag
  uint8_t handout = 0;   // index in /preaching-cards.json when kind == Handout
  uint16_t verse = 0;    // ordinal among the topic's Verse cards

  static constexpr PreachingOrigin ofQuestion(const int question, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Question;
    origin.question = static_cast<uint8_t>(question);
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  static constexpr PreachingOrigin ofTag(const uint16_t tag, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Tag;
    origin.tag = tag;
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  static constexpr PreachingOrigin ofHandout(const int handout, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Handout;
    origin.handout = static_cast<uint8_t>(handout);
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  constexpr bool isSet() const { return kind != Kind::None; }
};
```

- [ ] **Step 4: Run it and see it pass.**

```bash
cmake --build build/test --target ReaderEntryIntentTest -j
ctest --test-dir build/test -R ReaderEntryIntent --output-on-failure
```

Expected: all `ReaderEntryIntent.*` tests pass, including the two new ones.

- [ ] **Step 5: Commit.**

```bash
git add test/ui_layout/ReaderEntryIntentTest.cpp src/activities/preaching/PreachingOrigin.h
git commit -m "feat(preaching): add a handout origin for the reader round trip"
```

---

## Task 2: Strings

**Files:** `lib/I18n/translations/english.yaml`, `lib/I18n/translations/spanish.yaml`

- [ ] **Step 1: English.** In `english.yaml`, directly after the line
  `STR_PREACHING_SAVE_FAILED: "Couldn't save your topics"`, insert:

```yaml
STR_PREACHING_CARDS: "Cards"
STR_PREACHING_NO_CARDS: "No cards yet. Upload preaching-cards.json to the SD card's root with File Transfer."
STR_PREACHING_CARDS_UNREADABLE: "Can't read preaching-cards.json"
```

- [ ] **Step 2: Spanish.** In `spanish.yaml`, directly after
  `STR_PREACHING_SAVE_FAILED: "No se pudieron guardar tus temas"`, insert ("Transferir archivos" is
  `STR_FILE_TRANSFER` at `spanish.yaml:12`):

```yaml
STR_PREACHING_CARDS: "Tarjetas"
STR_PREACHING_NO_CARDS: "Aún no hay tarjetas. Sube preaching-cards.json a la raíz de la tarjeta SD con Transferir archivos."
STR_PREACHING_CARDS_UNREADABLE: "No se puede leer preaching-cards.json"
```

- [ ] **Step 3: Regenerate and check.**

```bash
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
grep -c 'STR_PREACHING_CARDS\b\|STR_PREACHING_NO_CARDS\|STR_PREACHING_CARDS_UNREADABLE' lib/I18n/I18nKeys.h
```

Expected: the script exits 0; the grep prints `3`. The generated headers are gitignored — do not
stage them.

- [ ] **Step 4: Commit.**

```bash
git add lib/I18n/translations/english.yaml lib/I18n/translations/spanish.yaml
git commit -m "feat(preaching): add the Tarjetas strings"
```

---

## Task 3: A handout as topic cards

**Files:** `src/activities/preaching/PreachingContent.h`, `PreachingContent.cpp`,
`PreachingTopicActivity.cpp`. No host suite builds these (spec, Testing strategy); the check is
the firmware build.

- [ ] **Step 1: `TopicCards` holds a handout.** In `PreachingContent.h`:

  Add `#include "study/PreachingHandouts.h"` directly after `#include "study/PreachingPassages.h"`.

  Replace the struct's comment and members up to `verseSource` so the top of the file's struct
  reads:

```cpp
// One topic's cards: a question read from the Bible, the Bible passages carrying a tag, or a handout.
// `cards` views into `parsed`, `passages`, `handout` and the session's question labels, so a
// TopicCards is never copied or moved -- moving a short std::string relocates its bytes -- and
// lives behind a std::unique_ptr; replacing a topic swaps pointers.
struct TopicCards {
  PreachingOrigin::Kind kind = PreachingOrigin::Kind::None;
  int question = 0;
  study::TagId tag{};
  std::string header;
  preaching::Question parsed;
  std::vector<PreachingPassages::Passage> passages;
  int handoutIndex = 0;
  PreachingHandouts::Handout handout;
  std::vector<preaching::CardInput> cards;
  // Per Verse ordinal: its block in `parsed` (a question), its passage (a tag) or its verse in
  // `handout`.
  std::vector<uint16_t> verseSource;
```

  (the deleted copy/move members and `verseCount()` below stay as they are).

  Replace the `LoadResult` line and add the declaration after `loadTag`:

```cpp
enum class LoadResult : uint8_t { Ok, Unreadable, OutOfMemory, VersesUnavailable, HandoutsUnavailable };
```

```cpp
LoadResult loadHandout(int handout, TopicCards& out);
```

- [ ] **Step 2: `loadHandout` and its message.** In `PreachingContent.cpp`, insert after the
  closing `}` of `loadTag`:

```cpp
LoadResult loadHandout(const int handout, TopicCards& out) {
  if (handout < 0) return LoadResult::Unreadable;
  const PreachingHandouts::Status status =
      PreachingHandouts::load(static_cast<size_t>(handout), PsramJsonAllocator::passageDoc(), out.handout);
  switch (status) {
    case PreachingHandouts::Status::Ok:
      break;
    case PreachingHandouts::Status::OutOfMemory:
      return LoadResult::OutOfMemory;
    case PreachingHandouts::Status::Missing:
    case PreachingHandouts::Status::Unreadable:
    case PreachingHandouts::Status::TooNew:
      return LoadResult::HandoutsUnavailable;
  }

  out.kind = PreachingOrigin::Kind::Handout;
  out.handoutIndex = handout;
  out.header = out.handout.title;
  const auto& verses = out.handout.verses;
  out.cards.reserve(verses.size() + out.handout.headings.size());
  out.verseSource.reserve(verses.size());
  int section = -1;
  for (size_t index = 0; index < verses.size(); ++index) {
    const PreachingHandouts::Verse& verse = verses[index];
    if (static_cast<int>(verse.section) != section) {
      section = verse.section;
      const std::string& heading = out.handout.headings[verse.section];
      if (!heading.empty()) out.cards.push_back({preaching::CardKind::Heading, heading, {}});
    }
    out.cards.push_back({preaching::CardKind::Verse, verse.text.view(), verse.reference});
    out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  return LoadResult::Ok;
}
```

  Replace the whole `failureMessage` function with:

```cpp
const char* failureMessage(const LoadResult result) {
  switch (result) {
    case LoadResult::VersesUnavailable:
      return tr(STR_PREACHING_VERSES_UNREADABLE);
    case LoadResult::HandoutsUnavailable:
      return tr(STR_PREACHING_CARDS_UNREADABLE);
    default:
      return tr(STR_PREACHING_OPEN_FAILED);
  }
}
```

- [ ] **Step 3: The card screen maps a handout.** In `PreachingTopicActivity.cpp`:

  Add `#include <cstdint>` to the `<...>` includes (alphabetical, before `<cstdio>`), and
  `#include "activities/reader/ReaderEntryIntent.h"` to the quoted includes (after
  `"activities/PostedMessage.h"`).

  Directly after the `namespace fui = freeink::ui;` line, insert:

```cpp
static_assert(PreachingHandouts::MAX_HREF_BYTES < ReaderEntryIntent::HREF_BYTES, "a handout href fits the intent");
static_assert(PreachingHandouts::MAX_HANDOUTS - 1 <= UINT8_MAX, "a handout index fits PreachingOrigin");
```

  Replace `originAt` with:

```cpp
PreachingOrigin PreachingTopicActivity::originAt(const int verse) const {
  if (topic->kind == PreachingOrigin::Kind::Question) return PreachingOrigin::ofQuestion(topic->question, verse);
  if (topic->kind == PreachingOrigin::Kind::Tag) return PreachingOrigin::ofTag(study::toRaw(topic->tag), verse);
  if (topic->kind == PreachingOrigin::Kind::Handout) return PreachingOrigin::ofHandout(topic->handoutIndex, verse);
  return {};
}
```

  Replace `openBible` with (a handout verse takes the question's href path; only a tag passage
  keeps `OpenAt`):

```cpp
void PreachingTopicActivity::openBible(const int verse) {
  if (verse < 0 || verse >= topic->verseCount()) return;
  const size_t source = topic->verseSource[static_cast<size_t>(verse)];
  if (!session.hasBible()) {
    LOG_ERR(MODULE, "No Bible to open verse %d in", verse);
    reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
    return;
  }

  ReaderEntryIntent intent;
  const std::string* href = nullptr;
  if (topic->kind == PreachingOrigin::Kind::Question) href = &topic->parsed.blocks[source].href;
  if (topic->kind == PreachingOrigin::Kind::Handout) href = &topic->handout.verses[source].href;
  if (href) {
    [[maybe_unused]] const unsigned long started = millis();
    const bool resolves = ReaderEntryIntent::openHref(*href, originAt(verse), intent) &&
                          session.book()->resolveHrefToSpineIndex(*href) >= 0;
    LOG_DBG(MODULE, "Reference check: %lu ms", millis() - started);
    if (!resolves) {
      LOG_ERR(MODULE, "Reference not opened: \"%s\"", href->c_str());
      reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
      return;
    }
  } else {
    const PreachingPassages::Passage& passage = topic->passages[source];
    intent = ReaderEntryIntent::of(ReaderEntryIntent::Kind::OpenAt);
    intent.unit = passage.start;
    intent.spineHint = passage.spine;
    intent.origin = originAt(verse);
  }
  app.clearTapFlash();
  activityManager.goToReader(session.biblePath(), false, intent);
}
```

  Before replacing, compare the current `openBible` with the block above apart from the `href`
  pointer: if a sibling has changed anything else in it, keep their change.

- [ ] **Step 4: Build.**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/berean-o-26-build.log | tail -5
grep -n 'warning:' build/berean-o-26-build.log | grep -E 'preaching/|PreachingHandouts' || echo "no warnings in touched files"
```

Expected: `SUCCESS`; `no warnings in touched files`. The first build in this worktree takes 3–4
minutes.

- [ ] **Step 5: Commit.**

```bash
git add src/activities/preaching/PreachingContent.h src/activities/preaching/PreachingContent.cpp src/activities/preaching/PreachingTopicActivity.cpp
git commit -m "feat(preaching): open a preaching card as verse cards"
```

---

## Task 4: The Tarjetas tab

**Files:** `src/activities/preaching/PreachingSession.{h,cpp}`, `PreachingActivity.{h,cpp}`.

- [ ] **Step 1: The session holds the summaries.** In `PreachingSession.h`:

  Add `#include "study/PreachingHandouts.h"` directly after `#include "study/PreachingPassages.h"`.

  In the class comment, change `the picked\n// topics; and the Mis temas counts once made.` so the
  list reads `...; the picked topics; the handouts; and the Mis temas counts once made.` (rewrap to
  the file's 100-column width).

  After the `count(...)` declaration in `public:`, add:

```cpp

  // Read the first time Tarjetas shows; held until Predicación exits.
  void ensureHandouts();
  PreachingHandouts::Status handoutsStatus() const { return handoutStatus; }
  const std::vector<PreachingHandouts::Summary>& handouts() const { return handoutSummaries; }
```

  After the `counts` member in `private:`, add:

```cpp

  bool handoutsLoaded = false;
  PreachingHandouts::Status handoutStatus = PreachingHandouts::Status::Missing;
  std::vector<PreachingHandouts::Summary> handoutSummaries;
```

  At the end of `PreachingSession.cpp`, append:

```cpp

void PreachingSession::ensureHandouts() {
  if (handoutsLoaded) return;
  handoutsLoaded = true;
  handoutStatus = PreachingHandouts::summaries(PsramJsonAllocator::json(), handoutSummaries);
}
```

- [ ] **Step 2: The activity's declarations.** In `PreachingActivity.h`:

  Replace the class comment's first two lines with:

```cpp
// Predicación: the owner's printed cards (Tarjetas), the Bible's twenty preaching questions
// (Preguntas) and the owner's picked tags (Mis temas). Opening one pushes PreachingTopicActivity.
// Opened from Home with no origin, or rebuilt from the reader's origin with the topic pushed again
// at its card.
```

  (this replaces all three existing comment lines.)

  Change the enum and default:

```cpp
  enum class Tab : uint8_t { Handouts, Questions, Topics, Count };
```

```cpp
  Tab tab = Tab::Handouts;
```

  After `bool openTag(study::TagId tag, int verse);` add:

```cpp
  bool openHandout(int handout, int verse);
```

- [ ] **Step 3: Enter on the right tab.** In `PreachingActivity.cpp`, replace `onEnter` with:

```cpp
void PreachingActivity::onEnter() {
  session.open(renderer);
  chooseTopicsLabel = std::string("+ ") + tr(STR_PREACHING_CHOOSE_TOPICS);
  if (origin.kind == PreachingOrigin::Kind::Tag) {
    // Only the picks: the counts wait until Mis temas is shown, so a return from the Bible reads
    // bible.json once, for the topic.
    session.ensureTopics();
    tab = Tab::Topics;
  } else if (origin.kind == PreachingOrigin::Kind::Question) {
    tab = Tab::Questions;
  }
  if (tab == Tab::Handouts) session.ensureHandouts();
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  UiTabListActivity::onEnter();
  seedSelection();

  if (origin.kind == PreachingOrigin::Kind::Question) {
    openQuestion(origin.question, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Handout) {
    openHandout(origin.handout, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Tag && !openTag(study::toTagId(origin.tag), origin.verse)) {
    // No topic result will come to run the deferred counts.
    session.ensureCounts();
    RenderLock lock(*this);
    rebuildRows();
  }
}
```

- [ ] **Step 4: Label, rows, selection, tab switching.** Replace `tabLabel` with:

```cpp
const char* PreachingActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::Handouts:
      return tr(STR_PREACHING_CARDS);
    case Tab::Questions:
      return tr(STR_PREACHING_QUESTIONS);
    default:
      return tr(STR_PREACHING_MY_TOPICS);
  }
}
```

  In `rebuildRows`, directly after `rowValues.clear();`, insert:

```cpp
  if (tab == Tab::Handouts) {
    const auto& handouts = session.handouts();
    rowValues.reserve(handouts.size());
    rowItems.reserve(handouts.size());
    for (const auto& handout : handouts) rowValues.push_back(std::to_string(handout.verses));
    for (size_t row = 0; row < handouts.size(); ++row) {
      fui::ListItem item;
      item.label = handouts[row].title.c_str();
      item.value = rowValues[row].c_str();
      item.actionValue = static_cast<int16_t>(row);
      rowItems.push_back(item);
    }
    return;
  }
```

  Replace `seedSelection` with:

```cpp
void PreachingActivity::seedSelection() {
  int row = 0;
  if (origin.kind == PreachingOrigin::Kind::Question && origin.question >= 1 && origin.question <= QUESTION_COUNT) {
    row = origin.question - 1;
  } else if (origin.kind == PreachingOrigin::Kind::Tag) {
    const int tagRow = session.topicRow(study::toTagId(origin.tag));
    row = tagRow >= 0 ? tagRow : 0;
  } else if (origin.kind == PreachingOrigin::Kind::Handout) {
    row = origin.handout;
  }
  auto& nav = activeNav();
  nav.selected = listCount() > row ? row + 1 : 0;
  nav.followOnBuild = true;
}
```

  Replace `showTab` with:

```cpp
void PreachingActivity::showTab(const Tab next) {
  if (next == Tab::Handouts) session.ensureHandouts();
  if (next == Tab::Topics) session.ensureCounts();
  RenderLock lock(*this);
  tab = next;
  rebuildRows();
}
```

  Replace `stepTab` with (`UiTabListActivity.cpp:70-71` passes `+1` / `-1` on a held Right / Left):

```cpp
void PreachingActivity::stepTab(const int direction) {
  const bool onTabBar = ringPos() == 0;
  constexpr int TAB_COUNT = static_cast<int>(Tab::Count);
  const int step = direction < 0 ? TAB_COUNT - 1 : 1;
  showTab(static_cast<Tab>((static_cast<int>(tab) + step) % TAB_COUNT));
  auto& nav = activeNav();
  nav.selected = onTabBar || listCount() == 0 ? 0 : 1;
  nav.followOnBuild = true;
  requestUpdate();
}
```

- [ ] **Step 5: The notices.** In `buildScreen`, directly after `buildTabBar(screen);`, insert:

```cpp

  if (tab == Tab::Handouts && session.handouts().empty()) {
    const PreachingHandouts::Status status = session.handoutsStatus();
    const bool absent = status == PreachingHandouts::Status::Missing || status == PreachingHandouts::Status::Ok;
    fui::TextStyle style = screen.theme().bodyText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 3;
    const int lineHeight = screen.target().lineHeight(style.font);
    const fui::Rect notice =
        screen.takeTop(static_cast<int16_t>(3 * lineHeight), static_cast<int16_t>(metrics.verticalSpacing));
    screen.target().text(notice, absent ? tr(STR_PREACHING_NO_CARDS) : tr(STR_PREACHING_CARDS_UNREADABLE), style);
    return;
  }
```

- [ ] **Step 6: Open a handout, and come back to it.** Replace `activateIndex` with:

```cpp
void PreachingActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (tab == Tab::Handouts) {
    openHandout(index, NO_VERSE);
    return;
  }
  if (tab == Tab::Questions) {
    openQuestion(index + 1, NO_VERSE);
    return;
  }
  const auto& topics = session.topics();
  if (index == static_cast<int>(topics.size())) {
    openTopicPicker();
  } else if (index >= 0 && index < static_cast<int>(topics.size())) {
    openTag(topics[static_cast<size_t>(index)], NO_VERSE);
  }
}
```

  Insert after the closing `}` of `openTag`:

```cpp

bool PreachingActivity::openHandout(const int handout, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: handout %d", handout);
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return false;
  }
  const auto result = PreachingContent::loadHandout(handout, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return false;
  }
  return pushTopic(std::move(cards), verse);
}
```

  Replace `onTopicResult` with:

```cpp
void PreachingActivity::onTopicResult(const ActivityResult& result) {
  const auto* returned = std::get_if<PreachingResult>(&result.data);
  if (!returned) return;
  const PreachingOrigin& at = returned->at;
  if (at.kind == PreachingOrigin::Kind::Question && session.sectionFound() && at.question >= 1 &&
      at.question <= QUESTION_COUNT) {
    showTab(Tab::Questions);
    moveRingTo(at.question);
  } else if (at.kind == PreachingOrigin::Kind::Tag) {
    showTab(Tab::Topics);
    const int row = session.topicRow(study::toTagId(at.tag));
    moveRingTo(row >= 0 ? row + 1 : (listCount() > 0 ? 1 : 0));
  } else if (at.kind == PreachingOrigin::Kind::Handout) {
    showTab(Tab::Handouts);
    moveRingTo(at.handout < listCount() ? at.handout + 1 : (listCount() > 0 ? 1 : 0));
  }
}
```

- [ ] **Step 7: Build.**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/berean-o-26-build.log | tail -5
grep -n 'warning:' build/berean-o-26-build.log | grep -E 'preaching/|PreachingHandouts' || echo "no warnings in touched files"
```

Expected: `SUCCESS`; `no warnings in touched files`.

- [ ] **Step 8: Commit.**

```bash
git add src/activities/preaching/PreachingSession.h src/activities/preaching/PreachingSession.cpp src/activities/preaching/PreachingActivity.h src/activities/preaching/PreachingActivity.cpp
git commit -m "feat(preaching): add the Tarjetas tab, first in Predicación"
```

---

## Task 5: The user guide

**Files:** `USER_GUIDE.md` (§11, spec A12).

- [ ] **Step 1: Three tabs.** In `## 11. Preaching`, replace `It opens from its card on Home, with
  two tabs:` with `It opens from its card on Home, on the first of three tabs:` and insert this
  bullet before the `- **Questions**` bullet:

```markdown
- **Cards** — your printed preaching cards, read from `preaching-cards.json` at the root of the SD
  card (upload it with **File Transfer**), each with the number of its verses. Without the file,
  the tab says **No cards yet. Upload preaching-cards.json to the SD card's root with File
  Transfer.**; when the file can't be read, it says **Can't read preaching-cards.json**. The other
  tabs work either way.
```

- [ ] **Step 2: A card topic.** In `### A topic`, append this sentence to the end of the first
  paragraph (after `...the screen says **No Bible verses have this tag**.`):

```markdown
A card shows every verse its article cites, in article order, under the article's headings when it
has more than one.
```

  Rewrap the paragraph to the guide's ~100-column width.

- [ ] **Step 3: Commit.**

```bash
git add USER_GUIDE.md
git commit -m "docs: describe the Tarjetas tab in the user guide"
```

---

## Task 6: Whole-tree checks

- [ ] **Step 1: Host tests.**

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
```

Expected: `100% tests passed`.

- [ ] **Step 2: Static analysis.**

```bash
/Volumes/stein/.platformio/penv/bin/pio check -e x4pro 2>&1 | tee build/berean-o-26-check.log | tail -15
grep -nE 'src/activities/preaching/|PreachingHandouts' build/berean-o-26-check.log || echo "no defects in touched files"
```

Expected: `no defects in touched files`. A defect listed in a touched file that `git blame` shows
predates this branch is not new; report it instead of fixing it.

- [ ] **Step 3: Format the whole tree, as CI does.**

```bash
./bin/clang-format-fix
git status --short
```

Expected: exits 0. If it changed files:

```bash
git add -u
git commit -m "style: format the Tarjetas tab change"
```

- [ ] **Step 4: Release build.**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release 2>&1 | tail -8
```

Expected: `SUCCESS`; note the firmware size against the 6,553,600 B app partition.

- [ ] **Step 5: Stop.** Report the commits, host test counts, firmware size, and any check output.
  Do not push.

---

## Device checklist (the owner, after landing)

1. Before uploading the file: Predicación opens on **Tarjetas** with the "No cards yet" notice;
   Preguntas and Mis temas still work; **holding** Left/Right cycles all three tabs both ways (a
   short press only moves the selection).
2. Upload `preaching-cards.json` to the SD root over File Transfer; reopen Predicación: eight rows
   with verse counts.
3. **Menos estrés**: verse cards, no headings. **La salud mental**: article headings between verses.
4. Tap a quote → show mode; tap a reference → the Bible at that verse; left-edge swipe → back on
   the same card, Tarjetas selected on that row.
5. Open a question and a tag topic, go to the Bible and swipe back: each returns to its own tab.
6. Serial: `Handouts: N, X ms` and `Handout I: N verses, X ms`; free heap equal across
   Tarjetas → Bible → back.
7. A copy with `"v":2` → Tarjetas shows "Can't read preaching-cards.json"; the other tabs work.
