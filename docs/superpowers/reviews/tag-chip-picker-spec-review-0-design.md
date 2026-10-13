# Review 0 (design attack): alphabetical tags and a chip-grid tag picker

Spec: `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`
Date: 2026-10-11. Scope: design attack only. Citations were read where the reasoning needed them;
a separate pass checks every citation.

| Severity | Count |
|---|---|
| BLOCKER | 1 |
| MAJOR | 4 |
| MINOR | 11 |

Assumptions: A1 HOLDS (two supporting sentences are wrong), A2 HOLDS, A3 UNCLEAR, A4 HOLDS,
A5 HOLDS (edge cases in m10), A6 BROKEN, A7 HOLDS.

---

## Findings, most severe first

### B1 — BLOCKER — Tapping a tag to remove it repaints the chip as if it were still applied

**Scenario.** The picker is open and "amor" is Active. The user taps it. `activateIndex` removes the
tag and requests one render. That render draws "amor" as an inverted pill, which looks Active, and
the panel keeps showing it until something else causes a repaint. The user concludes the tap did
not register, taps again, and applies the tag a second time.

**Mechanism.**
- A tap routed on the loop task arms the tap flash with `flashTicks_ = 2`
  (`FreeInkApp.h:729-745`, specifically `:739-741`).
- The next `render()` decrements the counter to 1, so the flash is still set for that frame
  (`FreeInkApp.h:683-687`). Nothing requests a second render: `UiListActivity.cpp:61-70` requests one.
- `stateFor` adds `StateFocused` to the flashed element (`FreeInkUICore.h:1039-1043`). `resolve()`
  checks focused before selected (`FreeInkUICore.h:606-615`), and `button()` uses the result
  (`button.h:47-49`).
- `TagChipView` sets `focused = active = selected`, which is the inverted pill
  (`TagChipView.cpp:84-89`).
- The spec changes focused/active only for **Disabled** chips (§2 Rules, second bullet).

Today no screen shows this bug, because no chip tap turns an inverted chip back to an outline. The
filter grid closes on a tap (`TagFilterActivity.cpp:280-293`). In the Highlights row, the tapped chip
becomes the selected chip, so the inverted look is correct. In the picker, adding a tag also looks
right, but only by coincidence: the flash look and the Active look are the same. Removing a tag is
the case that breaks.

**The spec must decide** how the flash works on picker chips. Options:
- Make each state's focused/active style the same as its own look, so the flash is invisible. This
  is what the spec already chose for Disabled.
- Call `clearTapFlash()` on every toggle.
- Use a flash look that differs from Active.

Also add a device check: "tap an Active chip; it turns white in the same refresh."

### M1 — MAJOR — Selected ids that have no chip count toward 8/8, then disappear on Done

**Scenario.**
1. Tag "fe" is retired while the Watchtower is open. `retireTag` removes the tag only from the open
   publication's passages (`StudyStore.h:119-124`, `StudyStore.cpp:262-271`), so a Bible passage
   still carries "fe".
2. Later, the user edits that Bible passage from Highlights. Highlights passes the passage's raw tags
   as `initialSelection` (`HighlightsActivity.cpp:475-476`). The constructor removes only `UNLABELLED`
   (`TagPickerActivity.cpp:24-31`).
3. If the passage carries "fe" plus 7 active tags, the picker shows "Tags 8/8", 7 Active chips, and
   every other chip Disabled. The user cannot add a tag, and nothing on screen explains why.

The same thing happens with ids the palette does not know. For example, if `tags.json` is missing,
`LoadResult::Missing` leaves the palette empty, but the passages keep their ids.

Today, commit walks the active tags only and silently drops the hidden id
(`TagPickerActivity.cpp:216-225`). The spec says the result is "ids in display order", but not how
it is built:
- built from `chips_`: hidden ids are dropped;
- built by sorting `selectedIds_` by `nameRank`: hidden ids are kept, because §1 makes `nameRank`
  cover retired ids.

The two give different stored data.

**The spec must decide** what happens to ids in `initialSelection` that are not active palette tags.
Either filter them out at construction, which makes both the count and the cap honest and makes
the drop explicit, or keep them and show them. It must also say which ids the result carries.

### M2 — MAJOR — The picker's selection is read by the render task and written with no lock

**Scenario 1.**
1. The user taps chip A, and a render starts.
2. The render task is in `buildScreen`, calling `chipState()` for each chip. Through §4's
   `pickerState`, that reads `selectedIds_` and its size.
3. The user taps chip B. The loop task routes the tap with no lock (`UiAppHost.cpp:29-40`, then
   `UiListActivity.cpp:61-70` and `:39-46`) and `push_back`s into `selectedIds_`.
4. That vector was built from `initialSelection`, so its capacity equals the passage's tag count. If
   it is full, the buffer is reallocated and the old one freed while the render task is reading it.

**Scenario 2.** When a tag is created, the keyboard's result handler runs with `RenderLock` released
(`ActivityManager.cpp:126-127`). A pending render of the picker can run at the same time, and the
handler calls `setSelected` (`TagPickerActivity.cpp:173`).

The "Tags 3/8" member buffer has the same problem if the loop task formats it.

The race exists today: `TagPickerActivity.cpp:94` reads `isSelected` in `buildScreen`, and `:140`
writes without a lock. The spec, though, takes charge of the threading model (§1 Threading; §3,
"under the RenderLock rules TagFilterActivity.h:62-68 documents") and lists only the members the base
class owns. The filter's equivalent, `filter_`, is written under `RenderLock`
(`TagFilterActivity.h:63-64`, `.cpp:189-194`). The picker's selection has no such rule.

**The spec must decide** the lock contract for each hook:
- `chipState` and `buildHeaderExtras` run on the render task, under its lock.
- `activateIndex`, `onTagRetired` and the create handler write anything the render task reads only
  under `RenderLock`.
- `selectedIds_` reserves `MAX_TAGS_PER_PASSAGE` up front.
- Which task writes the title buffer.

### M3 — MAJOR — `GUI.headerRightSlot` as specified cannot be built under the Classic theme (A6 broken)

The spec describes one rect in three ways: "the same rect the right label would use", "the header's
height", and "just left of the battery". The themes do not agree:

- **Classic (`BaseTheme`).** The battery strip is 20 px tall inside a 45 px band
  (`BaseTheme.h:135-136`), so `batteryLeavesCornerFree` is true. A right label is then drawn by hand
  in the lower-right corner: flush to `band.right() - headerSidePadding`, underneath the battery, one
  small-font line tall (`BaseTheme.cpp:335-337`, `:390-396`). That is neither left of the battery nor
  header-height.
- **Lyra.** The battery strip is as tall as the band (`LyraTheme.h:12-13`), so the label goes through
  `fui::header`. It sits at `content.right()`, left of `rightReserve`, sized to its measured text and
  bottom-aligned to the title line (`header.h:78`, `:140-147`).
- **Both themes.** The label's rect depends on the label's measured width, but
  `headerRightSlot(renderer, headerRect)` takes no width.

If Classic is implemented literally, a 45 px pill placed at the right label's x covers the battery
glyph and percentage, which are drawn flush right in the top 20 px (`BaseTheme.cpp:385-388`).

**The spec must decide** the slot by what the pill needs, and drop "the same rect the right label
would use". For example: right edge at the battery reserve's left edge minus `spaceMd`, full band
height, and a width the caller supplies. The battery reserve also depends on the live percentage
text (`BaseTheme.cpp:308-321`), so the helper and `drawHeader` must share one computation within a
frame.

### M4 — MAJOR — The sleep screen reads the stored tag order, so the new result order changes the tag it shows

The study sleep screen shows a pill with the passage's **first stored** tag.
`StudyPassageScan.cpp:81-88` takes the first valid id from `"t"`, and `StudySleepScreen.cpp:161-176`
and `:266` turn it into a name. The spec changes the picker result from palette order to display
order (§4, "Exits are unchanged"), and both callers store the result as it comes:
`setPassageTags` (`HighlightsActivity.cpp:490`) and `addPassage` (`PassageSelectActivity.cpp:355`).

- A passage is tagged [esperanza (older), amor]. The user opens the picker and presses Done without
  changing anything. The tags are rewritten as [amor, esperanza], and the sleep pill changes from
  "esperanza" to "amor".
- Passages nobody touches keep allocation order; passages saved again become alphabetical. Which tag
  the pill shows then depends on edit history.

This contradicts two statements in the spec:
- Non-goals: "No change to … the order a passage's tags are stored in".
- §4: "the stored order does not matter now that every reader sorts".

The sleep pill is a tag surface that D1 did not list.

**The spec must decide** one of two options:
- Keep the picker result in a fixed order that is not the display order, and leave the pill alone.
- Add the sleep pill to D1 and have it show the passage's tag with the lowest `nameRank`. The screen
  already parses a palette (`StudySleepScreen.cpp:161-176`), which will now build the rank anyway.

---

## MINOR

**m1. Where the Disabled check sits, and the silent tap.** This board does have a Confirm:
- Power short-click acts as Confirm when Settings → Short power is set to Confirm
  (`SettingsList.h:373-376`, `MappedInputManager.cpp:247-252` and `:264-268`, `main.cpp:739-743`).
- The base's default `handleButtons` then calls `activateIndex(nav.selected)`
  (`UiListActivity.cpp:53-57`).

§2 says "the activity ignores its tap". The check must be in `activateIndex`, which both tap and
Confirm go through, and it must recompute `pickerState` there. Also, today a tap at the cap shows
`STR_TAG_LIMIT_PER_HIGHLIGHT` (`TagPickerActivity.cpp:134-137`), while the spec ignores it silently.
Decide which; on e-ink, no response at all can read as a missed tap.

**m2. "+ New tag" at 8/8 that dedupes to a tag already on the passage.** The spec shows
`STR_TAG_LIMIT_PER_HIGHLIGHT` ("This highlight already has the maximum number of tags"), but that
message is wrong here: the tag the user typed is already applied. Show it only when the returned id
is not already selected.

**m3. Host tests will break.** `TagPalette.cpp` is compiled by three host-test targets:
`test/tag_palette`, `test/storage_io` (`CMakeLists.txt:21`) and `test/migration_planner`
(`CMakeLists.txt:7`). §6 adds `Fold.cpp` and the `lib/BibleSearch` include only to `tag_palette`.
The other two fail to compile, and CI's unit-tests job (`ci.yml:125-145`) fails with them.

**m4. Where the new types live.**
- `pickerState` returns `ChipState`, and the find-chip helper works on `ChipEntry`. `ChipEntry` lives
  in `TagChipView.h`, which includes `components/UiAppHost.h` and so pulls in FreeInkApp
  (`TagChipView.h:10-22`).
- `test/tag_rows` puts only `src` and `lib/StudyStore` on its include path
  (`test/tag_rows/CMakeLists.txt`).
- Fix: put `ChipState` and the `(kind, id)` lookup in `TagChipRow.h`, which is FreeInkUI-free by
  design (`TagChipRow.h:12-13`), or make the helper a template.
- Adding `Kind::NewTag` leaves the switch in `TagChipView::isSelected` (`TagChipView.cpp:45-55`)
  without a case for it. That is a `-Wswitch` warning under the per-source `-Wall`.

**m5. A tap right after a rebuild can hit the wrong chip.**
- A tap's `event.value` is a chip index taken from the last published interaction table.
- Create and retire re-sort the list, so indices shift, and `rebuildChips` swaps `chips_` straight
  away.
- The table stays the old one until the next `renderUi` publishes, so a tap routed in that window
  looks up `chips_[i]` in the new list and gets the neighbouring chip.
- "The selection follows the tag" fixes only `nav.selected`.

Decide whether `rebuildChips` calls `closeRouting()` (`UiAppHost.h:73-75`) whenever the list
changes. The filter grid has the same exposure on retire today.

**m6. When `onTagRetired` fires, and which error string.** `StudyStore::retireTag` retires the tag in
memory before saving, and does not roll back if the palette save fails (`StudyStore.cpp:262-271`).
If the base calls `onTagRetired` only when `retireTag` returns true, a failed save leaves a hidden id
selected (the M1 problem). Call it when `!palette().isActive(id)` after the attempt, as the filter
already does (`TagFilterActivity.cpp:193`). Also pick one failure string: the filter uses
`STR_HIGHLIGHTS_SAVE_FAILED` (`:198`), the picker uses `STR_TAG_SAVE_FAILED`
(`TagPickerActivity.cpp:281`).

**m7. Home while the retire popup is open.** The picker ignores Home while the popup is open
(`TagPickerActivity.cpp:207-214`). The filter cancels out from under it
(`TagFilterActivity.cpp:141-146`). With `handleHomeGesture` as a subclass hook, the base cannot own
that guard. Decide whether the base wraps the hook.

**m8. The memory table is wrong for the owner's palette.**
- The picker's `chips_` for the real 48-tag palette is 49 × 44 B ≈ 2.2 KB. That is under the
  4,096 B threshold, so it lives in internal SRAM; it reaches PSRAM only past about 93 chips.
- Today's `rowItems_` lives inside the activity object, which is already over 4 KB and therefore in
  PSRAM. So for the owner's palette, the change does not reduce internal SRAM; the table should say
  so.
- `buildChips` must `reserve(activeCount + 1)` (CLAUDE.md resource rule 7). Otherwise each growth
  step is a fresh allocation in SRAM.

**m9. The rank cache's rationale and contract.**
- The stated reason, that the picker "refreshes its tag snapshot on every buildScreen", goes away in
  this design: chips are built on the loop task, once per mutation. The real users of the cache are
  `tagNamesFor`, called for every visible passage in Highlights' `rebuildRowItems`
  (`HighlightsActivity.cpp:97-106`, often under `RenderLock`), and `buildEntries`. The cache is still
  worth having; the rationale should say this instead.
- "Folds once per palette change" is wrong. `fromJson` runs on every publication open
  (`StudyStore.cpp:71`) and on every study sleep screen (`StudySleepScreen.cpp:170`). That is cheap
  for 48 entries, but the entry count includes retired tags and is bounded only by the 45,000 B save
  budget (`SaveBudget.h:23`). Give a bound for the temporary fold keys.
- `nameRank(id)` is a linear scan: there is no id→index map, the same as `name()`
  (`TagPalette.cpp:42-47`). `tagNamesFor` must look each id up once, not inside the sort comparator.
- Define what `nameRank` returns for `UNLABELLED` and for ids the palette does not know. §1 also
  defines it two ways: as "the position in that order" (the active-only `activeIdsByName`), and as a
  rank that covers retired ids. Pick one.

**m10. Collation edge cases under D2 (A5 caveats).** All of these are consistent with D2 as written;
the owner should confirm they are intended.
- ¡ and ¿ (U+00A1, U+00BF) are below `LATIN_FIRST` (`Fold.cpp:48-49`, `:116-120`), so they pass
  through unfolded and "¿Por qué?" sorts after "zarza".
- Greek and Cyrillic are not case-folded.
- Digits sort as text: "10 mandamientos" comes before "2 Reyes".
- Folding ñ→n puts "año" before "anzuelo", the reverse of Spanish dictionary order.

**m11. The Done pill, the title and the battery.**
- `fui::header` reserves only `rightReserve` (the battery) from the title's width (`header.h:78`). A
  pill drawn afterwards is invisible to it.
- Classic centres the title (`BaseTheme.h:153`), so long `STR_DONE` translations ("Tamamlandı",
  "Concluído", "Zrobione") push the pill toward the title.
- The pill's x position also moves between renders as the width of the battery percentage changes.

The spec should say how the title leaves room for the pill. It should also explain why
`HeaderProps::trailingLabel`/`trailingAction` (`header.h:29-39`, `:99-123`) is not used, since it was
built for exactly this kind of header Done button: `BaseTheme::drawHeader` draws through its own
`GfxRendererFrame<1>` (`BaseTheme.cpp:292`), so its hits never reach the screen's interaction table.
Finally, give the pill `InputTouch` only, so a long-press cannot commit.

---

## Assumptions

| # | Verdict | Evidence |
|---|---|---|
| A1 | **HOLDS**, but two of the spec's supporting sentences are wrong (see below) | No render path reads the palette after this change. Both grids render from `chips_` (`TagFilterActivity.cpp:94-103`). Highlights' render path reads only `rowItems_` and `STUDY.passages().empty()` (`HighlightsActivity.cpp:611-622`); its tag strings are built on the loop task in `rebuildRowItems` (`:97-106`). The only task `src/` creates is the render task (`ActivityManager.cpp:32`). `src/network` never touches `StudyStore`. `MigrationRunner` and the sleep screen use their own local `TagPalette` (`MigrationRunner.cpp:209`, `StudySleepScreen.cpp:169`). |
| A2 | **HOLDS** | `entries_` is private. Only `add` (`TagPalette.cpp:12-27`), `retire` (`:29-33`) and `fromJson` (`:78-106`, which clears only after the version check) change it. Implicit copy and move carry the cache along. This holds as long as `add` rebuilds the cache on the revive path, which returns from inside the loop (`:17-18`). |
| A3 | **UNCLEAR** | Touch: `findTouch` keeps a hit that is not `StateDisabled` live, so a Disabled chip still receives taps. The tap flash is covered by the spec's Disabled focused/active styles. Confirm *is* reachable on X4 Pro (m1) and goes to `activateIndex`. So A3 holds only if the Disabled check is in `activateIndex`, and the spec does not say that. |
| A4 | **HOLDS** | Home is always available: the capacitive key, or a bottom-edge swipe on boards without it (`MappedInputManager.cpp:235-237`). It is handled before `loop()` (`ActivityManager.cpp:75-81`). Cost: button users can no longer focus Done, and Confirm on entry now opens the keyboard instead of committing. That is a change in muscle memory that D3 accepts. |
| A5 | **HOLDS** for D2's stated rule | `foldAppend` folds ASCII case and the Latin range U+00C0–U+017F, ñ→n included (`Fold.cpp:54-79`, `:106-123`). Edge cases are in m10. |
| A6 | **BROKEN** | See M3. The right label's rect depends on the theme and on the label's width, and under Classic it is neither left of the battery nor header-height. |
| A7 | **HOLDS** | The premise is slightly off: a logical Confirm does exist (m1). But a *held* Confirm cannot happen, because holding Power past 400 ms puts the device to sleep (`CrossPointSettings.h:390-392`, `main.cpp:760-766`) before a 700 ms hold could register. No settings screen lets the user remap Confirm onto a physical button. |

The two wrong sentences behind A1:
- "Highlights reads `tagNamesFor` on its render path": it does not.
- "The picker mutates the palette only under `RenderLock`, as it does today": this stops being true
  once retire moves into the base, modelled on `TagFilterActivity.cpp:187-199`, which calls
  `STUDY.retireTag` outside the lock.

Both are harmless, because nothing on the render task reads the palette. A1 says nothing about the
picker's selection state, which *is* read on the render task while being changed (M2).

---

## Attacked and held (so the next pass need not repeat these)

- **Empty palette.** The picker shows only "+ New tag"; the filter shows "All n". The base's
  `count == 0` early return is never reached by either screen: `candidates` always emits All
  (`TagChipRow.h:105`), and the picker always has New tag.
- **One page becoming two after a create.** The page strip appears. Restoring the selection by id
  lands on the new chip's page. Removing the strip's height can only keep the grid paged
  (`TagFilterActivity.cpp:79-84`).
- **Retiring the button-focused chip, or the last chip on the last page.** `onRowAction` sets
  `nav.selected` to the long-pressed chip and clears `buttonFocus_` (`TagFilterActivity.cpp:148-154`).
  After the rebuild the selection clamps to the last chip, and `pageHolding` clamps too
  (`TagChipRow.h:275-286`).
- **A create that revives a retired name.** It is revived under its own id (`TagPalette.cpp:15-19`).
  If the passage already carried that id (the M1 case), the chip comes back Active.
- **Names that fold to the same key** ("Amor"/"amor", "transformación"/"transformacion"). The
  raw-byte tiebreak is deterministic. `fromJson` dedupes by id only (`TagPalette.cpp:94-97`), so a
  hand-edited file can hold duplicate raw names; the id tiebreak covers that.
- **Highlights row in name order.** The old order was allocation order (oldest first), not usage
  order, so no "most used" ordering is lost. A filter that sits off the row still inverts the …
  chip (`HighlightsActivity.cpp:701-703`). D1 accepted.
- **Disabled must not be `fui::StateDisabled`.** Confirmed: `findTouch` skips disabled hits for
  long-press as well (`FreeInkUICore.h:1165-1170`).
- **Interaction budget.** At most 64 chips plus Done is 65, within 96. The retire popup draws outside
  the app's interaction table.
- **No chip moves on a toggle.** `measure()` always measures bold (`TagChipView.cpp:67-72`), and a
  toggle does not rebuild `chips_`, so indices stay the same.
- **Stack.** `GridPage` stays a member. `TagChipView::draw` already carries a `StyleSet` plus a
  `ButtonProps` that contains another (`TagChipView.cpp:77-100`), on the 8,192 B render stack
  (`ActivityManager.cpp:32-33`). Choosing between per-state styles does not need to add to that.
- **Heap churn per render.** Removed: `buildScreen` no longer copies the palette.
- **Deleting `TagRowMapping.h`.** Only `TagPickerActivity.cpp:12,54` and
  `test/tag_rows/TagRowsTest.cpp` use it, and `TagRowsTest.cpp` is one of three files in that test
  target.
- **Removing `STR_TAG_NEW`.** Its only live use is `TagPickerActivity.cpp:100`, and the key exists only
  in English. Caveat: `gen_i18n.py` scans comments in `src/` and `lib/` too (`scripts/gen_i18n.py:267-283`;
  it exits at `:873-881`), so a comment that still names the key after removal fails `pio run`. Add
  `STR_TAG_NEW_CHIP` to `english.yaml` in the same change that first mentions it.
- **The shared base class.** Every difference between the two screens maps onto a hook: counts and
  hidden zero-count chips, filter versus selection, Home, Done, and the result. Neither subclass
  needs something the base blocks, apart from the lock contracts (M2) and the Home popup guard (m7).
