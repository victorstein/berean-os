Tier: heavy

# Review 0 — `2026-09-27-issue-106-design.md`

Reviewed against issue #106 (`gh issue view 106 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-27-issue-106-research.md`, the installed PlatformIO 6.1.19
sources, the Xtensa GCC 14.2.0 toolchain (invoked directly on small probe TUs, not `pio run`), and
the scratchpad build logs (`spike-release.log`, `wall-x4pro.log`, `wall-release.log`, `spike.w`).

## What holds up

I checked these and they are sound. They are listed so the next pass does not re-litigate them.

- **The middleware mechanism.** `CollectBuildFiles` runs every registered middleware per source node
  and passes the builder env when `co_argcount == 2`
  (`platformio/builder/tools/piobuild.py:316-334`). `AddBuildMiddleware` is at `:339-340`. Lib
  builders clone `env` in `__init__` (`piolib.py:124`) and call `process_extra_options` there
  (`:153`), before `build()` reaches `CollectBuildFiles` (`:516`). So the `CCFLAGS` snapshot the
  callback takes is final for that builder. The framework core also goes through
  `BuildSources`/`BuildLibrary` (`framework-arduinoespressif32/tools/pioarduino-build.py:196,202`),
  so it reaches the callback and is passed through.
- **`PLATFORMIO_EXTRA_SCRIPTS` appends rather than replaces.** `extra_scripts` is a `multiple`
  option, and the env value is appended after a newline (`platformio/project/config.py:299-302`).
  The red census through the env var therefore keeps the five existing `pre:` scripts.
- **The spike numbers.** `spike-release.log:952-953,959` shows RAM 65148, flash 5412302, and
  `{'wall': 203, 'skip': 325}`. `spike.w` has 62 lines: 61 repo lines plus the WebSockets
  deprecation. Every repo line from the `x4pro` global-`-Wall` probe is also in the spike set, apart
  from the two `lib/expat` lines, which are correctly excluded (`comm -23` of the two sets).
- **The A5 site list.** It matches `spike.w` exactly: 23 in `EpubReaderActivity.cpp`, 4 each in
  `CrossPointWebServer.cpp` and `Epub.cpp`, 3 in `MeetingWeekPrefetch.cpp`, one each at the other
  sites, plus `contentTop`, for 44 in all. I read every non-reader initialiser. None hides a real
  bug: each is a timing value, a counter or a label consumed only by `LOG_*`, and the side effects
  (`fillUncompressedSizes`, `tmpHtml.size()`) are kept.
- **A7.** `EpubReaderMenuActivity.cpp:109-131` consumes both `ROTATE_SCREEN` and `AUTO_PAGE_TURN`
  and returns. `:150` is the only selected-action `setResult`. The reader already uses the same
  empty-case idiom for `NIGHT_MODE`/`FRONTLIGHT` (`EpubReaderActivity.cpp:782-788`).
  `BibleDownloadActivity.cpp:271-278` and `WifiSelectionActivity.cpp:945-947,978-980` are as
  described.
- **A8, A9 and A12.** These are as described (`ChapterHtmlSlimParser.h:30,57,150`;
  `ProgressMapper.cpp:228-234,654`; `ChapterHtmlSlimParser.cpp:925`; `HalGPIO.cpp:45-110,116-117`).
- **A13.** I verified this with the toolchain rather than taking it on trust. A TU that includes
  `.pio/libdeps/x4pro/ArduinoJson/src` and is compiled with `xtensa-esp32s3-elf-g++ -std=gnu++2a -Os -Wall`
  warns `possibly dangling reference to a temporary` on the inline
  `for (JsonVariant s : fObj["styles"].as<JsonArray>())`. It does not warn on the hoisted
  `const JsonArray styles = fObj["styles"].as<JsonArray>(); for (JsonVariant s : styles)`.
  `FontDownloadActivity.cpp:122` already uses the hoisted form, and it is warning-free in the spike.
- **A2's file set.** The only `library.json` files are `BibleSearch`, `expat`, `miniz` and `uzlib`.
  The `symlink://` libdeps appear as `.pio/libdeps/x4pro/*.pio-link` and resolve into
  `freeink-sdk/…`, which the rule classifies as not owned.
- **A3's `-isystem` does not change header resolution.** `FreeInkUI/include/components/` holds only
  `bars controls keyboard lists media overlays text`. `src/components/` holds `icons themes` plus
  files, so none of the `#include "components/…"` paths in repo code can resolve differently when
  that directory drops to the system-include position.

## Findings

### MAJOR 1 — A15's primary fix does not silence the warning

**Claim** (spec lines 256-263). The design turns `if (AP_PASSWORD && …)` into
`if constexpr (AP_PASSWORD != nullptr)` around the `strlen` test. It then says that if GCC still
reports the discarded branch, the fallback is a "larger edit" to be recorded as a deviation.

**Problem.** GCC 14 does still report the discarded branch. This `-Wnonnull` comes from the C++ front
end, and outside a template the discarded branch of an `if constexpr` is still parsed and checked. The
primary fix is known to fail before the plan is written. The spec's "the plan must record it as a
deviation" would then treat a certainty as a surprise.

**Evidence.** I compiled this probe:
`constexpr const char* P = nullptr; int g(){ if constexpr (P != nullptr) { return (int)strlen(P); } return 0; }`
with `/Volumes/stein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ -std=gnu++2a -c -Wformat`.
It gives `t.cpp:4:59: warning: argument 1 null where non-null expected [-Wnonnull]` at `-O0`, `-Os`
and `-O2` alike (1 warning at each level).

**Fix.** Make a fix that works the primary, and drop the `if constexpr` text. Either of these is
behaviour-preserving for the current value, `AP_PASSWORD = nullptr`
(`CrossPointWebServerActivity.cpp:28`, whose only uses are at `:204-205`):

1. Delete the dead password branch and keep the open-network call at `:207-208`.
2. Keep the knob, but check it at compile time and pass it straight through:
   `static_assert(AP_PASSWORD == nullptr || std::char_traits<char>::length(AP_PASSWORD) >= 8, "...");`
   followed by one `WiFi.softAP(AP_SSID, AP_PASSWORD, …)`.

   I compiled this form with the same toolchain under `-Wall -Wformat`, and it produces no
   diagnostic. It also turns today's silent fall-back to an open network on a short password into a
   build error, which is the safer failure.

Update the assumptions-index row for A15 to match.

### MAJOR 2 — Computing ownership with `relpath` breaks the build on Windows when the project and the packages are on different drives

**Claim** (spec lines 154 and 288-289). The glue computes
`rel = relpath(node.srcnode().get_abspath(), PROJECT_DIR)`. The spec says "A path outside
`PROJECT_DIR` (framework, toolchain) yields a `relpath` starting `..` and is classified not-owned.
The unit tests cover this."

**Problem.**
- On Windows, `os.path.relpath` (ntpath) raises
  `ValueError: path is on mount 'C:', start on mount 'D:'` when the two paths are on different
  drives.
- The framework core sources live under the PlatformIO core dir (`~/.platformio`, normally on
  `C:`), and they do reach the middleware (`pioarduino-build.py:196,202`, via `CollectBuildFiles`).
- A clone on any other drive would therefore abort every build with a traceback. CLAUDE.md treats
  Windows (Git Bash) as a supported development host ("Development environment" section, and the
  `COM7` example in `platformio.local.ini`).
- The unit test cannot catch this, because `is_repo_source` receives an already-computed relative
  path. The failure is in the untested glue.
- The error-handling section files a middleware exception under "fails the build loudly … the
  desired failure mode". Here that is a false positive that makes the firmware unbuildable, not a
  guard.

**Evidence.** The spike computes ownership the same way
(`scratchpad/spike_wall.py`, `os.path.relpath(node.srcnode().get_abspath(), PROJECT_DIR)`). It was
only run on macOS, where every path shares one root.

**Fix.**
- Move the path arithmetic into the pure module, for example
  `is_repo_source(abs_path: str, project_dir: str) -> bool`, that:
  - normalises with `os.path.normcase`/`normpath`;
  - returns `False` when `os.path.splitdrive` differs, or when `os.path.commonpath` raises or does
    not equal `project_dir`;
  - only then applies the prefix rule to the relative remainder.
- Add unit cases that call the function with `ntpath` semantics (for example `ntpath.relpath`
  injected, or the function parameterised on the path module): cross-drive → not owned, and
  same-drive backslash paths → owned.
- Correct the spec's line 288-289 claim.

### MINOR 1 — The framework's `-Wno-sign-compare` already removes part of `-Wall` from our code, and the spec does not say so

**Claim.**
- Goal 1: "`-Wall` is on for every translation unit this repo owns".
- Problem section: the framework adds "only `-Wno-*` entries".
- Architecture: global `-Wno-*` is rejected because it "would also switch the same checks off for
  our own code".

**Problem.**
- In C++, `-Wall` enables `-Wsign-compare`.
- The framework's `cpp_flags` carries `-Wno-sign-compare`
  (`framework-arduinoespressif32-libs/esp32s3/flags/cpp_flags`), and so does the effective compile
  line quoted in R, line 38.
- GCC gives the more specific option priority whatever the order, so appending `-Wall` after it does
  not bring `-Wsign-compare` back.
- The firmware's "zero warnings" therefore excludes sign-compare, which the host suites do check
  (`test/CMakeLists.txt:42-46`). That is exactly the "global `-Wno-*` weakens `-Wall` for our code"
  outcome the design rejects, except that it arrives through the framework.
- This does not invalidate A1. It does make Goal 1, the C3 `CLAUDE.md` line and the PR's list of
  suppressions incomplete as written.

**Evidence.** With the same toolchain,
`int f(int a, unsigned b){ return a < b; }` warns `[-Wsign-compare]` under `-Wall` alone. Under
`-Wno-sign-compare -Wformat -Wall` it gives no sign-compare warning.

**Fix.** Keep the scope, and state the gap in three places:
- a Non-goals bullet: "`-Wsign-compare` (part of C++ `-Wall`) stays off: the framework's
  `-Wno-sign-compare` overrides it; re-enabling it for owned TUs is a follow-up";
- the PR's suppression list (Goal 4);
- the C3 `CLAUDE.md` line.

Do not add `-Wsign-compare` in this PR. Its count was never measured.

### MINOR 2 — Testing step 4 claims host-test coverage that does not exist

**Claim** (spec lines 327-330). "The edits to `Section.h`, `ChapterHtmlSlimParser.*` and
`ProgressMapper.cpp` must not add host warnings."

**Problem.**
- No host suite compiles any of those files, so the step verifies nothing about them. CLAUDE.md
  names "this is already tested" as one of the two most expensive wrong claims.
- The spec also says the A14 fallback pragma is "one specific warning … at one site" (Goal 3). But
  bracketing `Section::buildReachedVisibleTextOffset` in `Section.h` suppresses the warning in every
  TU that inlines that header function.

**Evidence.**
- `test/bookmark_match/CMakeLists.txt:1-2`: "ReaderBookmarks itself needs Epub and Section, which no
  host suite builds."
- `grep -rhn "lib/Epub\|lib/ProgressMapper\|lib/hal" test/*/CMakeLists.txt` lists only
  `BibleNavScanner.cpp`, `BibleChapterNumber.cpp`, `VerseAnchors.cpp`, `htmlEntities.cpp` and
  include dirs.

**Fix.**
- Reword step 4 to say the host run is a regression check on the suites that exist, and that the
  edited files have no host coverage. Their verification is the firmware census plus the device
  list in step 6.
- Under A14's fallback, say that a header pragma covers every inliner, and require the PR to name it
  as such.

## Assumption audit summary

| ID | Verdict |
|---|---|
| A1 | Sound; d1 conditions C1-C4 are consistent with the rest of the design |
| A2 | Sound as a rule. The glue that feeds it is broken cross-drive (MAJOR 2) |
| A3 | Sound; resolution unchanged, and no name collisions |
| A4 | Sound. Effective `-Wall` lacks `-Wsign-compare` (MINOR 1) |
| A5, A6, A7, A8, A9, A12 | Sound, verified at the cited lines |
| A10 | Sound; the model test and the CI step exist (`ci.yml:163-164`) |
| A11 | Sound; a scope choice |
| A13 | Sound, verified with the toolchain |
| A14 | Plausible; the fallback exists. Pragma scope wording (MINOR 2) |
| A15 | Primary fix fails (MAJOR 1) |
| A16 | Sound under the brief |

Neither MAJOR reverses a decision, changes scope or needs a human call. Both are fixed inline in
the spec before planning.

VERDICT: CLEAR
