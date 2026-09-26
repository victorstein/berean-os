# Adversarial review: `2026-09-26-issue-108-design.md` (pass 0)

**Reviewed:** `docs/superpowers/specs/2026-09-26-issue-108-design.md` @ `b66a5b46`
**Against:** issue #108 (`gh issue view 108 --repo victorstein/berean-os`), `docs/superpowers/research/2026-09-26-issue-108-research.md`
**Branch state:** docs only. There is no implementation yet, so every claim below was checked against `main`'s code as it stands on this branch.
**D1:** already decided by the orchestrator and not reopened here. I checked only that the spec applies it correctly. One finding (MAJOR 1) is about how it is applied, not whether it was the right choice.

## What holds

I re-resolved these claims against the code and they are correct:

- **Premise.** `-fno-exceptions` is at `platformio.ini:72`, and `-fexceptions` is unflagged at `:74-76`. `CONFIG_COMPILER_CXX_EXCEPTIONS=y` and `EMG_POOL_SIZE=0` are in `framework-arduinoespressif32-libs/esp32s3/sdkconfig`.
- **The ditherer constructors are the root cause.** They use a throwing `new int16_t[]` at `BitmapHelpers.h:27-29,108-110,209-210`. The `makeUniqueNoThrow<…Ditherer>` calls at `JpegToBmpConverter.cpp:676-694` only make the object allocation nothrow. Nothing else in `lib/` or `src/` uses a ditherer outside JPEG, PNG and `Bitmap` (grep over `lib src`).
- **The dead null checks** at `ChapterHtmlSlimParser.cpp:775-778,783-786` are dead as the spec says.
- **The site inventory is complete.** A fresh sweep for `\bnew +[A-Za-z_:]` without `nothrow` found exactly the R§3 sites and nothing more.
- **`Bitmap`.** The `readNextRow` quantize fallback is at `Bitmap.cpp:195-200`. Only `nextRow`/`reset` touch the ditherers besides `processPixel` (`:274-277`, `:291-292`). The dead `errorCurRow`/`errorNextRow` are allocated nowhere and freed at `:17-18`.
- **`Epub`.** Every `bookMetadataCache->` use after `load` is guarded (`Epub.cpp:534-951`). `getCssParser()` has one consumer and it null-checks (`Section.cpp:392-393`). Cover and thumb failures remove the output (`Epub.cpp:607-609,641-643,701-703,736-738`).
- **`Section`.** `beginParse` failure goes to `abandonBuild` (`Section.cpp:442-446`), and so does `parseStep` Error (`:459-462`). `finalizeBuild` ignores `finishParse()` today (`:632`). `onPageComplete` dereferences `page` unchecked (`:92`).
- **Reader.** `showBuildError` is at `EpubReaderActivity.cpp:1013-1017`, reached at `:1084-1089`, `:1138-1143` and `:1203-1218`. The background build path is `:435-438`.
- **Web server.** The early return before `running = true` is at `CrossPointWebServer.cpp:125-128`. `stop()` no-ops when `!running` (`:232`), so A4's explicit teardown is needed and correct. `WebServer::~WebServer` deletes handlers (`framework-arduinoespressif32/libraries/WebServer/src/WebServer.cpp:52-62`). `dnsServer` is inside the anonymous namespace (`CrossPointWebServerActivity.cpp:24-64`) and has no other TU user.
- **Host test sources.** `BitmapHelpers.cpp` compiles on the host against `test/stubs`. I checked with `clang++ -std=c++20 -Wall -Wextra -pedantic -fsyntax-only -Itest/stubs -Ilib -Ilib/GfxRenderer lib/GfxRenderer/BitmapHelpers.cpp` and it printed `COMPILES`.
- **D1's three conditions are stated.** Both nothrow forms and all delete forms are replaced, the injector is disarmed by default, and the seam stays confined to `test/ditherers/`. `test/CMakeLists.txt` stays shared, per `.claude/agents/epub-dev.md:22-27`.

## Findings

### MAJOR 1: the D1 OOM test cannot see its injected failure under clang at the default build type

**Claim.** In D1 conditions 1 and 2, an armed injector makes the Nth nothrow allocation return `nullptr`, so the ditherer reports `valid() == false`.

**Problem.** C++14 [expr.new] lets the compiler omit calls to replaceable global allocation functions made from new-expressions. Clang does omit them when the result is only null-compared, zero-filled and deleted. That is exactly what "construct, check `valid()`, destruct" does to the rows. The replaced `operator new(size_t, nothrow_t)` is then never called, the countdown never fires, and `valid()` constant-folds to `true`. The suite builds `Release` by default (`test/CMakeLists.txt:8-10`), and this project's development host is macOS/Apple clang. The OOM test, which is the whole point of D1, would therefore fail on every local run. It would pass in CI, which uses Ubuntu GCC with `-DCMAKE_BUILD_TYPE=Release` (`.github/workflows/ci.yml:188`), because GCC 13 does not elide these calls. A test that goes red or green depending on the compiler invites someone to "fix" it the wrong way.

**Evidence.** I reproduced this in the scratchpad (`…/scratchpad/elide/`). It uses a stand-in class with three `makeUniqueNoThrow<int16_t[]>(w+4)` rows and `valid()`, plus a separate TU that replaces the nothrow and throwing forms exactly as D1 specifies, with a countdown injector:

```
Apple clang 21.0.0  -O2                        N=1 valid=1  N=2 valid=1  N=3 valid=1   <- injection invisible
Apple clang 21.0.0  -O1 / -O0                  N=1 valid=0  N=2 valid=0  N=3 valid=0
Apple clang 21.0.0  -O2 -fno-builtin           N=1 valid=0  N=2 valid=0  N=3 valid=0
Apple clang 21.0.0  -O2 -fno-assume-sane-operator-new   valid=1 (does not help)
GCC 13.5.0 (docker) -O2                        N=1 valid=0  N=2 valid=0  N=3 valid=0
```

**Fix.** Add a fourth line to D1 so it holds on both compilers. The `test/ditherers/` target compiles with `target_compile_options(DitherersTest PRIVATE -fno-builtin)`. That is verified above to restore the injection on clang, and it is harmless on GCC. Add a comment that clang elides new/delete pairs in new-expressions, so an unobserved allocation is never called. This stays within the "seam confined to `test/ditherers/`" condition and does not reopen D1.

### MAJOR 2: the trailing page in `finishParse` can reach `Section::onPageComplete` as `nullptr`

**Claim.** "Sticky guard… After a failure, no further page reaches `completePageFn` until the parse stops." Also, "`finishParse()` returns `false` when the trailing `makePages` fails."

**Problem.** `finishParse` runs *after* the parse stops. Its body is `makePages();` and then, unconditionally, `completePageFn(std::move(currentPage), …)` (`ChapterHtmlSlimParser.cpp:1630-1636`). Under this design, a failed `new Page()` at `:1704`, or at `:1667`/`:1676` inside the layout callback, leaves `currentPage == nullptr` and sets the flag, and `makePages` returns. If the new `return false` is placed anywhere after `:1636`, which is the natural spot for a "check the flag and report" line, then `completePageFn(nullptr)` reaches `Section::onPageComplete`. That function dereferences `page->serialize(file)` without a null check (`Section.cpp:92`) and crashes. That is the crash class #108 exists to remove. The guard sentence is also wrong as a statement of fact. `emitHorizontalRule` (`:364-366`) and the image path (`:768-771`) call `completePageFn` and are not in the guard list. They pass non-null pages, so they are harmless (the `.tmp` is discarded by `abandonBuild`), but the sentence promises something the design does not do.

**Evidence.** `ChapterHtmlSlimParser.cpp:1629-1639`, `:1703-1707`, `:1666-1679`, `Section.cpp:85-95`.

**Fix.** Two changes to the `finishParse()` bullet and the guard bullet:

- Rewrite the `finishParse()` bullet as: "check `allocationFailed_` immediately after the trailing `makePages()` and `return false` **before** touching `currentPage` or calling `completePageFn`."
- Reword the guard bullet to name what it guards (`addLineToPage` and `makePages`). Say that the horizontal-rule and image page breaks may still emit non-null pages into the `.tmp`, and that `abandonBuild` discards them.

### MAJOR 3: the PNG failure path, as specified, leaks three malloc'd buffers

**Claim.** Row 3 says to use `makeUniqueNoThrow`, then "`LOG_ERR("PNG", "OOM: …")` and `return false`. The manual `delete`/`delete[]` lines go." Data flow step 2 says: "Its `unique_ptr` members free whatever did allocate."

**Problem.** Step 2 is true for JPEG, where everything is in `ctx` `unique_ptr`s. It is false for PNG. By the ditherer allocation (`PngToBmpConverter.cpp:627-637`) and the scaling buffers (`:648-649`), three `malloc`/`calloc` buffers are already live: `rowBuffer` (`:618`), `ctx.currentRow` (`:516`) and `ctx.previousRow` (`:517`). NG3 keeps those as raw mallocs. Every existing early return in this function frees them by hand (`:520-522`, `:557-559`, `:565-567`, `:621-623`, `:660-666`). A literal "`LOG_ERR` and `return false`" at the new OOM points leaks up to `bytesPerRow + 2·rawRowBytes` on every failed conversion. Worse, it leaks on the OOM path, so a later conversion is more likely to fail too.

**Evidence.** `PngToBmpConverter.cpp:516-522,616-666,815-822`.

**Fix.** In row 3, require each new OOM return to `free(rowBuffer); free(ctx.currentRow); free(ctx.previousRow);`, matching `:660-666`. Alternatively, put one `ScopedCleanup` (`lib/Memory/Memory.h:42-54`) right after `:623` for those three buffers, which also lets `:660-666` and `:820-822` shrink. Correct data flow step 2 to say that only JPEG's cleanup is fully RAII.

### MINOR 4: the D1 countdown range does not fit `FloydSteinbergDitherer`, and the counting base is ambiguous

**Claim.** "N is 1, 2 or 3, which covers each row of each ditherer."

**Problem.** `FloydSteinbergDitherer` allocates two rows (`BitmapHelpers.h:209-210`). With N=3 its constructor finishes, and the guard disarms with the countdown unspent. A test that expects `valid() == false` then fails. Separately, if the test builds the ditherer through `makeUniqueNoThrow<…Ditherer>` inside the guard, then N=1 fails the *object* allocation, not a row.

**Fix.** State N ∈ {1,2,3} for the two Atkinson classes and N ∈ {1,2} for Floyd-Steinberg. State that the ditherer is constructed on the stack inside the guard, so N counts rows only.

### MINOR 5: A1/A2's "one failure, one treatment" argument ignores an existing line drop that stays

**Claim.** In A1, dropping a line "would emit a page with lines missing, and that page would be committed to the section cache." In A2, "If only the new sites followed A1, the parser would treat the same failure two ways."

**Problem.** `ParsedText.cpp:1562-1565` already drops a line on a `TextBlock` arena allocation failure and carries on, and this design keeps that. The parser still treats allocation failure two ways, and a page with a missing line can still be committed through that path. A1 cites these lines as the rejected alternative but never says they remain.

**Fix.** Add `ParsedText.cpp:1562-1565` to NG2's follow-up. It needs the same `ParsedText` callback contract change. Scope A1/A2 to "every allocation the parser itself makes."

### MINOR 6: NG3's "already fail soft" is not quite true, and row 6 inherits the gap

**Claim.** NG3 says the nothrow sites at `ChapterHtmlSlimParser.cpp:380,810,817` "already fail soft."

**Problem.** `std::shared_ptr<T>(new (std::nothrow) T(…))` still allocates its control block with the throwing `operator new`. The same happens when `Page::deserialize` pushes a `unique_ptr` into `std::vector<std::shared_ptr<PageElement>>` (`Page.h:77`, `Page.cpp:188,194,200`). These are small, fixed-size allocations, the same class as NG2's `make_shared`, and can still abort.

**Fix.** Reword NG3 to "the object allocation is nothrow; the `shared_ptr` control block is not". List these sites with NG2 for the follow-up issue.

### MINOR 7: an unlabelled consequence of the design is that a transient OOM deletes valid SD caches

**Problem.** Two paths delete good cache files on a transient OOM:

- **A1 routes a parser OOM into `abandonBuild()`.** `abandonBuild()` deletes the committed partial `.bin` too (`Section.cpp:709-713`). Its comment justifies that as "a parse error would recur against the same HTML," which does not hold for OOM.
- **Row 6 makes `Page::deserialize` return `nullptr` on OOM.** The reader then runs `abandonBuild(); clearCache();` on a *complete, correct* section (`EpubReaderActivity.cpp:1252-1259`) and rebuilds it, which needs far more heap than the page load that just failed. After `MAX_PAGE_LOAD_RETRIES = 3` (`EpubReaderActivity.h:38`) it shows `STR_PAGE_LOAD_ERROR`.

The spec says the page-load result "matches what a corrupt page cache already produces," which is accurate, but it does not say it throws away a valid cache. Both outcomes are still far better than `abort()`, and neither is a new SD write that could be mistaken for success, so the Goal holds.

**Fix.** Add a labelled assumption (A10) that states both cache deletions and accepts them. It should name the alternative, a distinct OOM result from `loadPage`, as out of scope.

### MINOR 8: small build-plumbing gaps

- **The model suite lacks two include directories.** Row 1 has `BitmapHelpers.h` include `<Memory.h>`. The `test/bit_blit/CMakeLists.txt` model has neither `${REPO_ROOT}/lib/Memory` nor `${REPO_ROOT}/test/stubs` on its include path (`test/bit_blit/CMakeLists.txt:6-8`). The new suite needs both, as `test/pagination/CMakeLists.txt:31` does for Memory. `pagination` is the one existing suite that pulls in `BitmapHelpers.h` transitively (via `GfxRenderer.h` → `Bitmap.h`), and it already has `lib/Memory`, so nothing else breaks.
- **A8 has no tag for `Bitmap.cpp`.** Row 4 requires a `LOG_ERR` there, but `Bitmap.cpp` has no logging and no `<Logging.h>` today. Name the tag (`BMP` exists once in the tree, `GFX` 29 times) and the include.

### MINOR 9: count and citation drift

- **"R§3 lists 23 bare `new` sites."** The research never gives 23. Its tables list 37 new-expressions, or 32 if each ditherer constructor counts once.
- **"10 callers of `startNewTextBlock`" (A1).** There are 9 call sites: `:337,530,750,851,993,1019,1025,1523,1553`.
- **A6: "All three callers draw the same URL as text."** The Wi-Fi QR at `CrossPointWebServerActivity.cpp:430` is paired with the SSID (`:433`), not a URL. That is still a usable fallback on an open AP, but the sentence should say so.

## Verdict rationale

Every finding can be fixed inline. None reverses D1 or any other decision, changes scope, or needs a judgment only the human can make:

- **MAJOR 1** adds one compile flag to a suite D1 already confines.
- **MAJOR 2** fixes an ordering in `finishParse` and the wording of the sticky guard.
- **MAJOR 3** restores the file's own cleanup pattern.
- **The MINORs** tighten wording, add an assumption and fill in plumbing.

VERDICT: CLEAR
