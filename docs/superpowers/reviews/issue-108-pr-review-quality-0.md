# PR #122 code-quality review, pass 0 (issue #108)

Scope: the code in `lib/`, `src/` and `test/ditherers/` in `gh pr diff 122`. The `docs/superpowers/`
files follow the repo's existing per-issue convention (compare `issue-111-*` and `issue-113-*` on
`main`) and are not reviewed here for quality.

## What was checked, and holds

- **One allocation idiom.** Every converted site uses `makeUniqueNoThrow` (`lib/Memory/Memory.h:23-34`).
  Raw `new (std::nothrow)` appears only where ownership passes to a C library or a framework,
  and each of those sites has a one-line "why" comment, as CLAUDE.md requires:
  `JpegToFramebufferConverter.cpp:56`, `PngToFramebufferConverter.cpp:53`, and
  `CrossPointWebServer.cpp:186` (`~WebServer` deletes its handlers).
- **The ditherer rows use an existing pattern.** The rows are nullable `unique_ptr<T[]>` built from
  `new (std::nothrow) T[n]` (`BitmapHelpers.h:34-37`, `110-113`, `206-210`). This matches the
  EpdFont code, which has the same `<Memory.h>` include-path limit (`SdCardFont.cpp:444`,
  `812`, `1370`, `1410`). The header comment at `BitmapHelpers.h:9-11` says why.
  `valid()` is the name the repo already uses for a nullable-state probe
  (`UnitText.h:42`, `UnitAnchors.h:49`, `CatalogIndex.h:47`).
- **No new helper was added.** The PNG converter's single cleanup (`PngToBmpConverter.cpp:627-631`) is the
  existing `ScopedCleanup` (`Memory.h:42-54`), and this PR is its first caller. The by-reference
  capture has a comment explaining why: the scanline loop swaps the two row pointers.
- **Error shape.** Every site does `LOG_ERR(<file's existing tag>, "OOM: <what>")` and then returns
  through a failure path its caller already handles: `Epub::load` returns `false`, the
  `deserialize` functions return `nullptr`, the parser reports `ParseStatus::Error`, the
  existing `showBuildError` lambda runs (`EpubReaderActivity.cpp:1055-1059`), the web server
  returns with `running == false`, and the QR code is not drawn. Tags match their siblings
  (`"GFX"` is GfxRenderer's tag, `GfxRenderer.cpp:66`; `"QR"` is `QrUtils.cpp:69`). The
  parser's one sticky flag behind one helper (`markAllocationFailed`, `ChapterHtmlSlimParser.cpp:287-290`)
  replaces nine near-identical `LOG_ERR` lines rather than adding to them.
- **Dead code was removed.** The unused `errorCurRow`/`errorNextRow` members and the hand-written
  `~Bitmap` are gone (`Bitmap.h:96-99`). The hand-written destructors in the three ditherers are
  gone, and the manual `delete` ladders in `PngToBmpConverter.cpp` are replaced. No code is
  commented out. The comment that intent review 0 found orphaned now sits above
  `startNewTextBlock` again (`ChapterHtmlSlimParser.cpp:292`).
- **The tests are well designed.** The golden test pins the output of all three ditherers
  against the pre-refactor code, both before and after `reset()`. That guards the
  `std::swap` rotation in `nextRow()`, which is the one behavioural change in the refactor
  (`DitherersTest.cpp:39-56`). The OOM test fails each row in turn, and it also asserts that
  the ditherer makes no more nothrow allocations than it has rows (`DitherersTest.cpp:61-86`),
  so an injection that silently misses cannot pass. The seam is disarmed by default through
  an RAII guard, and `-fno-builtin` has a comment explaining why it is needed
  (`test/ditherers/CMakeLists.txt:17-20`). Naming follows the test convention already in the
  repo: `k`-prefixed constants, as in `BitBlitTest.cpp` and `PaginationInvarianceTest.cpp`.
  The `lib`-rooted `"GfxRenderer/BitmapHelpers.h"` include matches `BitBlitTest.cpp:6`.

## Findings

### MINOR 1: the parser's new state member sits among the method declarations

`lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h:130-133`. `bool allocationFailed_ = false;` and its
comment sit between `startNewTextBlock` and `markAllocationFailed`, in the middle of the
private helper declarations. Every other piece of parse state in this class sits in the
data block above. The closest match is the "Resumable parse state" group, `xmlParser_` /
`parseFile_` / `parseStartTime_` (`:118-126`), which also uses the trailing-underscore
spelling. Fix: move the member and its comment into that group, and leave
`markAllocationFailed` with the other helpers.

### MINOR 2: an include path the test suite does not use

`test/ditherers/CMakeLists.txt:14` adds `${REPO_ROOT}/lib/Memory`. Nothing this suite compiles
includes `<Memory.h>`. `BitmapHelpers.h` deliberately avoids it (`:9-11`), `BitmapHelpers.cpp` →
`Bitmap.h` does not include it, and neither does `DitherersTest.cpp` or `FailingNothrowNew.*`.
The line looks copied from `bible_search_index` or `pagination`, suites that do need it. Fix:
drop the line. Alternatively, keep it with a reason, if the suite is meant to grow to cover a
`makeUniqueNoThrow` caller such as `Bitmap::parseHeaders`.

### MINOR 3: `grayRow` is still freed by hand beside the new scoped cleanup

`lib/PngToBmpConverter/PngToBmpConverter.cpp:678-682` and `:828`. This PR moves the function to
one exit-safe cleanup (`ScopedCleanup` at `:627`, `unique_ptr` for the ditherers and the
scaling buffers). But `grayRow` is still `malloc`/`free`, with a manual `free` at the one
normal exit. It is correct today, because nothing between those two lines returns. It is
also the one buffer in the function that a future early `return` inside the scanline loop
would leak. Fix: make it `makeUniqueNoThrow<uint8_t[]>(width)`, as `rowAccum`/`rowCount` are
two lines above. The zero-fill costs one pass over `width` bytes, once per image. The other
fix is to add it to `freeRowBuffers`.

No BLOCKER or MAJOR findings. All three MINORs are local, mechanical fixes that change no
decision or scope.

VERDICT: CLEAR
