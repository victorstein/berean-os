Tier: heavy

# Issue #200 spec review, pass 1

Reviewed: `docs/superpowers/specs/2026-09-30-issue-200-design.md` (as of `78557255`) against
`gh issue view 200 --repo victorstein/berean-os`, the research note
`docs/superpowers/research/2026-09-30-issue-200-research.md`, review 0 and the owner's decision d1, on
`feature/200-reader-menu-sheet`.

## What holds up

I re-checked the claims this revision changed or added, and these are correct:

- **d1 is applied consistently.** Goal 6, A-5, A-7, A-22, the error table, the data flow and V-4 all say the same
  thing: with no page, the same layout is drawn on a cleared screen with a HALF first paint. No surviving sentence still
  offers the scrolling list. The call sites A-5 lists are complete: `grep -rn openReaderMenu src` finds exactly
  `EpubReaderActivity.cpp:519,535` (reading surface) and `:676,730,747,759,773,800` (result handlers).
- **The region API.** `readFramebufferRegion`/`writeFramebufferRegion` exist as described (`GfxRenderer.cpp:1744-1775`,
  `GfxRenderer.h:268-275`). `readFramebufferRegion` returns 0 when `needed > dstCapacity` (`:1752`).
- **PSRAM placement.** `CONFIG_SPIRAM_USE_MALLOC 1` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096` are in the framework
  `qio_opi/include/sdkconfig.h:1040-1041`. The project's own `sdkconfig.x4pro:2146-2147` has the same values, and
  `platformio.ini`'s `custom_sdkconfig` block (`:94-116`) does not override them. So A-3 holds.
- **MAJOR 2 of review 0 (the tile).** Stacked tiles built from `frame().hit`, `target().bitmap` and `target().text` have a
  real precedent (`FrontlightPanelActivity.cpp:212-223`).
- **MINOR 7 of review 0 (icons).** `search24.h`/`search32.h` have no includers. `grep -rln "search24\|search32" src lib`
  finds only the two files and `search.h`, and `search.h` only mentions `search32.h` in a comment (`search.h:5`). The
  Lucide sources `layout-grid`, `search`, `tag` and `x` exist in `freeink-sdk/libs/assets/Icons/lucide/icons`.
- **Counts.** A Bible reaches 4 tiles + 11 rows and a non-Bible book 3 + 11 (`buildMenuItems`,
  `EpubReaderMenuActivity.cpp:41-82`, with Go to % removed and Tags here added). `PaintedPassage` carries `index`
  (`StudyStore.h:107-113`), so a spine filter can map to `STUDY.passages()` indices.
- **Close and lifetime.** The pop path runs `onExit` under the manager's lock (`ActivityManager.cpp:93-106`).
  `closeCancelled`→`finish` is idempotent (`ActivityManager.cpp:256-263`), so a double close would be harmless.

## Findings

### MAJOR 1: A-2's snapshot capacity is computed in screen orientation, but the region API sizes the copy in panel orientation; it works on the X4 Pro only by coincidence

**Claim (A-2).** `cap = ((pageRect.width + 8 + 7) / 8) * pageRect.height`, "`(480 + 15) / 8 = 61` bytes × about 355
rows ≈ 21.7 KB". `readFramebufferRegion` returning 0 is "treated like an OOM".

**Problem.**
- The reader is in `Portrait`. There, `rotateCoordinates` maps screen `(x, y)` to panel `(y, 479 − x)`
  (`GfxRenderer.cpp:283-290`), and `screenRectToAlignedMemRect` takes the bounding box of the rotated corners
  (`:335-366`). So a full-width page rect of height `H` becomes a panel rect `roundup8(H)` wide and 480 tall. The copy
  needs `roundup8(H)/8 × 480` bytes (`:1750-1751`), not `61 × H`.
- `61·H ≥ ⌈H/8⌉·480` holds only for some `H`. Below about 420 px it fails whenever `H mod 8` is small:
  `python3 -c "print([H for H in range(280,520) if ((H+7)//8)*480 > 61*H])"` prints
  `[281, 282, 283, 289, 290, 291, 297, 298, 299, 305, 306, 313, 314, 321, 322, 329, 330, 337, 338, 345, 346, 353, 354, 361, 369, 377, 385, 393, 401, 409, 417]`.
- The X4 Pro page heights from A-16's own arithmetic are 349, 399 and 449 (6, 5 and 4 rows per column, with a tile of
  32 + 4 + 29 + 16 = 81). They pass with margins of 169, 339 and **29** bytes. Any of these makes the capture fail:
  - an implementer rounds the tile differently, for example `gap` instead of `gap/2`, giving a tile of 85 and H = 345;
  - a later metric tweak moves the sheet by a few pixels;
  - the rotation build's 7-row case, H = 299.

  When it fails, `readFramebufferRegion` returns 0 and every open silently takes Cleared with
  `LOG_ERR "page snapshot unavailable"`. That is the headline feature switched off.
- T-2 cannot catch this. `screenRectToAlignedMemRect` is a file-static in `GfxRenderer.cpp`, and the layout test never
  computes bytes. The "21.7 KB" figure and V-2's "PSRAM down about 21 KB" rest on the wrong shape. The real figure for
  H = 349 is 21,120 B.
- `PassageSelectActivity.cpp:51`, which A-2 cites, gets away with the same formula only because it multiplies by
  `MAX_SELECTED_SNAPSHOT_LINES` (`:52-53`) for a region that is almost always far smaller.

**Fix.** Size `cap` to cover both orientations, for example
`max(((w + 15) / 8) * h, ((h + 15) / 8) * w)`, which is 21,655 B at 480 × 349. Put this `snapshotBytes(w, h)` in the
pure `ReaderMenuSheetLayout.h`, and add a T-2 case asserting `cap ≥ ((h + 7) / 8 + 1) * w` and
`cap ≥ ((w + 7) / 8 + 1) * h` for every row count 1 to 14. Correct A-2's arithmetic to "≈ 45 bytes × 480 panel rows ≈
21.1 KB", and V-2 to match.

### MAJOR 2: the column lists' `ListProps` are left to theme defaults, and those defaults break both "no scrolling" and V-5

**Claim (A-17 step 6, A-21, V-5).** Each column is `screen.setContentMargin(columnInsets)` then
`screen.list(props, h)`, "keeping the theme's substitution". Values are capped at 12 bytes, and "fit in a half column is
checked on the device (V-5)". The rows fill row-major.

**Problem.**
- **Row height.** Today `props.rowHeight` is written only by `syncListViewport` (`UiListActivity.cpp:126-138`), and
  the sheet no longer calls it. `Screen::list` substitutes `theme_.rowHeight` when `rowHeight <= 0`
  (`FreeInkApp.h:283-284`). That token is `lineHeight * 2 + 8` (`FreeInkUI.cpp:114`), which is 66 for Ubuntu 12
  (advanceY 29, `ubuntu_12_regular.h:4282`), and `uiThemeTokens` does not override it (`UIThemeTokens.h:12-37`).
  - A 6-row column in A-16's 300 px band then fits `listVisibleRows` = 4 rows (`list.h:300-316`).
  - The virtualised list draws only those 4, adds a scroll indicator, and drops the other 2.
  - That is the scrolling d1 forbids, and T-2 asserts rowHeight 50 in a model the render never uses.
- **Width.** Measured from the glyph tables (advanceX in 12.4 fixed point, `GfxRenderer.cpp:721`; labels in
  `bodyText` = UI 12, values in `smallText` = UI 10, `FreeInkApp.h:248-253`):
  - A half column is 240 px. The inherited `listInset` 20 is taken from **both** sides (`list.h:319-322`,
    `LyraTheme.h:23`), and `listSidePadding` 8 from each side, which leaves about 184 px. The value slot then takes
    `valueW + valueInset 8 + textGap 10` (`list.h:569-584`).
  - Spanish Night mode: "Modo nocturno" is 176 px and "Desactivado" is 116 px, so the label gets about 50 px. The same
    happens for Frontlight (114 + 116) and Auto turn.
  - Even English Night mode ("Night mode" 134 + "OFF" 39 + 18) is 191 > 184.
  - The 12-byte cap in A-21 does not help: an 11-byte value already takes more than half the band. So V-5 fails by
    arithmetic, not by chance.
  - The Spanish "Tags here (n)" label is about 198-212 px, so the ellipsis eats the count, which is the only part of
    that row that matters.
- **Row-major.** `ListProps.items` is one contiguous pointer plus a count (`list.h:36-46`), with no stride. A row-major
  split (column 0 = rows 0, 2, 4…) needs two separate `ListItem` arrays. The spec keeps one `menuRowItems` and does not
  say how they are split. `selectedIndex` is local to each array, so the flat `nav.selected` must be mapped per column.

**Fix.** State the column props explicitly in A-17, rather than "the theme's substitution":
- `props.rowHeight = layout.rowHeight`;
- `props.rowInset` as a small explicit value (for example 4);
- `props.scrollIndicator = false`;
- `props.inputMask = fui::InputTouch`, as today (`EpubReaderMenuActivity.cpp:206`).

Give Night mode and Frontlight `ListItem::toggle = true` / `toggleChecked`. That uses a 38 px switch instead of a text
value, with precedent at `TagPickerActivity.cpp:93-94`. Give Auto turn a value that is a number or "–" rather than the
translated "Off". Render Tags here as the label `STR_TAGS_HERE` ("Tags here") with `n` in the value slot, which also
drops the `%d` format key. Split the rows into two `ListItem` arrays of `MAX_ROWS/2` each when the model is built, and
map `nav.selected` to the per-column `selectedIndex`. Update V-5 to expect a truncated long label (for example
"Tomar captura de pantalla" is 298 px) but a visible state on every toggle row.

### MINOR 3: A-19's "on `ACTION_ROW`, falls to the existing dispatch" routes the same tap twice

- `handleCustomInput` routes through `UiAppHost::routeTouch`, and `app.route` has already dispatched `ACTION_ROW` to
  `rowActionTrampoline` → `activateIndex` (`FreeInkApp.h:729-741`, `UiListActivity.cpp:33-46`).
- If `handleCustomInput` then returns `false`, `UiListActivity::loop` goes on to `routeListTouch` (`:85-87`), which
  builds the same snapshot. `touchSnapshotFrom` takes a `const MappedInputManager&`, so nothing is consumed
  (`UiAppHelpers.h:166-191`).
- Release routing is stateless: it re-runs `findTouch` on the coordinates (`FreeInkUICore.h` `routeAgainst`,
  `touchReleased` branch). So the row dispatches a second time, and Night mode or Frontlight toggles twice, which looks
  like a no-op.

**Fix.** A-19: whenever `route.routed`, `handleCustomInput` returns `true`. It requests a render only when
`app.invalidated()` and the action is not `ACTION_CHROME`, mirroring `routeListTouch`'s `UiListActivity.cpp:68`. Also
pick one closer for `ACTION_CLOSE`: either the `app.on` handler or the post-route branch, not both.

### MINOR 4: review 0's MINOR 6 fix is half-applied; one locked rebuild site is missed

- A-14 moves the spine-filtered query outside the lock for `onEnter` and the delete path (`HighlightsActivity.cpp:407`),
  and concludes "No held lock is re-entered".
- But `applyTagEdit` also rebuilds under `RenderLock lock(*this)` (`HighlightsActivity.cpp:356-365`), and it is
  reachable from the chapter-filtered list through long-press → Tags…
- With `spineFilter` set, `rebuildVisibleIndices` there calls `passagesInDocument` → `unitText` →
  `SpineHtmlStream::stream(..., Inflate)`. When the HTML cache is missing, that takes a second `RenderLock`
  (`SpineHtmlStream.cpp:37`) on a non-recursive `xSemaphoreCreateMutex` (`ActivityManager.h:69`).
- The same latent precondition review 0 accepted applies, but the spec's claim is false as written. The tag-filter
  result handler (`:139`) is unlocked and fine.

**Fix.** A-14: split `rebuildVisibleIndices` into "compute into a local vector" (unlocked) and "swap under the lock",
and use that split at all three sites (`:139`, `:362-364`, `:407-409`), not just the delete path.

### MINOR 5: small inaccuracies and gaps

- **Spanish `STR_TAG`.** `STR_TAG` exists only in `english.yaml:375`: `grep -n "^STR_TAG:" lib/I18n/translations/*.yaml`
  finds one line. In the Spanish UI the Tag tile would read "Tag" beside "Ir a", "Buscar" and "Marcar". Add the Spanish
  `STR_TAG` to the hand-off.
- **Icon count.** "13 existing icon arrays" is 13 icons × 2 sizes. `listIcons.h` has 26 `static const freeink::Icon`
  definitions. The byte-identity check should cover all 26, plus their `_bits`.
- **`search.h` comment.** Deleting `search32.h` leaves `search.h:5` citing it as its source. Reword the comment in the
  same change.
- **Wrong cross-reference.** The revision's MINOR 5 bullet says the atomic mode is A-9; it is A-8.
- **V-7's flag.** `-DREADER_MENU_FORCE_SNAPSHOT_OOM` implies an `#ifdef` in shipped source. Say so in A-2 and name it
  under Resources, so it is not read as test-only code.

## Verdict rationale

There are no BLOCKER-severity defects, and d1 is applied without contradiction. Both MAJORs are wrong or missing
mechanics that the spec can correct inline, without reversing a decision, changing scope or needing the owner:

- MAJOR 1 needs the capacity formula (and a test that would have caught it).
- MAJOR 2 needs explicit column props (and value slots that fit).

MAJOR 2's toggle switches replace the mockup's "Off" text on two rows. If the owner prefers text, the inline
alternative is short values under new keys. Either way it is a presentation detail inside the chosen direction, not a
new decision. The MINORs are inline fixes.

VERDICT: CLEAR
