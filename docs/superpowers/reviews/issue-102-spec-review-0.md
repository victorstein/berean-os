Tier: heavy

# Review 0 — `2026-09-27-issue-102-design.md`

Reviewed against issue #102 (`gh issue view 102 --repo victorstein/berean-os`),
the research note, and the tree at `5ff298d2`. Decision d1 is taken as settled.
The spec applies it correctly. The load clamp (`src/CrossPointSettings.cpp:119,165-169`)
does map stored 2 and 3 to the struct default `LYRA` (`CrossPointSettings.h:315`).
The web POST rejects `val >= enumValues.size()`
(`CrossPointWebServer.cpp:1268-1272`). `setTheme` keeps a case for every
enumerator (`UITheme.cpp:30-52`). No other code reads `LYRA_3_COVERS` or
`ROUNDEDRAFF`: `rg -n 'ROUNDEDRAFF|LYRA_3_COVERS|uiTheme'` outside `docs/` hits only
`CrossPointSettings.h:218,315`, `SettingsList.h:273-276`, `SettingsActivity.cpp:193`
and `UITheme.cpp`. The A4 metric fields have no reader outside the removed code
(verified by rg over `src lib test freeink-sdk`). The A5 table's per-key YAML
counts all match `rg -c` exactly, and the key-gate prefixes over-match no live
key.

I found two MAJORs, both incomplete deletions or a gate the spec cannot satisfy. Neither
reverses d1, changes scope, or needs the human, so both can be fixed inline.

## MAJOR 1 — Completeness gate 1 can never reach zero, and the spec says it can

**Claim.** Testing strategy, step 2: `rg -n 'HomeActivity\b|…' src lib`
should return 0 lines. The spec adds: "`isHomeActivity` (`Activity.h:53`,
`LauncherActivity.h:32`) is a live virtual; the `HomeActivity\b` pattern does not
match it."

**Problem.** The pattern has a trailing `\b` but no leading one. In
`isHomeActivity()`, `HomeActivity` is followed by `(`, which is a word boundary,
so it matches. The gate stays non-zero even after a correct change. An
implementer told to get it to zero has one obvious way to do that: rename or
remove `isHomeActivity`. That virtual is live. It gates the home gesture at
`ActivityManager.cpp:77`.

**Evidence.** Current tree:
```
src/activities/Activity.h:53:  virtual bool isHomeActivity() const { return false; }
src/activities/ActivityManager.cpp:77:    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
src/activities/launcher/LauncherActivity.h:32:  bool isHomeActivity() const override { return true; }
```
All three matched `rg -n 'HomeActivity\b|RecentBooksActivity|ButtonRemapActivity|goToRecentBooks|RemapFrontButtons' src lib`.

**Fix.** Use `\bHomeActivity\b` in gate 1, and rewrite the sentence about
`isHomeActivity` to name the leading `\b` as what excludes it.

## MAJOR 2 — `LyraTheme::drawEmptyRecents` and two string keys are orphaned but not removed

**Claim.** Goal 1 and Goal 3 say to remove "every declaration … and string key that
only they used" and "every key orphaned by 1–2". A3 lists what goes from
`LyraTheme` as "the three overrides". A5's table is presented as the full set of orphaned
keys.

**Problem.** `LyraTheme::drawEmptyRecents` is a public, non-virtual helper. Its
only callers are the removed `LyraTheme::drawRecentBookCover` and the deleted
`Lyra3CoversTheme`. After the change it has no caller, and it is still the only
thing that keeps `STR_NO_OPEN_BOOK` and `STR_START_READING` referenced. The other
references to both keys sit inside `BaseTheme::drawRecentBookCover`, which is
removed. So the spec leaves one dead method and two dead keys × 32 YAMLs in place. That
contradicts its own Goal 3.
The gen_i18n gate would not catch this even if the method were deleted (see
MINOR 4).

**Evidence.**
- `rg -n drawEmptyRecents src lib` → `LyraTheme.h:114` (declaration),
  `LyraTheme.cpp:487` (inside `drawRecentBookCover`, which runs `:395-489`),
  `LyraTheme.cpp:491` (definition), `Lyra3CoversTheme.cpp:118` (file deleted).
- `LyraTheme.cpp:494,496` use `tr(STR_NO_OPEN_BOOK)` and `tr(STR_START_READING)`.
- The only other code references are `BaseTheme.cpp:790-791`. They are inside
  `BaseTheme::drawRecentBookCover` (`:567`), which ends before `getMenuRowHeight`
  (`:795`).
- Each key is in all 32 YAMLs (`rg -l '^STR_NO_OPEN_BOOK:'` → 32,
  `^STR_START_READING:` → 32).
- `STR_CONTINUE_READING` is also referenced from `BaseTheme.cpp:773,784`, but it stays live
  because of `LauncherActivity.cpp:513`.

**Fix.** Add `LyraTheme::drawEmptyRecents` (`LyraTheme.h:114`, `LyraTheme.cpp:491-497`) to
the Architecture table. Add `STR_NO_OPEN_BOOK` and `STR_START_READING` (32 YAMLs each) to
the A5 table and to the gate-2 YAML regex.

## MINOR 1 — Gate 2 hits `BaseTheme.h` comments that the spec never schedules for rewriting

**Claim.** The stale-comment list covers `GfxRenderer.h:379`,
`MappedInputManager.cpp:390-391`, `CrossPointSettings.cpp:87,188` and
`UiTabListActivity`. Gate 2 (`rg -n 'Lyra3Covers|RoundedRaff|…' src lib`)
should return 0 lines.

**Problem.** Four `ThemeMetrics` field comments name RoundedRaff and are on
neither list. Gate 2 fails on a tree that follows the spec exactly.

**Evidence.** `BaseTheme.h:46` `// row corner radius (RoundedRaff cards, Lyra pill)`,
`:52` `// bold row titles (RoundedRaff)`, `:60` `(Classic, RoundedRaff)`,
`:67` `(legacy RoundedRaff tabs)`. `BaseTheme.h:229-231` goes with
`getMenuRowHeight`, so it is not an issue.

**Fix.** Add `BaseTheme.h:46,52,60,67` to "Stale comments to rewrite". Reword them to
describe the metric itself, the same way the spec rewords the `UiTabListActivity`
comments.

## MINOR 2 — File-local helpers and includes orphaned in `LyraTheme.cpp` / `UITheme.cpp`

**Problem.** Removing `drawList`, `drawRecentBookCover` and `drawButtonMenu` from
`LyraTheme.cpp` leaves anonymous-namespace symbols with no user:
- `iconForName`: used only at `:267` (drawList) and `:522` (drawButtonMenu)
- `mainMenuIconSize` (`:39`)
- `listIconSize` (`:40`)
- `mainMenuColumns` (`:41`, already unused)
- `int coverWidth` (`:42`), used only at `:402-477`

`LyraTheme.cpp:14` `#include "RecentBooksStore.h"` also loses its last user (`RecentBook`, `:410`).
The 32 px icon headers (`LyraTheme.cpp:16-30`) are then only reachable through
the dead `iconForName`. A3 considers only the `BaseTheme.h:10` / `BaseTheme.cpp:16`
includes. `UITheme.cpp:12` includes `RecentBooksStore.h` even though no
remaining function in that file uses it.

**Fix.** List these in the Architecture row for `LyraTheme.{h,cpp}` and the row for
`UITheme.cpp`, and let the build decide the includes, as A3 already does.

## MINOR 3 — The theme setting stops being a popup; the spec says it stays one

**Claim.** A2: "The on-device option popup draws from the same list". Data flow:
"the option popup offers two entries; picking one reloads the theme exactly as
today". Device step 5: "UI theme shows two options".

**Problem.** `SettingsActivity::toggleCurrentSetting` only opens the popup when
`enumValues.size() > 2` (`SettingsActivity.cpp:261`). With two entries it falls
through to `(currentValue + 1) % size` (`:275`). A tap on the row then switches
Classic ↔ Lyra in place. The live reload still happens through
`applyUiSettingChange(setting.valuePtr)` at `:364`, so behaviour is sound. The
spec's description and the tester's expectation are wrong, though.

**Fix.** Update A2, "Settings change", and device step 5 to say: "tapping the row toggles
between Classic and Lyra and repaints live (`SettingsActivity.cpp:261,275,364`)".

## MINOR 4 — The gen_i18n gate compares a count, which hides new orphans

**Claim.** Step 2: "`Unused keys (N)` must not grow".

**Problem.** The non-verbose run prints only the total count
(`gen_i18n.py:944-947`). The change removes keys that are already unused, such as the 11
`STR_CALIBRE_*`, so N drops. A newly orphaned key, like the two in MAJOR 2, would
be hidden inside that drop. Also, the literal string `Unused keys (N)` only
appears with `--verbose` (`gen_i18n.py:910-911`).

**Fix.** Run `python3 scripts/gen_i18n.py --verbose lib/I18n/translations lib/I18n/`
before and after, and require the "after" unused set to be a subset of the "before"
set.

## MINOR 5 — `USER_GUIDE.md` §4 still documents the deleted Home screen

**Problem.** A10 updates `USER_GUIDE.md:263` "since it documents the theme
options users see". The same reasoning applies to `USER_GUIDE.md:94-104`
("## 4. Home screen": Browse files, **Recent books**, File transfer, Settings,
"Selecting the cover resumes reading"). That section describes `HomeActivity`
and `RecentBooksActivity`, which this change deletes. The section was already
stale, since the launcher replaced that screen, but this PR removes the code it
describes.

**Fix.** Do one of two things:
- Extend A10 to rewrite §4 for the launcher.
- Record §4 in Non-goals as known pre-existing drift, with a follow-up.

## MINOR 6 — The release-note risk depends on a PR title the spec never gives

**Claim.** Risks: "RoundedRaff users see their look change … the release note
(PR title) says so."

**Problem.** The spec gives no PR title. `refactor:` entries do reach the
changelog (`CHANGELOG.md:204-206`, PR #57). But a title like "refactor: remove
unreachable fork code" would not tell a RoundedRaff user their theme was
retired.

**Fix.** State the title, for example `refactor: remove unreachable fork activities and
the RoundedRaff and Lyra Extended themes`, and require the PR body to note the
fallback to Lyra.

VERDICT: CLEAR
