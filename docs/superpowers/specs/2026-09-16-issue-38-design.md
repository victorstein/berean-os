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
   the byte-mode branch (`qrcode.c:681`) — the column that does not apply.
2. **Over-capacity cannot be detected.** `bb_appendBits` (`qrcode.c:213-219`)
   writes without consulting `BitBucket::capacityBytes`;
   `encodeDataCodewords` (`qrcode.c:633-691`) never returns a negative mode; and
   `qrcode_initBytes` (`qrcode.c:779-853`) returns `-1` only on `mode < 0`,
   otherwise falling through to `return 0` at `qrcode.c:852`. So
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

**A1 — Acceptance criteria 2 and 3 are restated, not met.** The issue asks for
`QrUtils.{h,cpp}` deleted and `ricmoo/QRCode` dropped. Both are impossible while
`CrossPointWebServerActivity` compiles, and it does: `platformio.ini` declares no
`build_src_filter`, so every translation unit under `src/` is built, and
`src/activities/ActivityManager.cpp:19,201` include and instantiate the class.
*Decision:* deliver AC-1 and AC-4 as written, deliver the `QrDisplayActivity` half
of AC-2, and record AC-2's `QrUtils` half and AC-3 as not achievable.
*Attack surface:* an orchestrator may prefer to fail the issue rather than
restate its criteria.

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

**A3 — The flash saving is not the point and will be small.** With the library
still linked, this change removes `QrDisplayActivity.{h,cpp}` (67 lines) and a
12-line case. *Decision:* report the `pio run` figure against the 5,318,674 B
baseline as AC-4 asks, and state plainly that a single-digit-KB delta is the
expected result, not a shortfall. *Attack surface:* the issue frames dependency
removal as a deliverable; this reframes it as unavailable.

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

`onReaderMenuConfirm` (`EpubReaderActivity.cpp:687-881`) switches over every
enumerator with **no `default:` label**. That is load-bearing in our favour: with
`-Wswitch` the compiler flags the pair falling out of step, so the enumerator and
its case cannot be removed independently without the build saying so.

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
- **The compile-time gate is the real error handling.** The `-Wswitch`
  exhaustiveness of `onReaderMenuConfirm` means a half-applied deletion — the
  enumerator removed but not the case, or the reverse — fails the build rather
  than shipping. The implementer must not add a `default:` to silence it.
- **No `LOG_ERR` + return false convention applies**, because nothing new can
  fail (`CLAUDE.md`, "Error handling").

## Testing strategy

**Host** — nothing to add. No suite in `test/` references
`EpubReaderMenuActivity`, `QrDisplayActivity` or `QrUtils`, and none should be
written for a deletion: the behaviour under test is absence, which the compiler
and the gates below already assert. The existing suite must still pass unchanged.

**Gates**, in this order, after the last code edit:

```sh
~/.platformio/penv/bin/pio run            # AC-4; record the firmware.bin size
./scripts/i18n_orphans.sh | wc -l         # expect 23 (was 22) — A7
grep -rn "DISPLAY_QR\|QrDisplayActivity" src lib   # expect no hits
grep -rn "QrUtils\|qrcode.h" src                   # expect only QrUtils.{h,cpp} + the 3 web-server sites
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix  # whole tree, as CI does
```

The `grep` gates are the substitute for a host test. `pio run` alone cannot prove
the deletion is complete — an orphaned include or a stale reference in a file the
host suite never compiles would still link.

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
- **AC-3 will read as unmet to anyone reading the issue and not this spec.** The
  hand-back must lead with A1, not bury it.
- **`docs/superpowers/notes/phase-0-baseline.md` records an i18n orphan gate of
  0 that is now 22.** Anyone using that note as an absolute gate will read this
  change as adding 23 orphans. A7 states the delta.

## Open questions

None blocking. A1 and A2 are decisions for the orchestrator to ratify or
overturn, and are written as assumptions rather than questions so that the
default — proceed as specified — is the reviewable one.
