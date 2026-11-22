Tier: standard

# PR review 0 — berean-o-26, the Tarjetas tab

Range: `origin/batch/20261122-tarjetas-preaching-cards-in-predicaci-n-65ka...origin/feature/preaching-tarjetas-tab`
(head `c9a0df85`). Read against the brief (`hpipe brief --task t2`), the spec
`docs/superpowers/specs/2026-11-22-berean-o-26-design.md` and the plan
`docs/superpowers/plans/2026-11-22-berean-o-26-plan.md`. No empty reply commits from the worker on
the branch.

Checks the reviewer ran on the head:

- Host suite (`cmake -S test`, fresh build dir): `100% tests passed out of 1892`, including the two
  new `ReaderEntryIntent` cases.
- `pio run -e x4pro` with the preaching sources recompiled: `SUCCESS`, no `warning:` lines from
  `src/`; Flash 5,616,774 / 6,553,600 B (85.7 %).
- Not re-run here: `pio check`, `pio run -e x4pro-gh_release`, `./bin/clang-format-fix` (the last
  commit is the plan's format step).

## Intent

Every acceptance criterion in the brief is met in code:

- **Default tab and rows.** `Tab::Handouts` is enum value 0 and the default
  (`PreachingActivity.h:25,62`); `onEnter` loads the summaries before the first `rebuildRows`
  (`PreachingActivity.cpp:50`); one row per summary with its verse count as the value, label and
  value pointers into `session.handouts()` and a reserved `rowValues`, built under the caller's
  `RenderLock` (`:93-106`).
- **Notices.** Empty list + `Missing`/`Ok` → `STR_PREACHING_NO_CARDS`; `Unreadable`/`TooNew`/
  `OutOfMemory` → `STR_PREACHING_CARDS_UNREADABLE` (`:203-214`), matching spec A6/A7. Preguntas and
  Mis temas branches are untouched, so they work with or without the file.
- **Three-way stepping.** `stepTab` honours the direction modulo `Tab::Count` (`:170-179`);
  `UiTabListActivity.cpp:70-71` passes ±1, Confirm on the tab bar passes +1 (`:188`).
- **Opening a handout.** `activateIndex` → `openHandout` → `loadHandout` → `pushTopic`, the twin of
  `openTag` (`:244-247,348-361`). `loadHandout` emits a Heading card on each section change only
  for a non-empty heading, then Verse cards viewing `TopicCards::handout`, with `verseSource` the
  index into `handout.verses` (`PreachingContent.cpp:105-136`). No NextVisit card. Sections are
  monotone and every `Verse::section` names a pushed heading (`PreachingHandouts.cpp`, `load`), so
  `headings[verse.section]` is in bounds.
- **Reference tap and return.** `openBible` gives a handout verse the question's `openHref` +
  `resolveHrefToSpineIndex` path (`PreachingTopicActivity.cpp:217-228`), so a handout no longer
  falls into the tag branch and indexes an empty `passages`. `originAt` maps to `ofHandout`
  (`:171`); the reader copies the origin unchanged (`ReaderEntryIntent.h:49-55`) and
  `goToPreaching` rebuilds Predicación with it; `onEnter` keeps Tarjetas, `seedSelection` selects
  the row and `openHandout(origin.handout, origin.verse)` reopens at the verse
  (`PreachingActivity.cpp:58-61,145-147`). The two `static_assert`s pin the href and index bounds
  the round trip depends on (`PreachingTopicActivity.cpp:29-30`).
- **Question and Tag origins keep their tab.** A Question origin now sets `Tab::Questions`
  explicitly (`PreachingActivity.cpp:47-48`), as the brief's pointer required; Tag is unchanged.
  `onTopicResult` gains the Handout branch (`:386-389`).
- **Strings.** The three keys exist in English and Spanish; the Spanish uses File Transfer's own
  label (`spanish.yaml:12` "Transferir archivos").

Scope: no change under `src/study/PreachingHandouts.*`, `src/activities/reader/` or the Home card.
The only addition beyond the parent plan's Tasks 6–8 is the USER_GUIDE §11 edit, which the spec
justifies (A12) and the plan carries as Task 5; it keeps the shipped guide true rather than
expanding scope.

Plan conformance: the code reproduces the plan's blocks verbatim (onEnter, tabLabel, rebuildRows,
seedSelection, showTab, stepTab, notice, activateIndex, openHandout, onTopicResult, loadHandout,
failureMessage, static_asserts). No unexplained divergence.

Tests: the new host cases (`ReaderEntryIntentTest.cpp:88-105`) cover `ofHandout`'s clamping and the
origin's trip through `openHref`, which is the contract the reader round trip rests on. The heading
logic in `loadHandout`, the notices and `stepTab` have no host test; the spec records why
(`PreachingContent`/`PreachingActivity` are not built by any host suite and a seam would be a new
pattern) and routes them to the device checklist. That is a stated, reviewed decision, not a
silent gap.

## Quality

The change mirrors the Mis temas (`Kind::Tag`) code throughout: `ensureHandouts` is
`ensureCounts`'s load-once shape, `openHandout` is `openTag`'s, `ofHandout` is `ofTag`'s, and the
notice is the Mis temas notice with three lines instead of two. Naming follows the brief's
"handout" convention in code and "Tarjetas"/"Cards" only in UI text. Allocation goes through
`makeUniqueNoThrow` with a `LOG_ERR` on null; failures report through the existing
`reportOpenFailure`/`failureMessage` path; vectors are reserved before their push loops. Comments
added are the class/struct descriptions updated for the third kind, not narration. No dead or
commented-out code. Tab stepping uses the same inline modulo idiom as
`TextSettingsActivity::stepTab` (`TextSettingsActivity.cpp:491-493`), so it is not a second way of
doing it.

## Findings

1. **MINOR** — `USER_GUIDE.md:359-360`: "A card shows every verse its article cites, in article
   order, under the article's headings when it has more than one." The paragraph it ends uses
   "card" for the on-screen layout cards ("opens as cards", "A verse card is the quote"), so "A
   card" here reads as a layout card, not a printed card from the **Cards** tab, and the sentence
   says nothing a tester can check against a heading-less single-article handout. Suggest "A card
   from **Cards** shows every verse its article cites, in article order, under the article's
   headings when it has more than one; its title is the header."

VERDICT: CLEAR
