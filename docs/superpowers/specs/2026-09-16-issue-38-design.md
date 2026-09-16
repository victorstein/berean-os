# Remove Show page as QR

**Date:** 2026-09-16
**Status:** Design v1
**Target:** bereanOS, `x4pro` build target (ESP32-S3, 8 MB PSRAM, 800x480 e-ink)
**Delivery:** `origin` (victorstein/berean-os), branch `refactor/38-remove-qr-display`
**Closes:** #38
**Builds on:** `docs/superpowers/research/2026-09-16-issue-38-research.md`
**Modelled on:** `docs/superpowers/specs/2026-09-13-bible-chapter-status-and-hint-design.md`
— the nearest existing example: a small, surgical two-file reader change with a
Goal / Non-goals / Design / Risks / Testing shape. This spec keeps that shape and
adds the sections the brief requires (Architecture, Flow, Error handling,
Assumptions).

## Problem

`MenuAction::DISPLAY_QR` (`src/activities/reader/EpubReaderMenuActivity.cpp:77`)
offers a reader-menu row that renders the current page's text as a QR code for
the user to scan to a phone. It does not work, and it fails quietly.

The issue attributes this to truncation at
`MAX_QR_CAPACITY = 2953` (`src/util/QrUtils.cpp:19`). The research found a worse
mechanism, and it is the one this removal actually rests on:

1. **The version ladder over-fills every rung.** `src/util/QrUtils.cpp:28-32`
   selects a QR version from payload length using thresholds
   (114 / 395 / 1066 / 2110) that are not byte-mode capacities. The true ECC_LOW
   byte capacities, computed from the library's own
   `NUM_RAW_DATA_MODULES` / `NUM_ERROR_CORRECTION_CODEWORDS` tables, are
   78 / 271 / 858 / 1732 / 2953. Reader text is mixed case, so the encoder takes
   the byte-mode branch (`qrcode.c:676-682`) — the column that does not apply.
2. **Over-capacity cannot be detected.** `bb_appendBits` (`qrcode.c:209-215`)
   writes without consulting `BitBucket::capacityBytes`;
   `encodeDataCodewords` (`qrcode.c:629-687`) never returns a negative mode; and
   `qrcode_initBytes` (`qrcode.c:775-849`) returns `-1` only on `mode < 0`,
   otherwise falling through to `return 0` at `qrcode.c:848`. So
   `qrcode_initText` always returns 0, the `if (res == 0)` guard at
   `src/util/QrUtils.cpp:42` is always taken, and the
   `LOG_ERR("QR", "Text too large…")` at `src/util/QrUtils.cpp:64` is dead code
   that has never fired on any device.
3. **A real reader page lands in a broken band.** The payload is one rendered
   page — `Section::getTextFromSectionFile()` (`lib/Epub/Epub/Section.cpp:799-817`)
   calls `loadPage(currentPage)` and concatenates that page's words — on the
   order of 1-3 KB. That is either the 1733-2110 band (silently corrupt code) or
   the 2111-2953 band (valid, but truncated).
4. **Even a correct code would render at 2 px per module.** Portrait is 480x800
   logical (`lib/GfxRenderer/GfxRenderer.cpp:1856-1868` returns `panelHeight` for
   `Portrait`), `src/activities/reader/QrDisplayActivity.cpp:36` takes
   `480 - 40 = 440`, and `px = min(w,h) / qrcode.size` is integer division
   (`src/util/QrUtils.cpp:44-47`). A V40 code is 177 modules: `440 / 177 = 2`.

All `qrcode.c` line numbers above are **ricmoo/QRCode 0.0.1** as pinned at
`platformio.ini:152` and resolved into `.pio/libdeps/x4pro/QRCode/src/qrcode.c`
(872 lines) — not the project's `master`, which is four lines longer and against
which `:852` lands on a different statement.

The secondary argument — it is a general-reader carryover with no study role — is
the issue's, and stands, but the defect above is sufficient on its own.

## Goal

Delete the Show-page-as-QR feature from the reader: the menu row, its
`MenuAction` enumerator, its handler, the activity that draws it, and its line in
the user guide. Leave the build green and every other reader-menu row untouched.

## Non-goals

- **Deleting `src/util/QrUtils.{h,cpp}`.** It has three live callers
  (`src/activities/network/CrossPointWebServerActivity.cpp:445,463,483`). See A1.
- **Removing `ricmoo/QRCode` from `platformio.ini:152`.** `src/util/QrUtils.cpp:4`
  includes `<qrcode.h>` and survives. See A1.
- **Fixing the version ladder or the unbounded write in the library.** No caller
  reaches an overflowing band after this change. See A4.
- **Auto page turn** (`EpubReaderMenuActivity.cpp:74`,
  `EpubReaderActivity.cpp:54`). A deliberate non-removal in the issue.
- **The orientation rows** in these same two files — a second task owns them.
- **`lib/I18n/translations/*.yaml`.** A separate task owns the translation files;
  `STR_DISPLAY_QR` stays in place and becomes a genuine orphan for it to collect.
- **Removing `CrossPointWebServerActivity`.** See A2.

---

## Assumptions

Every behavioural decision in this spec, stated so the review can attack it.

**A1 — Acceptance criteria 2 and 3 are restated, not met — one because it cannot
be done, one because it should not be.** The issue asks for `QrUtils.{h,cpp}`
deleted and `ricmoo/QRCode` dropped. The two are not alike:

- **AC-3 is not achievable.** The library stays linked for the web server.
  `CrossPointWebServerActivity` compiles: `platformio.ini` declares no
  `build_src_filter`, so every translation unit under `src/` is built, and
  `src/activities/ActivityManager.cpp:19,201` include and instantiate the class.
- **AC-2's `QrUtils` half is achievable, and declined.** `drawQrCode` is a single
  free function in a namespace (`src/util/QrUtils.h:9-14`, 55 lines of body at
  `src/util/QrUtils.cpp:11-66`) whose dependencies — `Utf8.h`, `<qrcode.h>`,
  `Logging.h`, `GfxRenderer`/`Rect` — `CrossPointWebServerActivity.cpp` already
  has. Once `QrDisplayActivity.cpp:9` goes, its only remaining external include is
  `CrossPointWebServerActivity.cpp:20`. Relocating the function into an anonymous
  namespace there would delete `src/util/QrUtils.{h,cpp}` to the letter and keep
  the build green.

*Decision:* deliver AC-1 and AC-4 as written and the `QrDisplayActivity` half of
AC-2; record AC-3 as not achievable; **decline** the relocation, because it
satisfies AC-2's wording while defeating its intent — AC-2 and AC-3 together were
plainly aimed at removing the *capability*, and moving a file to claim the
criterion buys nothing while putting churn into the subsystem A2 has just decided
to leave alone. *Attack surface:* an orchestrator may prefer to fail the issue
rather than restate its criteria, or may want the relocation done anyway so the
criterion reads as met.

**A2 — Scope stays at the reader; the web server is a separate question.**
`CrossPointWebServerActivity` is unreachable from the UI — `goToFileTransfer()`
(`src/activities/ActivityManager.cpp:200-202`) has exactly one caller,
`src/activities/home/HomeActivity.cpp:325`, and `HomeActivity` is never
instantiated anywhere. Removing it would make A1 moot and delete a large
unreachable subsystem. *Decision:* do not. It also deletes the documented dev
side-load path (`docs/webserver.md`, `docs/webserver-endpoints.md`, and the
"Flashing" section of `CLAUDE.md`), which is a product decision, not a
consequence of removing a broken menu row. *Attack surface:* someone may argue
the two removals belong in one change because AC-3 only becomes true together.

**A3 — The flash saving is not the point and will be small, and the issue's
baseline is not directly comparable.** With the library still linked, this change
removes `QrDisplayActivity.{h,cpp}` (67 lines) and a 12-line case, so expect a
single-digit-KB delta — the expected result, not a shortfall. The issue's
5,318,674 B baseline has **no recorded measurement method**: it appears nowhere in
the tree but the research note and this spec, and it is *smaller* than both
figures in `docs/superpowers/notes/phase-0-baseline.md:7,38` (5,628,560 B and
5,441,200 B, each labelled "`x4pro` dev `firmware.bin`") despite predating three
feature merges — which points at pio's `Flash: used N bytes` line rather than a
`firmware.bin` size. **Confirmed during planning:** a pre-change build of this
worktree reports `Flash: used 5318874 bytes` against a `firmware.bin` of
5,319,376 B — 200 bytes from the issue's number, the gap explained by
`BEREAN_VERSION` carrying this branch's longer name. The issue's baseline is a
`Flash:` figure. Comparing it to a `firmware.bin` size would manufacture a
~120 KB movement that contradicts the real result. *Decision:* the hand-back records **both**
`ls -l .pio/build/x4pro/firmware.bin` and pio's `Flash:` line, and compares
against the measured 5,318,874 B `Flash:` baseline rather than the issue's
number, since only the former was measured here the same way. Note also that
`BEREAN_VERSION` embeds the branch name and short SHA (`platformio.ini:168`,
`scripts/git_branch.py`), so byte-exact cross-branch comparison carries a few
bytes of unavoidable noise. *Attack surface:* the issue frames dependency removal
as a deliverable; this reframes it as unavailable and declines its baseline as
unusable without a stated method.

**A4 — The latent unbounded write in `ricmoo/QRCode` is recorded, not fixed.**
After this change the surviving callers pass 20-50 bytes: `WIFI:T:nopass;S:<ssid>;;`
is 18 bytes plus an SSID that caps at 32 characters, and
`http://berean.local/` is 20 bytes (`AP_HOSTNAME`,
`src/activities/network/CrossPointWebServerActivity.cpp:27`). All sit in the V4
band well under its true 78-byte capacity, so nothing overflows today.
*Decision:* out of scope; open a follow-up issue rather than widen this one.
*Attack surface:* leaving a known unbounded write in the tree is a judgement
call, and a reviewer may want a one-line length guard in `QrUtils::drawQrCode`
as cheap insurance.

**A5 — The stale row-count comment is corrected in passing.**
`src/activities/reader/EpubReaderMenuActivity.h:53-56` claims "13 unconditional
items … reaches exactly 18 on an X4 Pro". Counting
`buildMenuItems` (`EpubReaderMenuActivity.cpp:44-81`) gives 11 unconditional and
5 conditional (FOOTNOTES, BOOKMARKS, HIGHLIGHTS, HIGHLIGHT_PASSAGE, FRONTLIGHT)
= 16 — the comment is already wrong by two on `main`. This change makes it 10 and
15. *Decision:* correct the numbers to 10 / 15 rather than leave a comment that
is wrong for a second reason. *Attack surface:* it is an unrelated fix inside a
file another task is also editing; a reviewer may want it left alone or split
out.

**A6 — `MAX_MENU_ITEMS` stays at 24.** `EpubReaderMenuActivity.h:57` sizes
`menuRowItems[]` and the comment calls it "a capacity, not a contract".
*Decision:* do not shrink it. The array is `freeink::ui::ListItem` and shrinking
it trades a re-verification of the clamping loops for a few hundred bytes of an
object that is not the binding constraint. *Attack surface:* an embedded reviewer
may reasonably want the array sized to the real maximum.

**A7 — The i18n orphan gate is expected to go from 22 to 23.** Both uses of
`STR_DISPLAY_QR` disappear (`EpubReaderMenuActivity.cpp:77` and
`QrDisplayActivity.cpp:34`), so `scripts/i18n_orphans.sh` gains exactly one line.
*Decision:* treat 23 as the passing result for this change, not a regression.
Note that `docs/superpowers/notes/phase-0-baseline.md` records "0 orphans" as the
Phase 0 gate; the tree is at 22 today, so that baseline has drifted and the gate
must be read as a delta, not an absolute. *Attack surface:* someone may want the
English key removed here and the other 30 languages left to the translation task.

---

## Architecture

A deletion, not a refactor. Nothing is added and no call site changes shape.

### What goes

| File | Change |
| --- | --- |
| `src/activities/reader/QrDisplayActivity.h` | delete (20 lines) |
| `src/activities/reader/QrDisplayActivity.cpp` | delete (47 lines) |
| `src/activities/reader/EpubReaderMenuActivity.h:26` | delete the `DISPLAY_QR,` enumerator |
| `src/activities/reader/EpubReaderMenuActivity.h:53-56` | correct the row counts to 10 / 15 (A5) |
| `src/activities/reader/EpubReaderMenuActivity.cpp:77` | delete the `push_back` |
| `src/activities/reader/EpubReaderActivity.cpp:39` | delete `#include "QrDisplayActivity.h"` |
| `src/activities/reader/EpubReaderActivity.cpp:828-839` | delete the `case` |
| `USER_GUIDE.md:140` | delete the bullet |

### What stays, and why

- **`src/util/QrUtils.{h,cpp}` and `ricmoo/QRCode`** — A1.
- **`STR_DISPLAY_QR` in all 31 YAMLs that define it** (`finnish.yaml` never had
  it) — the translation task owns them. `STR_SCAN_QR_HINT` is untouched; it
  belongs to the web server (`CrossPointWebServerActivity.cpp:477`).
- **`utf8SafeTruncateBuffer`** (`src/util/QrUtils.cpp:23`) — four other callers
  (`src/network/PubMediaJson.cpp:16`,
  `src/activities/reader/BibleNavigationActivity.cpp:28`, `lib/Utf8/Utf8.cpp:199`,
  `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:1291`). Not orphaned.
- **`Section::getTextFromSectionFile()`** — still used for bookmark summaries
  (`src/activities/reader/EpubReaderActivity.cpp:1778`). Not orphaned.

### Why the enum edit is safe

`MenuAction` (`EpubReaderMenuActivity.h:14-31`) is an unpersisted `enum class`.
Its only journey outside the type is `MenuResult::action`, an `int`
(`src/activities/ActivityResult.h:24-28`), set on the way out of the menu
activity and cast back at `EpubReaderActivity.cpp:291`. Both ends are in the same
binary and the same call, so removing a middle enumerator renumbers the ones
after it with no on-disk or cross-version consequence. Nothing writes a
`MenuAction` to SD; there is no format version to bump.

`onReaderMenuConfirm` (`EpubReaderActivity.cpp:687-881`) has **no `default:`
label**, but it is *not* exhaustive and never has been: it carries 14 cases
against 16 enumerators. `AUTO_PAGE_TURN` and `ROTATE_SCREEN` have none, because
`activateIndex` consumes them in the menu activity and returns before `setResult`
(`EpubReaderMenuActivity.cpp:104-127`, against the single `setResult` at `:145`).

So there is **no `-Wswitch` safety net**, in two independent ways: the switch was
already non-exhaustive on `main`, and the build enables no warning that would say
so — `platformio.ini` adds no `-Wall` and nothing anywhere is `-Werror`
(`grep -cE '\-Wall|\-Werror' platformio.ini` → 0; `.github/workflows/ci.yml`'s
build job is a bare `pio run`).

What *is* enforced is one-directional:

| Half-applied deletion | Result |
| --- | --- |
| enumerator removed, `case` left behind | **hard compile error** — name lookup fails |
| `case` removed, enumerator left behind | **compiles and ships silently** |

The grep gate is the only thing that catches the silent direction. Keep the
absence of a `default:` — but for consistency with the file's neighbours, not
because it preserves a gate it does not preserve.

## Data and control flow

**Today**, one path exists and is broken:

```
reader menu row tapped
  -> activateIndex()            EpubReaderMenuActivity.cpp:96
  -> MenuResult{ action=int }   ActivityResult.h:24
  -> onReaderMenuConfirm()      EpubReaderActivity.cpp:291, 687
  -> case DISPLAY_QR            EpubReaderActivity.cpp:828
       section->getTextFromSectionFile()        one page, ~1-3 KB
  -> QrDisplayActivity(textPayload)             EpubReaderActivity.cpp:832
  -> QrUtils::drawQrCode()                      QrDisplayActivity.cpp:41
  -> qrcode_initText(version from the bad ladder)
  -> a corrupt or truncated code, always res == 0, no error anywhere
```

**After**, the path does not exist. The row is absent from
`buildMenuItems`, so `activateIndex` can never produce the value, and the
enumerator it would have produced is gone. No fallback, no "feature removed"
message, no stub activity.

The three web-server call sites keep exactly the flow they have today; none of
them route through the reader, and none of their payloads change.

## Error handling

There is no new failure mode to handle — that is the point of preferring deletion
to a fix here.

- **No runtime error path is added.** The change removes a `case`; it does not
  introduce a branch that can fail.
- **The dead error path goes with it.** `QrDisplayActivity` was the only caller
  of `drawQrCode` that could ever have tripped
  `LOG_ERR("QR", "Text too large for QR Code version %d")`
  (`src/util/QrUtils.cpp:64`) — and it could not, because
  `qrcode_initText` always returns 0. That log line stays in the tree for the web
  server's sake but is now unreachable from any payload that exists. A4 records
  this rather than fixing it.
- **The compiler catches one half-applied deletion, not both.** Removing the
  enumerator while leaving its `case` is a hard name-lookup error. Removing the
  `case` while leaving the enumerator compiles and ships — the switch is already
  non-exhaustive (14 cases, 16 enumerators) and the build enables no `-Wall`, so
  no `-Wswitch` diagnostic exists to fire. See "Why the enum edit is safe". The
  grep gate below is the only guard on the silent direction, and it is therefore
  a required step, not a convenience. The implementer must not add a `default:` —
  it would suppress a future diagnostic without adding one today.
- **No `LOG_ERR` + return false convention applies**, because nothing new can
  fail (`CLAUDE.md`, "Error handling").

## Testing strategy

**Host** — nothing to add. No suite in `test/` references
`EpubReaderMenuActivity`, `QrDisplayActivity` or `QrUtils`, and none should be
written for a deletion: the behaviour under test is absence, which the compiler
and the gates below already assert. The existing suite must still pass unchanged.

**Gates**, in this order, after the last code edit:

```sh
# 1. Build first — it regenerates lib/I18n/I18nKeys.h, which gate 2 greps.
~/.platformio/penv/bin/pio run
ls -l .pio/build/x4pro/firmware.bin        # AC-4; also record pio's "Flash:" line

# 2. Completeness. The greps are the only guard on the silent half-deletion.
grep -rn "DISPLAY_QR\|QrDisplayActivity" src lib \
  --include='*.cpp' --include='*.h' | grep -v "I18nKeys.h\|I18nStrings"
                                                     # expect no hits (13 before)
grep -rn "QrUtils\|qrcode\.h" src                   # expect 10 lines: 6 in QrUtils.{h,cpp},
                                                     # 4 in CrossPointWebServerActivity.cpp
                                                     # (:20 include + :445,:463,:483)
./scripts/i18n_orphans.sh                            # expect 23 lines (was 22) — A7

# 3. The other two CI jobs. A pure deletion should pass both untouched.
~/.platformio/penv/bin/pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release   # NOT -G Ninja: ninja is
cmake --build build/test                                 # not installed here
ctest --test-dir build/test --output-on-failure -j

# 4. Format last, over the whole tree, as CI does.
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
```

**The completeness grep must exclude the YAMLs and the generated i18n files.**
`STR_DISPLAY_QR` is defined in 31 files under `lib/I18n/translations/`, which the
non-goals forbid touching, and in the generated `lib/I18n/I18nKeys.h` and
`I18nStrings.*` — so an unfiltered `grep … src lib` can never reach zero. The
filtered form above returns 13 lines before the change and must return 0 after.
(Found while writing the plan; the plan uses the corrected form.)

**Order matters for the orphan gate.** `scripts/i18n_orphans.sh` greps `src lib`,
which includes the build-generated `lib/I18n/I18nKeys.h` (`.gitignore`). That file
lists only *used* keys and today still contains `STR_DISPLAY_QR`
(`lib/I18n/I18nKeys.h:420`) — so running the gate against a stale copy reports 22
and reads as "the deletion added no orphan", the exact inverse of what A7 uses it
for. Run it after `pio run`, never before. Do not pipe it to `wc -l`: it is
`set -euo pipefail` and the pipe discards its exit status.

`.github/workflows/ci.yml` gates on four jobs behind `test-status` — `build`,
`clang-format`, `cppcheck` and `unit-tests`. All four are above; `pio check` and
the host suite are cheap here precisely because nothing in this change should move
them.

The `grep` gates are the substitute for a host test. `pio run` alone cannot prove
the deletion is complete — an orphaned include or a stale reference in a file the
host suite never compiles would still link, and as the Error handling section
records, a deleted `case` with a surviving enumerator compiles silently.

Build **once**, after the last edit. Do not clean, and do not rebuild after
formatting or documentation-only changes (`CLAUDE.md`, "Testing checklist").

**Device — the human tester's, not claimable here:**

1. Open a publication, open the reader menu: the **Show page as QR** row is gone
   and every other row is in its previous order.
2. Rows below where it sat — **Go home**, **Delete book cache** — still activate
   the right handler. This is the renumbering check, and it is the one thing a
   silent `MenuAction` mistake would break.
3. **Take screenshot** (the row immediately above) still writes to
   `screenshots/`.
4. Menu scrolling and the touch grid still land on the right row now that the
   list is one shorter.
5. Free heap after entering and leaving the reader menu is unchanged from before
   the change (`ESP.getFreeHeap()`, above ~50 KB).
6. No SD cache change: nothing here touches a format version, so
   `/.crosspoint/` must **not** need clearing. If a book re-indexes after this
   change, something is wrong.

## Risks

- **Two other tasks are editing these same two files** — one owns the orientation
  rows, one owns the translation YAMLs. The QR diff must touch only the rows in
  the table above. A5 deliberately edits a comment in the shared header; if that
  collides, drop A5 rather than the deletion.
- **The renumbering is invisible until it is wrong.** Nothing on screen says
  which `MenuAction` a row carries. Device check 2 is the only thing that catches
  a mismatch that still compiles, which is why it is listed first among the rows
  below the deletion.
- **AC-2 and AC-3 will both read as unmet to anyone reading the issue and not
  this spec** — and for different reasons, which the hand-back must keep apart.
  AC-3 is impossible; AC-2's `QrUtils` half is possible by relocating `drawQrCode`
  and is *declined*. Claiming both are impossible is disprovable in two minutes
  and would cost the hand-back its credibility. Lead with A1, do not bury it.
- **`docs/superpowers/notes/phase-0-baseline.md` records an i18n orphan gate of
  0 that is now 22.** Anyone using that note as an absolute gate will read this
  change as adding 23 orphans. A7 states the delta.

## Open questions

None blocking. A1 and A2 are decisions for the orchestrator to ratify or
overturn, and are written as assumptions rather than questions so that the
default — proceed as specified — is the reviewable one.

---

## Review pass 0 — what changed and why

`docs/superpowers/reviews/issue-38-spec-review-0.md` returned **CLEAR** with
0 BLOCKER, 2 MAJOR and 5 MINOR. All were applied to this document; none changed
what gets deleted. Recorded so the next pass does not re-derive them:

| Finding | Change |
| --- | --- |
| MAJOR 1 | The `-Wswitch` "compile-time gate" was fiction. The switch carries 14 cases against 16 enumerators (`AUTO_PAGE_TURN`, `ROTATE_SCREEN` never reach it) and the build sets no `-Wall` or `-Werror`. "Why the enum edit is safe" and the Error handling section now state the one-directional enforcement honestly, and the grep gate is promoted from convenience to required. |
| MAJOR 2 | A1 claimed both AC-2 and AC-3 were impossible. Only AC-3 is. AC-2's `QrUtils` half is achievable by relocating `drawQrCode` into `CrossPointWebServerActivity.cpp`; A1 now *declines* it as churn defeating the criterion's intent, and the Risks bullet keeps the two reasons apart. |
| MINOR 3 | Every `qrcode.c` citation was against `master`, four lines adrift from the pinned `0.0.1` that actually links — `:852` landed on a different statement. Re-cited against `.pio/libdeps/x4pro/QRCode/src/qrcode.c` and the version is now named. The same correction was applied to the research note. |
| MINOR 4 | The gate list covered 2 of CI's 4 gating jobs. `pio check` and the host `ctest` suite added. |
| MINOR 5 | The orphan gate greps the build-generated `lib/I18n/I18nKeys.h`, which still lists `STR_DISPLAY_QR` until `pio run` regenerates it — running it first is a false pass reading 22. Ordering is now explicit and the `wc -l` pipe (which discarded the script's exit status) is gone. |
| MINOR 6 | A3 had adopted the issue's 5,318,674 B baseline despite the research listing it as unverified. It is smaller than both figures in `phase-0-baseline.md` and is probably pio's `Flash:` line, not a `firmware.bin` size; the hand-back now records both numbers and refuses the comparison without a stated method. |
| MINOR 7 | The `QrUtils\|qrcode.h` gate's expectation undercounted (10 lines, 4 of them in `CrossPointWebServerActivity.cpp` including its include) and left `.` unescaped. Both fixed. |

## Amendments from the plan phase

Writing `docs/superpowers/plans/2026-09-16-issue-38-plan.md` surfaced three more
corrections to this document, applied above:

| | Change |
| --- | --- |
| Completeness grep | The unfiltered `grep -rn "DISPLAY_QR\|QrDisplayActivity" src lib` can never reach zero: `STR_DISPLAY_QR` lives in 31 files under `lib/I18n/translations/` that the non-goals forbid touching, plus the generated `I18nKeys.h`/`I18nStrings.*`. Filtered, it is 13 → 0. |
| `-G Ninja` | Ninja is not installed on this machine; the host-suite command now uses the default generator, which is verified working. CI installs `ninja-build` and is unaffected. |
| A3's baseline | No longer a suspicion. A pre-change build reports `Flash: used 5318874 bytes` against a `firmware.bin` of 5,319,376 B — 200 bytes from issue #38's 5,318,674, the gap being `BEREAN_VERSION`'s branch name. The issue's figure is a `Flash:` line, and the comparison is made against the measured one. |

Plan review pass 0 (`docs/superpowers/reviews/issue-38-plan-review-0.md`) returned
CLEAR with 0 BLOCKER, 2 MAJOR, 7 MINOR; all were applied to the plan, and the two
spec-affecting ones are in the table above. It re-measured every numeric claim in
the plan and all of them held.

The one finding worth carrying forward into implementation: **a deleted `case`
with a surviving enumerator compiles and ships silently.** Nothing in the build
catches it. The grep gate is the whole guard.
