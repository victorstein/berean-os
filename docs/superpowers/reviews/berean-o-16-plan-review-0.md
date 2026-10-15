Tier: heavy

# Plan review 0: berean-o-16 (Predicación screens), plan against spec

Plan: `docs/superpowers/plans/2026-10-15-berean-o-16-plan.md`
Spec: `docs/superpowers/specs/2026-10-15-berean-o-16-design.md`
Read at `1b8b800a`. I read the plan but did not apply or build it.

## Summary

The plan covers the spec. Every goal (G1–G6) and every row of the error table maps to code in a
task. All 15 strings are there. The FILES lock covers every file a step edits: the
`src/activities/preaching/` prefix, the two reader files, `ActivityResult.h`,
`ActivityManager.{h,cpp}`, both YAML files and the four `test/ui_layout` files. Comma-separated
FILES lines are the convention in earlier plans. No step pushes, opens a PR or runs `hpipe submit`
(plan:2607).

I checked these and they hold:

- **Lifecycle order.** The Replace path runs the topic's `onExit` and then the list's
  (`ActivityManager.cpp:143-150`). The list parks the Bible before the reader takes it.
- **Tap handlers and the render lock.** Handlers run on the loop task with no render lock held
  (`UiAppHost.cpp:29-40`, `FreeInkApp.h:729-745`). So `openNextQuestion`'s `RenderLock` and the
  `SpineHtmlStream` lock (`SpineHtmlStream.cpp:37-44`) cannot deadlock.
- **Counts line up with rows.** `countByTag` returns its counts in the input order, zeros
  included (`PreachingPassages.h:40-42`, `.cpp:174-175`). So `count(row)` indexes correctly.
- **The href pre-check.** `resolveHrefToSpineIndex` strips the `#fragment` before it compares
  (`Epub.cpp:953-955`).
- **The "›" mark.** Noto Serif 12 has U+203A (`notoserif_12_bold.h:2889`).

One MAJOR and three MINORs follow.

## MAJOR

### M1. A topic opened from the list can skip its title and opening prose

**Claim.** A fresh open from the list, and the "Para la revisita" swap, should start on the
topic's first page. The spec says so for the revisita: "select verse 0 on page 0" (spec:385-386).

**Problem.**

- Both paths pass Verse ordinal 0 to `selectVerse`.
- `selectVerse` turns to the page where that Verse card *begins*.
- Every question starts with a non-selectable `Title` card, and some go on with `Text`/`Heading`
  prose. When the first Verse begins after page 0, the screen opens on that later page. The
  title, the opening prose and page 0 are skipped, and the indicator reads "2/N". Left still gets
  back to them, but only if the reader notices.
- The likely case is Q20, which is "mostly prose: `<h2>` plus plain `<p>`" with scripture further
  in (parent spec `2026-10-14-preaching-tool-design.md:78-79`).
- No host test can see this. The nav tests only call `selectVerse` on layouts where verse 0 is on
  the page being tested.

**Evidence.**

- plan:2445 `openQuestion(index + 1, 0);` and plan:2452 `openTag(..., 0);`. Both are fresh opens.
- plan:1894 `cursor = preaching::selectVerse(*layout, startVerse);`
- plan:1127-1132: `selectVerse` returns `{layout.verseFirstPage[verse], verse}`.
- plan:2047: the revisita does `cursor = preaching::selectVerse(*layout, 0);`
- plan:1644: a question's first card is always `Title`.

**Fix.** Use "no verse" for a fresh open, and keep `selectVerse` for an origin.

1. In `PreachingActivity::activateIndex`, call `openQuestion(index + 1, -1)` and
   `openTag(topics[...], -1)`.
2. In `PreachingTopicActivity::onEnter`:
   `cursor = startVerse < 0 ? preaching::atPage(*layout, 0) : preaching::selectVerse(*layout, startVerse);`
3. In `openNextQuestion`: `cursor = preaching::atPage(*layout, 0);`

The origin path still passes `origin.verse`, which is never negative (it is a `uint16_t`), so
returning from the Bible still lands on the card. A Back from a page with no selection still
returns verse 0 through `ofQuestion`'s clamp (plan:153).

Add a test to Step 3.1:

```cpp
TEST(PreachingCardNav, AFreshOpenStartsOnTheFirstPage) {
  const Layout layout = pagesWithVerses(3, {1, 2});
  EXPECT_TRUE(sameCursor(preaching::atPage(layout, 0), {0, -1}));
}
```

With it, Step 3.2's expected count becomes 22 tests, not 21.

## MINOR

### m1. The T5 seam `onTopicsChanged` reloads the palette while the list's rows still point into it

**Claim.** The plan says T5's result handler needs only this one call (plan:2187).

**Problem.**

- `onTopicsChanged` calls `session.reloadTopics()` outside the render lock (plan:2459).
- That reload re-runs `topicsFile.load()` (plan:1503-1508, 1492), which replaces the palette's
  strings.
- The Mis temas rows still hold `const char*` labels into those strings
  (`item.label = session.topicName(...).c_str()`, plan:2345; `TagPalette.h:53` returns a
  reference). They are only rebuilt afterwards (plan:2461-2464).
- The handler runs after a Pop with the lock released (`ActivityManager.cpp:118-124`). If the
  render task was already waiting to draw, it can render the list in that window. `buildScreen`
  then reads freed label memory, and also reads `session.topics()` while it is being changed
  (plan:2420).
- T3 never calls the seam, so this only shows up once T5 lands, but the seam is shipped as ready.

**Fix.** Empty the rows under the lock before reloading:

```cpp
{ RenderLock lock(*this); rowItems.clear(); rowValues.clear(); }
```

Then reload and count, and then `rebuildRows()` under the lock as it does now.

### m2. The hand-off narrows the spec's device checks, and Mis temas cannot be tested yet

**Claim.** The hand-off lists the spec §Testing device checks (plan:2617-2622).

**Problem.** Three things are dropped or left out:

1. **Mis temas.** The hand-off drops "Mis temas shows picks with counts" (spec:513-514). Until T5,
   nothing can create picks: `openTopicPicker` only logs (plan:2456). So the tester has no way to
   reach the tag screen, the counts, the `OpenAt` path from a tag card into the Bible, or the
   empty-tag and unreadable-verses states. The tester needs to be told how to seed the picks.
2. **The cold-slot drop.** It is omitted (spec:520-523). A tester who starts from boot sees a
   one-time heap drop and may report it as a leak.
3. **Tap-to-page time.** The timing for the furthest question reference is omitted (spec:524-525).

**Fix.** Add three items to the hand-off's device checks:

- To test Mis temas before T5, write `/.berean/preaching.json` as `{"v":1,"tags":[<ids>]}`
  (`PreachingTopicsFile.h:12-13`, `SdPaths.h:28`). Take the IDs from active tags in `tags.json`,
  and upload the file through Settings → System → File Transfer. Then check the counts, a tag
  card's reference opening the passage, and a tag with no Bible verses showing
  `STR_PREACHING_TOPIC_EMPTY`.
- From a cold slot, a one-time drop the size of the Bible is expected.
- Record tap-to-page time for the furthest question reference.

### m3. A tag origin whose topic fails to open leaves Mis temas without counts

**Claim.** The counts pass is deferred until Mis temas is first shown, and runs then (spec:308-310).

**Problem.**

- With a tag origin, `onEnter` puts the list on Mis temas with no counts (plan:2289-2294).
- It relies on `onTopicResult` to run the counts (plan:2521-2522).
- If `openTag` fails (plan:2495-2498, for example with `OutOfMemory`), there is no topic result.
  The list stays on Mis temas without counts until the user switches tabs and back.
- It is low impact: with `VersesUnavailable` the counts would fail anyway. It matters only for
  OOM and transient failures.

**Fix.** Have `openTag` return whether it pushed. If it did not, in `onEnter`, call
`session.ensureCounts()` and then `rebuildRows()` under the `RenderLock`.

---

MAJOR: 1 (M1, the concrete fix is above). MINOR: 3. None blocks; apply M1, and m1–m3 when convenient.

VERDICT: CLEAR
