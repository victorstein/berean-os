# Issue #108 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-26-issue-108-plan.md`.
Spec: `docs/superpowers/specs/2026-09-26-issue-108-design.md`.

## How this was checked

I did more than read the plan. I tested it:

- **Find texts.** A script pulled every "Find" block out of the plan and searched the named
  file for it. All 60 blocks match verbatim, with the expected count. The block at
  `plan:421-442` appears exactly twice in `BitmapHelpers.h`, as the plan says. The others
  appear once.
- **Plan applied mechanically.** I copied the worktree to a scratch directory (outside the
  worktree; nothing in the worktree changed). There I applied steps 1–12 exactly as written,
  plus the local `add_subdirectory(ditherers)`.
- **Host suite.**
  - Step 1: `DitherersTest` passes on the unchanged header, `[  PASSED  ] 3 tests.`, so the
    golden values are correct.
  - Step 2: 7/7 pass. The full ctest run passes, `100% tests passed out of 858`.
  - The OOM tests really inject: the `rowCount + 1` case confirms that exactly 3, 3 and 2
    nothrow allocations happen.
- **Grep checks.** Step 13.1's inventory grep prints only the expected comment line.
- **Firmware.** I ran `pio run -e x4pro` on the applied tree. **It FAILED** (BLOCKER 1). I
  then made one change to `BitmapHelpers.h` and nothing else, and the same build returned
  `SUCCESS`. So every other firmware edit in steps 3–12 compiles as written.

## Spec coverage

| Spec item | Plan step | Status |
|---|---|---|
| Row 1 ditherers + `valid()` | 2 | covered, **does not build in firmware** (BLOCKER 1) |
| Row 2 JPEG `valid()` checks | 3 | covered |
| Row 3 PNG unique_ptr + `ScopedCleanup` | 4 | covered. No manual frees are left after the cleanup, and there are no double frees (checked on the applied copy) |
| Row 4 / A3 / A7 `Bitmap` | 5 | covered |
| Row 5 `Epub::load` | 6 | covered, `:362,364,383,495` |
| Row 6 `Page` / `PageImage` | 7 | covered |
| Rows 7–8, A1, A2 parser + `finalizeBuild` | 8 | covered. All 9 sites, sticky guard, `parseStep`/`beginParse`/`finishParse` (before `completePageFn`) |
| Rows 9–10 decoders + `HalFile` | 9 | covered. Close callbacks `pngCloseWithHandle`/`jpegClose` exist (`PngToFramebufferConverter.cpp:62`, `JpegToFramebufferConverter.cpp:65`) |
| Row 11 reader `Section` | 10 | covered. `showBuildError` is defined at `EpubReaderActivity.cpp:1013` in the same function |
| Rows 12–13, A4, A5 web server / DNS | 11 | covered. See MINOR 1 and MINOR 2 |
| Row 14 / A6 QR | 12 | covered |
| Testing 1–3, D1 conditions 1–4 | 1, 2 | covered, verified by running them |
| PR notes (CMake line, device checks, NG2 follow-ups) | 14.3 | covered |

The names stay consistent from step to step: `valid()`, `markAllocationFailed`,
`allocationFailed_`, `ScopedNthNothrowFailure` and `fired()`.

---

## BLOCKER 1. Step 2's `#include <Memory.h>` in `BitmapHelpers.h` breaks the firmware build

**Claim.** Step 2.3 (`plan:350-357`) adds `#include <Memory.h>` to
`lib/GfxRenderer/BitmapHelpers.h` and uses `makeUniqueNoThrow<int16_t[]>` in all three
constructors. Step 2.4 checks only the host suite. Steps 3–12 commit without a firmware
build (`plan:32-35`).

**Problem.** Some libraries include `BitmapHelpers.h` without having `lib/Memory` on their
include path:

- The header reaches them through `GfxRenderer.h:23` → `Bitmap.h:7` → `BitmapHelpers.h`.
- They include `GfxRenderer.h` from their own sources: `lib/EpdFont/SdCardFontManager.cpp:4`
  and `lib/ProgressMapper/ProgressMapper.cpp:3`.
- Neither library includes `<Memory.h>` itself, so PlatformIO's LDF never adds `lib/Memory`
  to their include path.
- This host's filesystem is case-insensitive. `<Memory.h>` therefore resolves silently to the
  toolchain's C header `toolchain-xtensa-esp-elf/xtensa-esp-elf/include/memory.h`, and
  `makeUniqueNoThrow` is undeclared.

The host suite cannot see this, because every host suite puts `lib/Memory` on its include
path. The first firmware build is in step 13. By then steps 2–12 are committed on top of a
tree that does not build. Step 13.3's advice ("fix it, commit as `fix: …`") leaves the
implementer to choose a design with no guidance.

**Evidence.** I ran `~/.platformio/penv/bin/pio run -e x4pro` on a scratch copy with steps
1–12 applied verbatim:

```
In file included from lib/GfxRenderer/Bitmap.h:8,
                 from lib/GfxRenderer/GfxRenderer.h:23,
                 from lib/EpdFont/SdCardFontManager.cpp:4:
lib/GfxRenderer/BitmapHelpers.h:32:19: error: 'makeUniqueNoThrow' was not declared in this scope
x4pro          FAILED
```

Then I made one change. `BitmapHelpers.h` included `<new>` in place of `<Memory.h>` and
spelled each row as `std::unique_ptr<int16_t[]>(new (std::nothrow) int16_t[width + 4]())`
(`width + 2` for Floyd-Steinberg). The same build returned `x4pro SUCCESS`. No other file
needed a change.

I did not verify how Linux CI behaves. A case-sensitive filesystem would give
`Memory.h: No such file` or would resolve through LDF. Either way, the plan's own
verification host is this Mac, and step 13.3 fails on it.

**Fix.** Change step 2.3's `BitmapHelpers.h` edit as follows.

1. Replace the include block with:

   ```cpp
   #include <cstdint>
   #include <cstring>
   #include <memory>
   #include <new>
   #include <utility>
   ```

2. Spell each row initialiser as
   `errorRow0(std::unique_ptr<int16_t[]>(new (std::nothrow) int16_t[width + 4]()))`
   (and `width + 2` for `FloydSteinbergDitherer`). Add one "why" comment above the first
   class:

   ```cpp
   // Rows use nothrow new[] directly rather than makeUniqueNoThrow: this header reaches
   // EpdFont and ProgressMapper through GfxRenderer.h, and lib/Memory is not on their
   // include path, so <Memory.h> would resolve to the C library's <memory.h>.
   ```

This keeps the spec's intent: nullable `unique_ptr<int16_t[]>` rows, the same value
initialisation `()`, and the same nothrow array operator. D1's seam intercepts the same
`operator new[](size_t, nothrow_t)`, so the host tests stay valid unchanged. It also stays
inside the declared files.

Also add one line to the plan: after step 2 (the only header shared across libraries), run
`~/.platformio/penv/bin/pio run -e x4pro` once. That catches this class of break at the
step that causes it. The alternative, adding `#include <Memory.h>` to
`SdCardFontManager.cpp` and `ProgressMapper.cpp`, would touch two files that are not on
the `FILES:` lines.

---

## MINOR 1. Step 11.1 leaves a dead `if (!server)` block behind

**Claim.** Step 11.1 (`plan:1686-1698`) adds a null check with `LOG_ERR` and `return`
straight after the `WebServer` allocation.

**Problem.** `CrossPointWebServer.cpp:126-129` already has
`if (!server) { LOG_ERR("WEB", "Failed to create WebServer!"); return; }`. It was dead
while `new` threw. After the plan it is still dead, because the new check runs first. That
leaves two checks for one condition.

**Evidence.** `src/network/CrossPointWebServer.cpp:112` (the allocation) and `:126` (the
existing check).

**Fix.** Replace only the allocation line with
`server = makeUniqueNoThrow<WebServer>(port);`. The existing check at `:126` then becomes
the live OOM path. Optionally, change its message to `"OOM: WebServer"` to match A8.

## MINOR 2. The WebDAV failure path does not follow A4 to the letter, and the plan does not say why

**Claim.** Spec A4 (`spec:170-173`) says that on a `WebSocketsServer` **or `WebDAVHandler`**
failure, `begin()` calls `server->stop()` and then `server.reset()`. Step 11.1
(`plan:1710-1716`) calls only `server.reset()` on the WebDAV path.

**Problem.** The plan is correct: `server->begin()` has not run at that point
(`CrossPointWebServer.cpp:184` versus `:187`), so there is nothing to stop. It also relies
on `~WebServer` deleting registered handlers, and it does
(`framework-arduinoespressif32/libraries/WebServer/src/WebServer.cpp:59-63`). But it departs
from the spec without saying so, and a reviewer comparing against A4 will flag it.

**Fix.** Add one sentence under 11.1: "The WebDAV path skips `stop()` because
`server->begin()` has not run yet."

## MINOR 3. Step 4.5's expected output is slightly wrong

**Claim.** `plan:803-806` says the grep prints the `free` pairs and the lambda, and that
there is "no bare `new`."

**Problem.** The pattern `new ` also matches the comment at
`PngToBmpConverter.cpp:796` (`// Only reset when we'll move to a new source row`). A
literal implementer will see a line the plan did not predict.

**Fix.** Add "and the comment `…move to a new source row`" to the expected output.

## MINOR 4. Step 0.3 edits `test/CMakeLists.txt`, which no `FILES:` line declares

**Claim.** `plan:16-19` and `plan:53-60` add `add_subdirectory(ditherers)` locally, never
stage it, and revert it in 14.1.

**Problem.** The file is touched on disk but never committed. This matches the accepted
pattern from earlier batches (see issue-58 plan review 0, MAJOR 2) and
`.claude/agents/epub-dev.md:22-23`. The plan guards staging carefully: every `git add`
names explicit paths, and there is no `-u test`. I rank this MINOR, not BLOCKER, because
the file never enters the branch diff. If the pipeline's lock checks working-tree
modifications rather than commits, this is the one path it would flag.

**Fix.** None required. Optionally, add a note beside the `FILES:` lines saying the edit is
local only, so the lock owner sees it was deliberate.

---

## Everything else holds

- Every step that has a host-testable surface starts from a test. Step 1 is characterisation,
  green by design. Step 2 goes red at compile, on the missing `valid()`, and then green.
  Steps 3–12 are firmware-only, as the spec says (`spec:222-224`), and the plan states this
  openly.
- Steps are ordered by their dependencies: `valid()` lands before steps 3, 4 and 5 use it.
  Step 8 commits the header, the source and `Section.cpp` together.
- With BLOCKER 1 fixed, every intermediate commit compiles. That is shown by the
  full-tree `SUCCESS` with only that change.
- No placeholders, no vague steps. Every edit is an exact find and replace. An implementer
  with no other context could execute it literally, apart from BLOCKER 1.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
