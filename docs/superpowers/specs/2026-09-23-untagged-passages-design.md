# Passages with no tags — design note for issue #32

Closes the open item in `docs/superpowers/specs/2026-09-13-berean-os-design.md:668-671`
("mark it, label it later"). The shape is the one the issue proposes: a reserved
`study::TagId` 0, named `study::UNLABELLED` (`lib/StudyStore/StudyStore/TagPalette.h`).

## The model

- `TagPalette` never allocates 0: `nextRaw_` starts at 1 and `fromJson` skips a
  stored id 0 (`TagPalette.cpp:87`). So `StudyStore::activeTags()` never yields it
  and it cannot collide with a user tag. No change there.
- `PassageDoc`'s `normaliseTags` is now the single rule: drop `UNLABELLED` from any
  list that has a real tag, and turn an empty list into `{UNLABELLED}`. A passage
  in memory therefore never has an empty tag list, and `UNLABELLED` never shares a
  list with a real tag, so it never spends one of the eight per-passage slots.
- `PassageDoc::add` no longer refuses an empty list; `setTags` with an empty list
  leaves the passage unlabelled instead of refusing; `removeTagEverywhere` leaves
  an orphaned passage unlabelled (it was already kept, just tagless) and is a
  no-op for `UNLABELLED` itself. `untaggedCount` became `unlabelledCount`; it had
  no caller outside its test.
- `StudyStore::passagesWithTag(UNLABELLED)` is the unlabelled view with no code
  change. `StudyStore::tagName(id)` is new: `tr(STR_TAG_UNLABELLED)` for the
  reserved id, the palette name otherwise; `tagNamesFor` goes through it, so an
  unlabelled row shows "Unlabeled" rather than a blank. `retireTag(UNLABELLED)`
  is refused.

The rollback shape in `addPassage` / `setPassageTags` / `removePassage` is
unchanged. `removePassage`'s rollback re-`add`s the backup, which used to fail
silently for a tagless passage; it now succeeds.

## On-disk format: no version bump — PassageDoc stays at v1

The question the issue raises is what an older build does with a tag id 0.
Every build since the store landed (`cf1faa11`, unchanged through `36d4e246` and
`main`) reads tags with

```cpp
if (id > 0 && id <= UINT16_MAX) p.tags.push_back(...);   // PassageDoc.cpp:119-122 on main
```

and keeps the passage whatever the result — `fromJson` has no "no tags" rejection
(`PassageDoc.cpp:99-132` on main). A tag-less passage is already a legal v1
record: `retireTag` produces one whenever it removes a passage's last tag
(`StudyStore.cpp:146-156` on main), and v1 writes it as `"t":[]`.

So this change writes `UNLABELLED` as the **empty array** (`PassageDoc::toJson`)
and reads both `[]` and a stray `[0]` back as `UNLABELLED`. The reserved id never
reaches the card. An older build reading a file this build wrote sees exactly the
record it already produces and handles itself: the passage is kept, painted, and
listed with an empty tag slot. Nothing is reinterpreted, so CLAUDE.md storage
rule 4 has nothing to refuse, and `PassageDoc::FORMAT_VERSION` stays **1**.

Had 0 been written as `[0]`, older builds would still have been safe (they drop
it and land on the same `[]` record), but the empty array needs no argument at
all. A host test (`PassageDocUnlabelled.SerialisesAsTheEmptyTagArrayEveryV1BuildAlreadyReads`)
pins both halves: `v` is 1 and the array is empty.

Consequence worth knowing for #33: passages retired to zero tags on an older build
now appear as unlabelled on this one. That is the intended meaning.

## The open UI questions

**Unlabelled in the highlights list.** An "Unlabeled" row in `TagFilterActivity`,
second after "All", rather than a separate section in `HighlightsActivity`. It
reuses the filter the screen already has (`HighlightsActivity::openTagFilter`),
costs one row, and needs no new screen. The row layout is a host-tested mapping
(`FilterRows` in `src/activities/reader/TagRowMapping.h`, beside the picker's
`TagRows`) so a long-press on "Unlabeled" can never reach the retire path.
`openTagFilter` no longer bails on an empty palette, since "Unlabeled" is always
a meaningful filter. `HighlightsActivity::dropRetiredFilter` skips the reserved id,
which `isActive` reports false for.

**Untag to zero.** It now leaves the passage unlabelled. Deletion stays one step
away on the same surface: a long-press on a passage in `HighlightsActivity` offers
Tags… / Delete / Cancel (`HighlightsActivity.cpp:212`), and Delete is confirmed
(`showDeleteConfirmation`). `applyTagEdit` now reports every failed save, since an
empty selection is no longer a silent refusal.

**Marking while reading.** The path already existed and was dead:
`PassageSelectActivity`'s chooser offers Highlight / Tag / Cancel, and Highlight
called `finalizeSelection` with no tags, which finished without saving because
`addPassage` refused an empty list. With that guard gone, Highlight marks the
passage unlabelled. In the Tag flow, Done with nothing checked is a deliberate
confirm and also saves unlabelled; backing out of the picker (Back / left-edge
swipe, `isCancelled`) abandons the mark and saves nothing, because Back is
cancel everywhere else and Highlight already is the explicit "mark without a
tag". That matches what `main` did on cancel. `TagPickerActivity` strips
`UNLABELLED` from its seed so it does not spend a slot.

## Migration

One migration outcome changes as a side effect, and it is kept:

- A legacy highlight whose tag names **all** fail `palette->add`
  (`MigrationPlanner.cpp:39-43` — the palette is full, or every name is empty or
  over `MAX_TAG_NAME_BYTES`) comes back with an empty tag list. `PassageDoc::add`
  used to refuse it, and `MigrationRunner.cpp:356-359` recorded it as a
  "store full" drop. It now migrates as an unlabelled passage: the mark survives
  and can be labelled on the device, instead of being lost.

Unchanged: a legacy highlight with **no tag names at all** is still dropped
before any of that (`MigrationPlanner.cpp:29-31`, `DroppedNoTags`). Migrating it
as unlabelled would be consistent, but the migration has already run on the
user's card and changing what it keeps is a separate decision.
