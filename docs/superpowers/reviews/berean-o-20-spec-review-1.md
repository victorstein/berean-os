Tier: heavy

# Spec review 1: berean-o-20 (t7), the reader opens a preaching reference and swipes back

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-20-design.md` at `81af9e68`. I checked it
against `hpipe brief --task t7`, the research note, the parent spec
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §6, review 0, and the code at `HEAD`.
Decision d1 (the verse-offset route, everything kept in `src/activities/reader/`) is treated as
settled.

## What pass 0's fixes got right

- **BLOCKER 1 is fixed properly.** `Route::Href` now resolves the verse by the same mechanism as the
  verse grid. That is `SpineHtmlStream::stream` into a `VerseAnchors::Scanner`, then
  `navigateTo({spine, offsetJump})`. The verse grid's version is `BibleNavigationActivity.cpp:250-264`,
  with the result handed back at `:429` and consumed at `EpubReaderActivity.cpp:1030-1032`. The offset
  unit matches what the build loop expects (`EpubReaderActivity.cpp:1417-1419`, `:1446-1451`). No
  `navigateToHref` remnant is left in the Goal, the flow or the device checks.
- **I re-ran the EPUB check myself.** In `OEBPS/1001061250`–`1270.xhtml` of `~/Downloads/nwt_S.epub`
  there are 150 `href`s:
  - 41 are the index's links to the question files, or CSS.
  - The other 109 are all `…#chapterC_verseV`.
  - Every target file exists, and every target id exists in it.
  - No target file holds more than one chapter, so A12 is a defence and not a known case.
  - The longest href is 45 bytes, inside `HREF_BYTES = 64`.
- **The lock check d1 asked for (A15) holds.** In order:
  - `ActivityManager::loop` unlocks before `onEnter` (`ActivityManager.cpp:160-161`), and the boot
    path takes no lock at all (`:195`).
  - `ReaderActivity::onEnter` takes no lock. The file's only `RenderLock`s are at `:88` and `:195`,
    both in other methods.
  - `SpineHtmlStream::stream` takes `RenderLock` only on the inflate branch
    (`SpineHtmlStream.cpp:37-43`).
  - The promoted HTML cache is never removed after a build (`Section.cpp:258-305`, `:481`), so A10's
    "paid once" holds.
- **MINOR 1, 2 and 3 are applied.** The two stale comments are in the file table. A6 now names the
  swipe-mode page back. The `getHeldTime()` reasoning is correct: it writes the mutable flag at
  `MappedInputManager.cpp:283`, and `decodeSwipe` is a per-frame read that consumes nothing
  (`:177-186`).
- **The rest of the design checks out:**
  - The origin is 6 bytes (`PreachingOrigin.h`), and `PreachingActivity` copies it by value
    (`PreachingActivity.cpp:31-33`).
  - Nothing in the reader re-creates the Bible reader mid-session. The `goToReader` calls at
    `EpubReaderActivity.cpp:435` and `ReaderActivity.cpp:102` each open a different book.
  - Place capture is gated on `pageShown` (`EpubReaderActivity.cpp:1697`).
  - The host-test include path has a precedent: `GridMarksTest` already adds `src` and `lib/Epub` to
    reach `<Epub/VerseAnchors.h>` (`test/number_grid/CMakeLists.txt:50-54`).
  - The grammar duplication in A11 has a precedent too (`VerseTextScanner.cpp:213`).

Two findings change behaviour or safety, and both can be fixed inline. Nothing blocks.

## MAJOR 1: the X4 Pro does have a physical Back, a held Left key; after T7 it leaves for Predicación instead of going Home

**Claim.** A5: "With an origin and an empty stack, a short physical Back would go to Predicación
instead of Home; the X4 Pro has no such button." The Non-goals say: "Physical Back-button behaviour
beyond A5. The X4 Pro has none (`CLAUDE.md`, 'Input hardware')."

**Problem.** Since `706fded5` (2026-09-14, "feat: synthesise Back and Confirm from the two nav keys",
an ancestor of `HEAD`), holding the Left nav key for at least 850 ms produces a logical Back. It
reports a 40 ms held time.

- **Today**, in a reader with an empty return stack, that Back goes Home.
- **As specified**, `decide` sees `shortBackRelease = true`, depth 0, origin set → `ToPreaching`. A held
  Left on the opened verse now goes to the card, not Home.

The spec says this input does not exist. It doesn't decide what the input should do, and no device
check covers it. The reworded `ReturnStack.h:15` comment would also keep a false premise: "The
left-edge swipe is this device's only Return". A held Left pops the stack today, through branch 1.

**Evidence.**

- **The Left key becomes Back.**
  - The Left key is read as `BTN_UP` and drives `navGestures` (`lib/hal/HalGPIO.cpp:155-165`).
  - A hold of at least `HOLD_MS = 850` resolves on release to `Synth`, "emit BTN_BACK (left)"
    (`lib/Input/Input/NavKeyGestures.h:34`, `:41`).
  - `HalGPIO::wasReleased(BTN_BACK)` → `synthesisedEdge` → `case BTN_BACK: return left == NavEvent::Synth;`
    (`HalGPIO.cpp:237-240`, `:211-212`).
- **Logical Back reaches that button.** Logical Back maps to `SETTINGS.frontButtonBack`
  (`MappedInputManager.cpp:61-63`). Its default is `FRONT_HW_BACK = 0` (`CrossPointSettings.h:135`,
  `:296`), which is `HalGPIO::BTN_BACK` (`HalGPIO.h:143`).
- **It always reads as a short press.** The held time is forced to `SYNTHETIC_HELD_MS = 40`
  (`HalGPIO.cpp:253-254`, `NavKeyGestures.h:68`), so it is always `< GO_BACK_OR_HOME_MS`.
- **Today's path ends at Home.** `ReaderUtils.h:255` (not a gesture) → `:258` (released) → `:261`
  (`longPress` is false) → `:262` (`false != backShortToFileBrowser`, default 0,
  `CrossPointSettings.h:351`) → `:265` `goHome`.
- **The project docs are stale.** `NavKeyGestures.h:7-10` says this is the Back that survives a dead
  GT911. `CLAUDE.md:76`, "There is no Back button", predates it, and the spec inherited that line.

**Fix.** Keep A5's conclusion, treating the button and the gesture alike. It matches branch 1, which
already pops the stack on a held Left. It is also the only route back to the card that survives a dead
touch panel. Correct what the spec says around it:

1. **A5.** Rewrite it: "A held Left nav key (≥ 850 ms) synthesises Back with a 40 ms held time
   (`NavKeyGestures.h:41`, `:68`; `HalGPIO.cpp:211-212`, `:253-254`). With an origin and an empty
   stack it goes to Predicación instead of Home. With no origin it goes Home, as today."
2. **Non-goals.** Delete the "Physical Back-button behaviour beyond A5 … has none" line.
3. **`ReturnStack.h:15-18`.** Don't keep "only Return" in the rewording. Write "Back — the left-edge
   swipe, or a held Left key".
4. **Device checks.** Add:
   - on the opened verse, hold Left about 1 s → the same card;
   - in a reader opened from Home, hold Left → Home.
5. **`CLAUDE.md:76`.** Its staleness is outside this task. File it with `hpipe discover`.

## MAJOR 2: `VerseHref::find(scanner.take(), …)` returns a pointer into a destroyed vector

**Claim.** `VerseHref::find` returns `const VerseAnchors::VerseAnchor*` into the vector passed in
(spec line 143). Step 4 of `findReferencedVerse` is "`VerseHref::find(scanner.take(), *verse)`; its
`offset`, or nullopt" (line 201).

**Problem.** `Scanner::take()` returns the vector by value (`VerseAnchors.h:45`,
`VerseAnchors.cpp:99`). A temporary bound to a reference parameter lives only until the end of the
full-expression that contains the call. So the natural reading of step 4 leaves a dangling pointer:

```cpp
const auto* a = VerseHref::find(scanner.take(), v);
if (!a) return std::nullopt;
return a->offset;
```

`a` points into the 1,408-byte anchor block, which was freed as soon as the declaration ended. On the
device, that block is internal SRAM: it is under the 4,096-byte PSRAM threshold.

Nothing catches it:

- `findReferencedVerse` lives in the Arduino-bound `EpubReaderActivity`, so no host test runs it.
  `VerseHrefTest` calls `find` with named vectors only.
- The build adds only `-Wall` (`scripts/enable_repo_warnings.py:30`). No diagnostic sees a pointer into
  a vector's heap buffer.

The precedent avoids the trap by naming the vector first: `verseAnchors = scanner.take();`
(`BibleNavigationActivity.cpp:263`). This is undefined behaviour in the step the spec prescribes, on a
device where stability is non-negotiable. In practice the target verse's 8 bytes often survive the
free, so the device check may well pass.

**Fix.** The caller needs only the offset, so give `find` that contract:

```cpp
// The offset of `verse`'s marker among `anchors`, or nullopt.
std::optional<uint32_t> offsetOf(const std::vector<VerseAnchors::VerseAnchor>& anchors, Verse verse);
```

A by-value return cannot dangle, whatever the caller passes, and the tests (hit, miss, A12's other
chapter, A11's agreement) carry over unchanged. If the pointer is kept instead, step 4 must say
`const auto anchors = scanner.take();` before the lookup.

## MINOR 1: the device checks never exercise sleep, which the brief's acceptance names

**Claim.** "Dropping the origin" covers Home and sleep. Device check 4 tests Home only.

**Problem.** The brief's acceptance says "The Home key and sleep drop the origin". Sleep holds by
construction: `goToSleep` replaces the reader (`ActivityManager.cpp:241-244`), and wake reopens it
with no intent (`src/main.cpp:594-601`, `:586`). But the tester is never asked to check it.

**Fix.** Add a check: from the opened verse, sleep with Power, then wake. The Bible reopens at the
verse. The swipe is inert (tap mode), and a held Left goes Home (MAJOR 1).

## MINOR 2: A7's "left-edge swipe" is a quarter of the screen, which matters in swipe mode

**Claim.** A7: "a short left-edge swipe goes back to the card instead of paging back. A mid-screen
rightward swipe still pages back."

**Problem.** "Edge" is wider than the word suggests. A rightward swipe counts as Back when it starts
inside the left 25% of the width: `EDGE_SWIPE_SIDE_FRAC = 0.25f`, tested at `sx <= screenW * edgeFrac`
(`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUICore.h:235`, `:256`).

A3 keeps the origin for the reader's whole life. So in `TOUCH_READER_SWIPE` mode, for a whole Bible
session opened from Predicación, any page-back swipe started in the left quarter leaves for the card.
Swipe mode is opt-in (the default is `TOUCH_READER_ON`, `CrossPointSettings.h:363`), and branch 1
already does this while the stack has entries. Still, a tester reading "edge" will take the exits for
a bug.

**Fix.** In A7, name the band (the left 25%, `FreeInkUICore.h:235`). Add a swipe-mode device check: on
the opened verse, page back by starting a swipe at mid-screen, then confirm a swipe from the left
quarter returns to the card.

## MINOR 3: "a number past `uint16_t` → nullopt" holds only up to `UINT32_MAX` on the host

**Claim.** `parse` returns nullopt for "a number past uint16_t", and the `VerseHrefTest` list includes
that case.

**Problem.** `%u` overflow is undefined behaviour (C11 7.21.6.2p10), and on this LP64 host it wraps
modulo 2³². I compiled `sscanf(id, "chapter%u_verse%u%c", …)` on this Mac:

```
chapter65536_verse1        n=2 chapter=65536 accepted=0
chapter4294967295_verse1   n=2 chapter=4294967295 accepted=0
chapter4294967296_verse1   n=2 chapter=0 accepted=1
chapter4294967379_verse1   n=2 chapter=83 accepted=1
```

A test author who picks a "clearly huge" number gets a failing test, or a test rewritten to match host
behaviour. The device's 32-bit newlib would not behave the same way. `VerseAnchors.cpp:35-36` has the
identical limit, so A11's agreement still holds.

**Fix.** Pin the overflow test to a value in (65535, 4294967295], for example `65536`. Word the
comment as "a number past `uint16_t`" without implying any width.

VERDICT: CLEAR
