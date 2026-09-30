Tier: standard

# Issue #237 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-30-issue-237-design.md` (at `d18feddc`).
Checked against: issue #237 (`gh issue view 237 --repo victorstein/berean-os --json title,body,comments`, no comments), the research note, and the code on this branch.
Decision d1 (the hero pays, Recent keeps `listRowHeight`) is settled and was not reopened.

## What was verified and holds

- **Geometry table.** I recomputed it from `HomeLayout.h:97-104` with the test inputs (`HomeLayoutTest.cpp:15-20`). bottom = 800 − 3 − 5 = 792. Lyra: fixed = 143 + 172 + 94 + 79 = 488 and 4·gap = 32, so hero = 792 − 22 − 488 − 32 = **250**. Base: fixed = 458 and 4·gap = 40, so hero = **272**. plateHeight `:93-95` + PAD gives 101 (Lyra) and 102 (Base). The art above the plate is 149 and 170, both ≥ `MIN_HERO_ART` 96 (`:28`). The strip is 7 × 34 = 238, so strip.x = 8 + 464 − 8 − 238 = 226 and title width = 226 − 8 − 56 = **162** (`:137-140`). hero.y + height = 272 before and after, so everything below the hero keeps its y and the icon row foot stays at 792.
- **A4 (thumbnail key).** `thumbHeightFor(464, 250) = max(250, int(464/0.6)) = 773` (`CoverBandGeometry.h:46-48`). `thumbPathFor` keys only on that value (`CoverBand.cpp:91-92`). No regeneration.
- **A6 (measured widths).** `getTextWidth` returns `maxX − minX` from `getTextBounds`, with `minX` seeded at `startX = 0` (`EpdFont.cpp:9-12`, `:58-59`). The glyph table (`struct` order `width, height, advanceX, left, …`, `EpdFontData.h:131-139`) gives these digit entries (`ubuntu_10_regular.h:1507-1516`): `0,2,3,5-9` = `{10, …, 188, 1}`, `1` = `{7, …, 188, 1}`, `4` = `{11, …, 188, 0}`. The advance of 188/16 snaps to 12 (`EpdFontData.h:27`). No digit appears in the kern class tables: `awk 'NR>2590 && NR<3551' … | grep -E "// [0-9]$"` returns nothing. So `"0"` = 11, `"00"` = 23, and no day number from 1 to 31 measures above 23, because `x4` is also 12 + 0 + 11 = 23. The claim that `"00"` is the widest holds.
- **Real gap on device, not just in the test model.** `drawCentredIn` centres in the cell widened by PAD (`LauncherActivity.cpp:66-71`, `:369`). Take "28" and "29" at pitch 34. The pen is at cellX − 8 + (50 − 23)/2 = cellX + 5, so the ink runs from cellX + 6 to cellX + 28, and the next number's ink starts at cellX + 40. That is a **12 px** gap, which clears the 11 px measured digit and the 11.75 px advance, so it also meets the research note's stricter reading. The today box (`:363`) ends at cellX + 33, **7 px** clear of the neighbour's ink. The spec's "6 px" in A10 is conservative because it ignores the 1 px bearing. The seven-cell letters stay centred on the same box (A9).
- **A8.** Only `english.yaml:9` and `spanish.yaml:9` define `STR_MEETINGS`. `STR_MEETING_PROGRESS` is short (`"%d%% leído"`, `spanish.yaml:366`), so 162 px holds both lines with room to spare.
- **Seams.** `HomeLayout::compute` has one production caller (`LauncherActivity.cpp:95`) and one test caller (`HomeLayoutTest.cpp:23`). `STRIP_CELL` is used only at `HomeLayout.h:137` and `LauncherActivity.cpp:359-360`. Touch routing reads the same boxes (`boxFor`, `:224-253`). `test/home_layout/CMakeLists.txt` needs no change. `LOG_DBG` is compiled in on `x4pro` (`platformio.ini:174` `-DLOG_LEVEL=2`, `Logging.h:57-58`), so the device check's serial line will print on a dev build. It will not print on the OTA release build (`:191`).
- **Citations.** I spot-checked about 25 `file:line` references and all match, apart from the one in MINOR 3.

## Findings

### MINOR 1: A2 is fused into A1's paragraph

- **Claim.** A1 and A2 are separate labelled assumptions.
- **Problem.** The d1 edit dropped a newline, so line 34 reads `…against a MIN_HERO_ART floor of 96 (:28).- **A2 — Top gap = PAD …`. In rendered Markdown, A2 is not a list item. It becomes the tail of A1's already long DECIDED paragraph. A reader or plan-writer scanning the bullets will miss the top-gap mechanism: `top += PAD` and `band(…, insets.top + PAD, …)`.
- **Evidence.** Spec line 34: `grep -n '(`:28`).- \*\*A2' docs/superpowers/specs/2026-09-30-issue-237-design.md`.
- **Fix.** Put a line break before `- **A2`.

### MINOR 2: `fallbackHeader` (A5) is added to `Layout` but no test pins it, and it is not stated to diverge from the app-wide header row

- **Claim.** A5: `HomeLayout` exposes `fallbackHeader = {0, topPadding + PAD, screenWidth, headerHeight}`, and the geometry "is host-tested rather than recomputed in the `.cpp`" (Architecture).
- **Problem.** None of the updated or new tests listed under Testing strategy asserts `fallbackHeader`, so the no-cover half of complaint 1 is not host-covered. Separately, every other screen draws its header at `Rect{0, metrics.topPadding, …}`: 18 call sites (`grep -rn "drawHeader(renderer, Rect{0, metrics.topPadding" src | wc -l` → 18), plus `Masthead.cpp:89` for the Masthead's own no-cover fallback. After this change, Home's no-cover header sits 8 px lower than all of them. That is a reasonable reading of "breathing room at the top", but it should be recorded as a deliberate divergence, not left implicit.
- **Evidence.** Spec lines 101-117 (no `fallbackHeader` assertion). `LauncherActivity.cpp:286`. `Masthead.cpp:89`.
- **Fix.** Add `fallbackHeader.y == topPadding + PAD` and `fallbackHeader.width == SCREEN_W` for both themes to `TheTopLeavesAGapAboveTheHero`. Add one sentence to A5: "Home's no-cover header deliberately sits PAD below the `topPadding` row other screens use."

### MINOR 3: Wrong line cited for `drawTextIn` truncation

- **Claim.** A8 and the research note say "`drawTextIn` truncates the percent line to its box (`LauncherActivity.cpp:59`)".
- **Problem.** Line 59 is the second line of the signature. The truncating call is at `:61` (`renderer.truncatedText(font, text, box.width - 2 * inset, style)`).
- **Evidence.** `sed -n 58,63p src/activities/launcher/LauncherActivity.cpp`.
- **Fix.** Cite `:58-63`, or `:61`.

No BLOCKER or MAJOR findings. The design implements d1 exactly: the hero outline gives up 8 px and the art gives up 16 px, Recent and the icon row are untouched, and the screen still fits at 480×800. The strip criterion is derived from runtime-measured font data, and it holds on the actual draw path, not only in the test's model of it. All three MINORs can be fixed inline.

VERDICT: CLEAR
