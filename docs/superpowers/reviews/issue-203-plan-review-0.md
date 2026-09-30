Tier: standard

# Issue #203 plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-203-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-203-design.md` (revision 1, review 1 CLEAR)

## How this was checked

The plan says its code blocks are the files' complete final contents, already built and tested. I
did not take that on trust. I extracted all 18 code blocks mechanically into a scratch worktree at
`418508e3`, under `/private/tmp`, and applied the Task 6 YAML inserts and the Step 0.2 CMake line.
Then I ran the plan's own gates there. The review worktree was not modified.

| Gate | Plan claims | Observed |
|---|---|---|
| `StudySleepPickTest` (Step 1.5) | 56 passed | 56 passed |
| `HomeLayoutTest` after Task 4 | 33 passed | 33 passed (12 + 11 + 10, matching Steps 2.4, 3.4 and 4.4) |
| `pio run -e x4pro` | SUCCESS, no `src/`/`lib/` warnings | SUCCESS; no warnings from `src/` or the repo's own `lib/` |
| `./bin/clang-format-fix`, whole tree | no changes | exit 0, no diff |
| full `ctest` | 1481 passed | `100% tests passed out of 1481` |

I also diffed the rewritten files that have a base version against that base:
- `StudySleepPick.h`: only the four listed changes.
- `StudySleepScreen.h`: only the `formatDate` export.
- `StudySleepScreen.cpp`: a faithful lift. The ring and gate are the same as before, and `ScanBuffers` moved into `pickFromAll`.
- `test/study_sleep_pick/CMakeLists.txt`: two added lines.

Result: the code compiles, the tests pass, and the formatting is already clean. The findings below
are about behaviour and what the spec requires, not about whether it builds.

**FILES lock.** Every committed path falls under a `FILES:` line at column 0, outside any code block
(plan lines 18-24). That covers Tasks 1-8, including `test/study_sleep_pick/CMakeLists.txt` under
`test/study_sleep_pick/` and the two YAMLs.

Two files are touched but not committed:
- `test/CMakeLists.txt` is edited locally only and restored in Step 9.4. That matches the #204
  plan's accepted treatment of the same shared file (`2026-09-30-issue-204-plan.md:22,73`).
- The generated i18n headers are gitignored.

Committing the YAMLs rather than handing them off (Deviation 3) matches #206's plan, which also
locks them (`2026-09-30-issue-206-plan.md:10`). The lock serialises the two.

---

## MAJOR 1 — With no cover, the hero's outline is drawn across the header

**Claim.** Plan `drawHero` (lines 2553-2573) follows A16's fallback: "When the cover can't be drawn,
the header goes in its usual place, as `Masthead`'s fallback does" (spec:268-269).

**Problem.** The header's "usual place" is `Rect{0, topPadding, screenW, headerHeight}`. Under Lyra
that spans y = 5..48, across the full width (plan:2562). The hero box starts at y =
`marginTop + topPadding` = 14 (pinned in the test at plan:810). After the header is drawn, the plan
unconditionally draws the hero's rounded outline (plan:2565):
- its top edge at y = 14 runs through the header's title and battery row;
- its side edges at x = 8 and x = 471 run down through rows 14..48.

`Masthead`, which A16 cites as the model, draws no outline around its band
(`src/components/Masthead.cpp:84-93`), so it never had this collision.

The fallback is not rare. It is **every** no-Bible Home (A15), because `bibleCoverPath` is empty
and `drawn` is false (plan:2556). That is the device's first-run screen. It also hits any Bible
whose thumbnail won't draw.

**Evidence.**
- plan:2556, 2561-2565;
- `LyraTheme.h:11,13` (`topPadding = 5`, `headerHeight = 44`);
- `MastheadLayout.h:23-28` (`band.y = marginTop + topPadding`);
- `HomeLayoutTest` `TheHeroSharesTheMastheadThumbnail` (`hero.y == 14`).

**Fix (inline).** Draw the hero outline only when the cover was drawn, so the fallback matches
`Masthead`'s exactly. The plate's buttons still sit on paper, as A16 says:

```cpp
if (drawn) renderer.drawRoundedRect(layout.hero.x, layout.hero.y, layout.hero.width, layout.hero.height, 1, RADIUS, true);
```

An alternative is to keep the outline and draw the fallback header inside the box, at
`Rect{hero.x, hero.y + 1, hero.width, headerHeight}`. That departs from A16's wording, though, so
the first option is preferred. Add a device check to the Step 9.5 list: "With no Bible, the header
is drawn clean, with no line through it."

This does not reverse any decision, so it is fixed inline.

## MINOR 1 — A JSON-document OOM is cached as "no passages", against the spec's error table

**Claim.** The spec's error table says an OOM for `Sampler`, `ScanBuffers` **or the JSON
document** shows `STR_HOME_TAGS_EMPTY`, and "The result is not cached, so the next entry retries".
A8 says the same: "An OOM is **not** cached".

**Problem.** Only the `Sampler` OOM is uncached (plan:2104-2108). A PSRAM failure inside
`deserializeJson` comes back from `readDocFromFileStreamed` as `DocReadStatus::ParseError`, the same
value as a corrupt file (`lib/Serialization/PersistableStore.cpp:156-159`, `DocReadStatus.h:8-13`).
`offerFile` logs it and returns (plan:1515-1518). `pickFromFile` then returns false, and `ensure`
caches `valid = true, hasPick = false` for the rest of the wake (plan:2132-2136).

A second, smaller symptom: on the `Sampler` OOM path, `entry.empty` keeps its previous value. If
that was `TooLong`, the card shows `STR_HOME_TAGS_TOO_LONG` rather than the spec's
`STR_HOME_TAGS_EMPTY`. None of this appears among the plan's deviations.

**Impact.** Low. PSRAM OOM on a document of at most ~49 KB is unlikely, and the cache clears the
next day or on the next tag edit. It is still a spec row that no step implements.

**Fix (inline).**
1. Add `bool outOfMemory = false;` to `ScanTotals`.
2. In `offerFile`, after a non-Ok status, set `totals.outOfMemory = doc.overflowed();`. ArduinoJson
   7 sets `overflowed()` on a `NoMemory` failure.
3. In `HomeVerse::ensure`, if `totals.outOfMemory`, set `entry.valid = false` and
   `entry.empty = home_verse::Empty::NoPassages`, then return false before caching.
4. Also set `entry.empty = NoPassages` on the `Sampler` OOM path.

## MINOR 2 — Step 9.5 pushes and opens the PR unconditionally

**Claim.** Step 9.5 (plan:2855) reads "Push, and open the PR…".

**Problem.** The project's CLAUDE.md, Git workflow rule 2, says: "Never push to any remote, or open
or close a PR, without explicit user approval." The pipeline's implement phase may grant that. The
#204 plan qualifies the same step accordingly: "Push and open the PR (implement phase instructions
govern this)" (`2026-09-30-issue-204-plan.md:1425`). As written, an implementer following this plan
literally would push on its own authority.

**Fix (inline).** Reword it as "Push and open the PR as the implement phase's instructions direct",
and keep the PR body content as it is.

---

## Checked and found sound

- **Spec coverage.** Every assumption maps to code:
  - A1-A3, A12, A14, A15, A18 and A19a map to `HomeTargets` plus `openReader`/`activate`.
  - A4-A9 and A11 map to `HomeVerseCache`, `HomeVerse` and `StudyPassageScan`.
  - A10 maps to `onEnter` and `loop`.
  - A13 maps to `resolveMeetings` and `drawMeetings`.
  - A16 and A17 map to `drawHero` and `HomeLayout`.
  - A19 maps to `render`.
  - A21 maps to `HomeLayout`, pinned by the test.
- **The A10 ordering holds on the first entry.** `onEnter` skips `requestUpdate()` when the verse is
  pending, and `requestUpdateAndWait` blocks until the render task finishes
  (`ActivityManager.cpp:47-69,296-323`). The one case where a render can overlap the scan, a return
  from a sub-screen, is declared as Deviation 6. That same window was accepted in spec review 1.
- **Types and names stay consistent across tasks.** `Candidate::start`/`spine`, `FitGate`,
  `ScanTotals`, `home_verse::{Key, Pick, Entry}` and `HomeTargets::{State, Route}` are used the same
  way from Task 1 through Task 8. `FitRung{sizeIndex, lineHeight, maxHeight}` matches
  `StudySleepFit.h:19-23`.
- **Deviations 1, 2, 4 and 5 are sound and each is justified.** Deviation 2's
  `static_assert(sizeof(Entry) <= 640)` also compiles on the target.
- **TDD.** Tasks 1-4 go red, then green, with verified test counts. Task 5 explains why it has no
  host test. Tasks 7-8 are firmware glue whose pure parts Tasks 2-4 already test.
- **Committable at every step.** Each commit builds, and the generated i18n files and
  `test/CMakeLists.txt` are never staged.

VERDICT: CLEAR
