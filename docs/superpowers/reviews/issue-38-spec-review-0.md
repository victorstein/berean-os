# Adversarial review — `2026-09-16-issue-38-design.md`, pass 0

Reviewed 2026-09-16 on `09bada58` in the `refactor/38-remove-qr-display` worktree,
against issue #38 and `docs/superpowers/research/2026-09-16-issue-38-research.md`.

**Summary: 0 BLOCKER, 2 MAJOR, 4 MINOR.** The removal set is complete and correct,
and every citation into this repository's own source checks out at the quoted line.
The two MAJORs are both bad reasoning attached to correct conclusions: a
compile-time safety net that does not exist, and an "impossible" that is really a
"declined". Neither changes what gets deleted; both change what the spec may claim.

## What I verified and found sound

- **The removal set is exhaustive.** `grep -rn "QrUtils\|qrcode\.h\|QRCode\|DISPLAY_QR\|QrDisplayActivity" src lib test platformio.ini USER_GUIDE.md docs`
  returns nothing outside the spec's table, the three web-server sites, the 31
  translation YAMLs, and `platformio.ini:152`. Nothing is missed.
- **A1's premise (three live callers).** `src/activities/network/CrossPointWebServerActivity.cpp:20,445,463,483`;
  `src/activities/ActivityManager.cpp:19,200-202`; `src/activities/home/HomeActivity.cpp:325`.
- **A2 (`HomeActivity` never instantiated).** `grep -rn "HomeActivity>(\|HomeActivity("
  src/` outside its own files returns only `isHomeActivity()` overrides;
  `src/activities/launcher/LauncherActivity.h:31` took its place.
- **A5's arithmetic.** `buildMenuItems` (`src/activities/reader/EpubReaderMenuActivity.cpp:44-81`)
  is 11 unconditional + 5 conditional = 16 today; 10 / 15 after. The header comment
  (`EpubReaderMenuActivity.h:53-56`) really does say 13 / 18.
- **A7's baseline.** `./scripts/i18n_orphans.sh | wc -l` → `22`. 31 YAMLs define
  `STR_DISPLAY_QR`; `finnish.yaml` does not. `docs/superpowers/notes/phase-0-baseline.md:9,43`
  really does record "0".
- **The index-safety argument.** `activateIndex` reads `menuItems[index].action`
  (`EpubReaderMenuActivity.cpp:103`) and `buildScreen` drives everything off
  `menuItems` (`:193-213`). No hardcoded row index anywhere; removing one
  `push_back` cannot desynchronise a row from its handler.
- **The QR mechanism itself**, re-derived against the *installed* `ricmoo/QRCode @ 0.0.1`
  (`.pio/libdeps/x4pro/QRCode/src/qrcode.c`, `library.properties` → `version=0.0.1`):
  `bb_appendBits` has no bounds check (`:209-215`); `capacityBytes` is stored at
  `:195` and read only by `bb_initBuffer`/`bb_initGrid`; `encodeDataCodewords`
  returns `MODE_BYTE`/`MODE_ALPHANUMERIC`/`MODE_NUMERIC` and never a negative
  (`:629-687`); `qrcode_initBytes` returns `-1` only at `:800` and otherwise
  `return 0` at `:848`. So `qrcode_initText` always returns 0 and
  `src/util/QrUtils.cpp:64` is dead. The V4 ECC_LOW figure checks out from the
  library's own tables: `NUM_ERROR_CORRECTION_CODEWORDS[Low][3] = 20`
  (`qrcode.c:44`), 807/8 − 20 = 80 codewords = 640 bits, less 4 mode bits and 8
  count bits (`getModeBits`, `:146-160`) = 78 bytes, against the ladder's 114.
- **`Section::getTextFromSectionFile()` is one page.** `lib/Epub/Epub/Section.cpp:799-817`
  calls `loadPage(currentPage)`, exactly as the research says; the bookmark caller
  is `src/activities/reader/EpubReaderActivity.cpp:1778`.
- **`utf8SafeTruncateBuffer`'s four other callers** are at exactly the four quoted
  lines.
- **The format gate runs.** `.venv/bin/clang-format` exists in this worktree, so
  `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` will not trip the
  wrapper's exit-1 path (`bin/clang-format-fix:4-12`).

---

## MAJOR 1 — the `-Wswitch` "compile-time gate" does not exist, and the switch is not exhaustive

**Claim.** Spec lines 190-193: "`onReaderMenuConfirm` … switches over **every**
enumerator with **no `default:` label**. That is load-bearing in our favour: with
`-Wswitch` the compiler flags the pair falling out of step, so the enumerator and
its case cannot be removed independently without the build saying so."
Spec lines 234-237 escalate it: "**The compile-time gate is the real error
handling.** … a half-applied deletion … fails the build rather than shipping."

**Problem.** Both halves are false.

*(a) The switch covers 14 of 16 enumerators.*

```
$ grep -c "case EpubReaderMenuActivity::MenuAction::" src/activities/reader/EpubReaderActivity.cpp
14
```

at lines 725, 764, 777, 795, 799, 802, 820, 824, 828, 840, 844, 862, 870, 876.
`MenuAction` (`src/activities/reader/EpubReaderMenuActivity.h:14-31`) has 16
enumerators. `AUTO_PAGE_TURN` and `ROTATE_SCREEN` have **no case**, because
`activateIndex` consumes them in the menu activity and returns before `setResult`
(`EpubReaderMenuActivity.cpp:104-127`, against the single `setResult` at `:145`).
So the switch has been non-exhaustive on `main` all along. Note that `NIGHT_MODE`
and `FRONTLIGHT` return early the same way (`:129-143`) yet *do* carry empty cases
— the file is already inconsistent about this, which is precisely why "every
enumerator" should not have been asserted without counting.

*(b) `-Wswitch` is not enabled, and nothing is `-Werror`.* From the live build
configuration, not from `platformio.ini` alone:

```
$ ~/.platformio/penv/bin/pio run -e x4pro -t idedata
  cxx_flags warning entries:
    -Wno-enum-conversion  -Wno-error=deprecated-declarations  -Wno-error=extra
    -Wno-error=unused-but-set-variable  -Wno-error=unused-function
    -Wno-error=unused-variable  -Wno-sign-compare  -Wno-unused-parameter
    -Wwrite-strings  -Wno-bidi-chars
  occurrences of "-Wall" in the whole idedata dump: 0
  occurrences of "-Werror"  in the whole idedata dump: 0
```

GCC enables `-Wswitch` only under `-Wall`. `platformio.ini:31-72` adds no warning
flag but `-Wno-bidi-chars`, and `.github/workflows/ci.yml`'s build job is a bare
`pio run -e ${{ matrix.env }}` with no warning gate. Even if `-Wswitch` were on, it
would emit a warning the build ignores — and it would already be emitting two.

What is actually enforced is one-directional: deleting `DISPLAY_QR` from the enum
while leaving `case EpubReaderMenuActivity::MenuAction::DISPLAY_QR:` is a hard
name-lookup error. The reverse — case deleted, enumerator left behind — compiles
silently and ships.

**Why it matters.** The Testing strategy declines host tests on this basis
("the behaviour under test is absence, which **the compiler** and the gates below
already assert", lines 243-246). Strip the compiler out of that sentence and the
grep gate is carrying the whole load by itself. It can, but the spec should say so
rather than resting on a net that is not there.

**Fix (inline).**
1. Line 190: replace "switches over every enumerator" with "switches over 14 of the
   16 enumerators — `AUTO_PAGE_TURN` and `ROTATE_SCREEN` never reach it, they are
   consumed in `activateIndex` (`EpubReaderMenuActivity.cpp:104-127`)".
2. Lines 191-193 and 234-237: state the enforcement honestly — removing the
   enumerator while keeping the case is a compile error; removing the case while
   keeping the enumerator is silent, because `-Wall` is absent from the build
   (verified via `pio run -t idedata`) and nothing is `-Werror`.
3. Promote `grep -rn "DISPLAY_QR\|QrDisplayActivity" src lib` from "the substitute
   for a host test" to the primary completeness check, and say it is the only thing
   that catches the silent direction.
4. Keep "do not add a `default:`", but on the honest ground that it keeps the file
   consistent with its neighbours, not that it preserves a gate.

---

## MAJOR 2 — A1 says "impossible" where the truth is "achievable and declined"

**Claim.** Spec lines 82-85: "The issue asks for `QrUtils.{h,cpp}` deleted and
`ricmoo/QRCode` dropped. **Both are impossible** while `CrossPointWebServerActivity`
compiles, and it does". Research line 153 says the same ("`QrUtils` no — it has
three live callers"), and the Risks section (line 293) only prepares a hand-back for
AC-3.

**Problem.** Only AC-3 is impossible. AC-2's `QrUtils` half is achievable without
touching the web server's behaviour at all, by relocating the one function into its
one remaining consumer. `QrUtils` is a single free function in a namespace
(`src/util/QrUtils.h:9-14`), 55 lines of body (`src/util/QrUtils.cpp:11-66`), whose
only dependencies are `Utf8.h`, `<qrcode.h>`, `Logging.h` and `GfxRenderer`/`Rect`
— all of which `CrossPointWebServerActivity.cpp` already has. After
`QrDisplayActivity.cpp:9` goes, its only external include is
`CrossPointWebServerActivity.cpp:20`. Moving `drawQrCode` into an anonymous
namespace in that translation unit deletes `src/util/QrUtils.{h,cpp}` to the
letter, and the build stays green.

The spec never considers that option, so the orchestrator is being asked to ratify
a restatement of AC-2 on a premise that is not true. The premise it *should* ratify
is a judgement, and a defensible one: relocating a file to satisfy the wording of a
criterion whose evident intent (paired with AC-3) was to remove the *capability*
buys nothing, and it puts churn into a subsystem A2 has just decided to leave alone
pending a separate product decision.

**Evidence.** `src/util/QrUtils.h:9-14`; `src/util/QrUtils.cpp:1-9` (include set);
`src/activities/network/CrossPointWebServerActivity.cpp:20,445,463,483`;
`src/activities/reader/QrDisplayActivity.cpp:9` (the only other include, deleted by
this change).

**Fix (inline).** Rewrite A1's second sentence to: "AC-3 is not achievable — the
library stays linked for the web server. AC-2's `QrUtils` half is achievable only
by relocating `drawQrCode` into `CrossPointWebServerActivity.cpp`, which satisfies
the criterion's wording while defeating its intent; we decline it as churn in a
subsystem A2 has deliberately left untouched." Then widen the Risks bullet at line
293 to cover AC-2 on the same ground, so the hand-back does not claim an
impossibility a reader can disprove in two minutes.

---

## MINOR 3 — every `qrcode.c` citation is from `master`, not the pinned `0.0.1` that compiles

**Claim.** Spec lines 32-40 cite `qrcode.c:213-219` (`bb_appendBits`),
`qrcode.c:633-691` (`encodeDataCodewords`), `qrcode.c:779-853` (`qrcode_initBytes`),
`qrcode.c:852` ("`return 0`") and `qrcode.c:681` (the byte-mode branch). The
research note (line 94) says it verified against
`raw.githubusercontent.com/ricmoo/QRCode/master/src/qrcode.c`.

**Problem.** `platformio.ini:152` pins `ricmoo/QRCode @ 0.0.1`, and that is what is
resolved into `.pio/libdeps/x4pro/QRCode` (`library.properties` → `version=0.0.1`).
Every citation is off by four lines against the source that actually builds:

| Spec says | `0.0.1` actual |
| --- | --- |
| `bb_appendBits` at `:213-219` | `:209-215` |
| `encodeDataCodewords` at `:633-691` | `:629-687` |
| `qrcode_initBytes` at `:779-853` | `:775-849` |
| `return 0` at `:852` | `:848` — `:852` is `return qrcode_initBytes(...)` inside `qrcode_initText` |
| byte-mode branch at `:681` | `:676-682` |

The spec's single most load-bearing citation, "`return 0` at `qrcode.c:852`",
therefore points a reader at a different statement in the version this firmware
links. The mechanism is correct — I re-derived all of it above — but the evidence
trail does not survive being followed, which is what `CLAUDE.md`'s "a claim about
this codebase needs a file and a line, and the line must have been read" exists to
prevent.

**Fix (inline).** Re-cite against `.pio/libdeps/x4pro/QRCode/src/qrcode.c` and say
which version the line numbers belong to, e.g. "`qrcode.c:209-215` (ricmoo/QRCode
0.0.1, as pinned at `platformio.ini:152`)". Same edit in the research note.

---

## MINOR 4 — the gate list omits two of CI's four jobs

**Claim.** Spec lines 248-256 present "**Gates**, in this order, after the last code
edit" as the complete set: `pio run`, `i18n_orphans.sh`, two greps, `clang-format-fix`.
Line 246 adds "The existing suite must still pass unchanged" without a command.

**Problem.** `.github/workflows/ci.yml` runs four gating jobs behind `test-status`:
`build`, `clang-format`, `cppcheck` and `unit-tests`. The spec covers two.
`pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` is a
separate job that `pio run` does not exercise (`platformio.ini:20,24` set
`check_tool = cppcheck` and `check_flags`), and the host suite is
`cmake -S test -B build/test -G Ninja` / `cmake --build build/test` /
`ctest --test-dir build/test`. Neither appears.

**Fix (inline).** Add both commands to the gate block. For a pure deletion they
should pass untouched, which is exactly why they are cheap to list and expensive to
discover in CI.

---

## MINOR 5 — the orphan gate is order-dependent, and the spec does not say so

**Claim.** A7 (lines 139-146) predicts `./scripts/i18n_orphans.sh | wc -l` goes
22 → 23.

**Problem.** The prediction is right but fragile in a way the spec leaves unstated.
`scripts/i18n_orphans.sh` greps `src lib --include=*.cpp --include=*.h`, and
`lib/I18n/I18nKeys.h` is a build-generated header (`.gitignore:8`) that
`scripts/gen_i18n.py --strip-unused` populates with *used* keys only. It currently
contains `STR_DISPLAY_QR` (`lib/I18n/I18nKeys.h:420`) and does **not** contain
`STR_LOADING` — one of the 22 reported orphans. So running the orphan gate against a
stale `I18nKeys.h`, before `pio run` regenerates it, still finds `STR_DISPLAY_QR`
and reports 22: a false pass that reads as "the deletion added no orphan", the exact
opposite of what A7 uses the gate for. The spec's ordering happens to be correct,
but line 263 ("do not rebuild after formatting or documentation-only changes")
invites running the gates out of order on a re-check.

**Fix (inline).** One sentence under the gate block: the orphan gate must run
*after* `pio run`, because it greps the generated `lib/I18n/I18nKeys.h` and a stale
copy still lists the removed key.

---

## MINOR 6 — the AC-4 baseline's provenance is unknown and probably a different measurement

**Claim.** A3 (lines 103-108): "report the `pio run` figure against the 5,318,674 B
baseline as AC-4 asks, and state plainly that a single-digit-KB delta is the
expected result, not a shortfall."

**Problem.** `grep -rn "5,318,674\|5318674"` over the tree hits only the research
note and this spec. The research lists it under **Not verified** (lines 185-188);
A3 silently drops that caveat and adopts it as the comparison point. The only
firmware figures actually recorded in the repo are
`docs/superpowers/notes/phase-0-baseline.md:7` (5,628,560 B) and `:38` (5,441,200 B),
both labelled "`x4pro` dev `firmware.bin`" — both *larger* than the issue's number
despite predating three feature merges, which suggests 5,318,674 is `pio`'s
"Flash: used N bytes" line rather than a `firmware.bin` size. Comparing the two
produces a fictitious ~120 KB movement that would swamp, and contradict, the
"single-digit-KB" result A3 pre-commits to. Separately, the `x4pro` dev env embeds
the branch name and short SHA in `BEREAN_VERSION` (`platformio.ini:168`,
`scripts/git_branch.py:1-8,49-62`), so byte-exact cross-branch comparisons carry a
few bytes of unavoidable noise.

**Fix (inline).** Have the hand-back record **both** `ls -l .pio/build/x4pro/firmware.bin`
and pio's `Flash:` line, state that the issue's baseline has no recorded measurement
method, and say the delta is meaningful only against a figure measured the same way.

---

## MINOR 7 — the grep gates' stated expectations are wrong

**Claim.** Spec line 254:
`grep -rn "QrUtils\|qrcode.h" src   # expect only QrUtils.{h,cpp} + the 3 web-server sites`.

**Problem.** The post-change hit set is 10 lines, and **four** of them are in
`CrossPointWebServerActivity.cpp`: the include at `:20` plus the three draw calls at
`:445,463,483`. The other six are `QrUtils.h:9,14` and `QrUtils.cpp:1,4,11,14`. An
implementer comparing against "the 3 web-server sites" sees four and has to decide
whether that is the bug. Two smaller things in the same block: `qrcode.h` is an
unescaped regex (`.` matches any character), and `./scripts/i18n_orphans.sh | wc -l`
discards the script's exit status despite its `set -euo pipefail`.

**Fix (inline).** State the expected count (10 lines, four of them in
`CrossPointWebServerActivity.cpp` including its include), escape the dot, and drop
the pipe or capture the status separately.

---

## Not findings, recorded so the next pass does not re-litigate them

- **A4's payload arithmetic holds.** In AP mode `connectedSSID` is the compile-time
  `AP_SSID = "bereanOS"` (`CrossPointWebServerActivity.cpp:25`), so
  `WIFI:T:nopass;S:bereanOS;;` is 26 bytes; `http://berean.local/` is 20
  (`AP_HOSTNAME`, `:27`); the STA-mode payload is `"http://" + connectedIP + "/"`
  (`:481`), ~22 bytes. All far below the true 78-byte V4 capacity. Nothing
  overflows today, and the "open a follow-up rather than widen this one" decision
  is sound.
- **A6 (`MAX_MENU_ITEMS` stays 24).** The clamping is real and correct
  (`EpubReaderMenuActivity.cpp:36,193-197,213`), so shrinking the array would buy a
  few hundred bytes of `ListItem` at the price of re-verifying three loops. Leaving
  it alone is right.
- **The renumbering argument.** `setResult(MenuResult{static_cast<int>(selectedAction), …})`
  (`EpubReaderMenuActivity.cpp:145`) and the cast back at
  `EpubReaderActivity.cpp:291` are the same call in the same binary, and
  `MenuResult::action` is a plain `int` (`src/activities/ActivityResult.h:24-28`).
  Nothing persists a `MenuAction`. The spec is right that there is no format version
  to bump; device check 2 is belt-and-braces against a human editing slip, not
  against the renumbering itself, and is worth keeping on that basis.
- **A5's edit is worth making.** The comment is wrong by two on `main` today, and a
  deletion that changes the number is the natural moment to fix it.

VERDICT: CLEAR
