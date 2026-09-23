# Cross-references between passages — design note for issue #33

User-decided defaults: directed links (A → B) stored on the source passage as the
target's `study::Unit`; a `PassageDoc` format bump that older builds refuse; a hard
per-passage cap with a budget argument; a two-step "mark, then link to marked" in the
existing passage action menu; following a link through `StudyStore`'s locate path
with an honest "not found in this publication". No backlinks, no bidirectional sync.
This note records the smaller choices those defaults left open.

## The record

`study::PassageLink` (`lib/StudyStore/StudyStore/TaggedPassage.h`) is
`{Unit target, uint16_t targetSpine, std::string label}`, held in
`TaggedPassage::links`.

- **Unit, not index.** The link names the target's start unit, so it survives the
  target being deleted and re-added and, for a Verse unit, an edition change.
- **A spine hint beside it.** A Unit alone cannot find a Paragraph or DocumentOffset
  target: `documentOffsetOf` accepts a DocumentOffset unit in *any* document
  (`UnitAnchors.cpp:114`), and a data-pid is unique only within its document. This is
  the same Unit-plus-hint split `TaggedPassage` already uses (`start` +
  `documentSpine`).
- **A label.** The target's reference, or its snippet when it has none, cut to
  `MAX_REFERENCE_BYTES` with `utf8SafeSummary`. Without it, a link whose target passage
  was deleted would have nothing to show but a compact address.

## On-disk format: v1 → v2, written only when it has to be

JSON key `"k"` on a passage row: `[{"u": "<compact unit>", "s": <spine>, "r": "<label>"}]`.

**What a v1 build does with it.** Every v1 build reads a row field by field and
ignores keys it does not know (`PassageDoc.cpp:106-139` on main). Left at `"v":1`, a
v1 build would load the file, drop every link, and its next tag edit would save the
file without them. So links need a version v1 refuses.

**How v1 refuses `"v":2`.** `PassageDoc::fromJson` rejects `version > FORMAT_VERSION`
(`PassageDoc.cpp:109` on main). `PassageFile::load` turns that into
`LoadResult::Failed` for both the primary file and a promoted `.tmp`
(`PassageFile.cpp:73-75`, `:86-88`). From there:

- the reader: `StudyStore::openPublication` latches `saveDisabled_` for the session
  (`StudyStore.cpp:45-47`), every mutating path refuses while it is set, and the reader
  shows `STR_HIGHLIGHTS_LOAD_FAILED` (`EpubReaderActivity.cpp:248-250`);
- migration: `MigrationRunner` skips the publication with "destination unreadable" and
  never saves over it (`MigrationRunner.cpp:277-283`).

The v1 build shows no passages for that publication, and the file is untouched.

**Why links-free files stay v1.** `toJson` writes `"v":2` only when some passage
carries a link, and `"v":1` otherwise (`PassageDoc::LINKLESS_FORMAT_VERSION`). A
links-free file holds nothing v1 would drop, so after an OTA rollback the user keeps
all their passages until they actually make a link. The host test
`PassageDocLinks.WritesVersionOneWhileNoPassageCarriesALink` pins this. Removing the
last link writes v1 again, which is correct for the same reason.

**The load path refuses `"k"` rather than repairing it** (`linksFromJson` in
`PassageDoc.cpp`). Any of these makes `fromJson` return false:

- an unparseable target;
- a missing spine, or one over 65,535;
- a label longer than `MAX_REFERENCE_BYTES`;
- more than `MAX_LINKS_PER_PASSAGE` links;
- a duplicate link;
- a link to the passage itself.

Repairing would drop or cut a link, and the next save would make the loss permanent.
A refusal becomes `LoadResult::Failed`, then `saveDisabled_`, and the file is left
untouched. Both limits are therefore part of the format: widening either one needs a
`FORMAT_VERSION` bump (pinned in `PassageDoc.h`). The in-memory paths (`add`,
`setLinks`) still normalise, because nothing there is on the card yet.

**Link identity includes the document for non-Verse units.** `Unit::operator==`
carries no spine. Two Watchtower articles can each have a pid-5 paragraph, and their
Units are equal. For Paragraph and DocumentOffset units, the self-link and duplicate
checks therefore compare `(unit, spine)`; a Verse unit compares by address alone
(`samePlace` in `PassageDoc.cpp`).

## Cap and budget

`MAX_LINKS_PER_PASSAGE = 8`, the same figure as `MAX_TAGS_PER_PASSAGE`.

- **Absolute worst case.** Every field is at its maximum, every string is made of `"`
  (so each byte serialises as two), and all 8 link slots are full: **1,669 bytes**.
  `SAVE_BYTE_BUDGET` (200,000) holds 119 of those.
  `PassageDocBudget.AFullyLinkedWorstCasePassageCannotExhaustTheBudgetInOrdinaryUse`
  asserts < 1,700 bytes and ≥ 100 per budget.
- **The user's real store.** 63 passages, the figure `HoldsTheUsersRealStoreWithRoomToSpare`
  already uses. With every one of them linked to 8 others, it measures **36,841 bytes**,
  18% of the budget.
  `PassageDocBudget.TheUsersRealStoreFullyLinkedUsesUnderAQuarterOfTheBudget` asserts
  under 25%.

The cap is what makes that argument hold: without it, one passage's link list is
unbounded. `linkPassages` still measures after appending and refuses with `OverBudget`
rather than truncate. The UI refuses at the cap with `STR_LINK_LIMIT_REACHED`.

Unlinking is included, although the issue does not ask for it. Without it, a passage
at the cap could never take another link. A long-press on a row in the links list
removes that link after a confirmation.

## Making a link

The menu is the long-press chooser in `HighlightsActivity`. Its rows come from
`PassageActions::menuFor` (`src/activities/reader/PassageActions.h`, host-tested in
`test/tag_rows/PassageActionsTest.cpp`):

`Tags…` · `Links…` (only if the passage has links) · `Link to marked` (only if
another passage is marked) · `Mark as link source` (unless this passage is the marked
one) · `Delete` · `Cancel`. That is at most 6 rows, against `OptionPopup::MAX_OPTIONS = 16`.

**Where the pending source lives.** `StudyStore::linkSource_`, an
`std::optional<size_t>` in RAM. It is never persisted. It clears when:

- the publication closes (`closePublication`). Links join two passages of the one
  open study file, and the source's file is not loaded once another publication opens;
- any passage is removed (`removePassage`). Removal shifts the indices, so a held
  index could name a different passage.

Linking does not clear it, so one source can be linked to several targets in a row.
Marking another passage replaces it.

**Cross-publication links are not offered.** Because the mark clears on close,
"Link to marked" only ever appears inside the publication the source belongs to.
Allowing it across publications would mean writing a study file that is not open,
which is a new cross-cutting write path this issue does not justify. Bible passages
share one language-free pubkey, so a link made in one edition still resolves in
another.

## Following a link

`Links…` pushes `PassageLinksActivity`. Tapping a row calls
`StudyStore::locateLink`. If it resolves, the activity returns the same
`ProgressChangeResult` a highlights-row tap returns. `HighlightsActivity` hands it up
unchanged, and the reader's existing offset-jump branch (`EpubReaderActivity::openHighlights`)
applies it. `EpubReaderActivity.cpp` is not touched.

`locate` and `locateLink` share one resolver (`StudyStore::locateUnit`): the hint
first, then, for a Verse unit only, a search within that book. `locateLink` adds one
guard `locate` did not need. Because `documentOffsetOf` accepts a DocumentOffset unit
anywhere, a DocumentOffset target is refused in three cases: its hint is past the
index's document count; it names a document of another kind (the sign of an edition
change); or the document could not be indexed (`UnitIndexCache::indexFailed`). That
last case returns a placeholder that looks exactly like a real DocumentOffset document.
Otherwise it would open arbitrary text. Anything unresolved shows `STR_LINK_TARGET_NOT_FOUND`
("Not found in this publication") and stays on the list. It never falls back to
opening the hinted document, which is what a passage jump does and would be a
misleading claim for a link.

A dangling link, whose target passage has been deleted, still resolves whenever its
address does, because it points at a place, not a passage.

## Messages

Every notice here goes through `ReaderUtils::showMessage`, which since #78 posts to
`PostedMessage` for the next completed render. Only `UiListActivity::render` and
`ReaderActivity` drew that queue. `HighlightsActivity` and `PassageLinksActivity`
override `render()`, so both now call `PostedMessage::drawNext` after their own
`displayBuffer()`. Without that call, "Linked" or "Not found in this publication"
would appear late, over the reader page. `TagFilterActivity::render` had the same gap,
and this change closes it too.

## Deliberately not done

- No backlinks index and no bidirectional sync (issue scope).
- A followed link does not push the reader's `ReturnStack`. Highlight jumps do not
  either, and doing so would mean changing `EpubReaderActivity.cpp`.
