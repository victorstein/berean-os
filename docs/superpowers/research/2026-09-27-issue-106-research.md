# Issue #106 part 2 — research: `-Wall` on the firmware build

Scope: part 1 (format attribute on `logPrintf` plus `-Wformat`) landed in PR #142.
This note covers part 2 only: add `-Wall` to the firmware build, with repo-code warnings
at zero before the flag lands. Third-party and `freeink-sdk` warnings are out of scope,
and must be excluded or suppressed explicitly.

All builds below were run on `fix/106-enable-wall` at `7c080671`. Each was run through the
batch `pio-locked.sh` wrapper, with `PLATFORMIO_BUILD_FLAGS="-Wall"` as the probe. That
variable appends to `build_flags` (`platformio/project/options.py:395-397`, sysenvvar
`PLATFORMIO_BUILD_FLAGS` → `BUILD_FLAGS`). The logs are in the session scratchpad. The
counts below come from `grep -E 'warning: .*\[-W' | sort -u`.

## Installed tool versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `pio --version` |
| Xtensa GCC | 14.2.0 (`esp-14.2.0_20251107`) | `xtensa-esp32s3-elf-g++ --version`; `toolchain-xtensa-esp-elf/package.json` `"version": "14.2.0+20251107"` |
| arduino-esp32 | 3.3.7 | `framework-arduinoespressif32/package.json` |
| IDF libs | 5.5.0+sha.87912cd291 | `framework-arduinoespressif32-libs/package.json` |
| Platform | pioarduino 55.03.37 | `platformio.ini:15` |

GCC 14 is the relevant fact. `-Wdangling-reference` (GCC 13+) and the `std::optional`
`-Wmaybe-uninitialized` false positive both appear in the census below.

## Who owns the behaviour

- **`platformio.ini:31-75`** (`[base] build_flags`). This is the only warning control today.
  - `-Wno-bidi-chars` (`:70`).
  - `-Wformat` (`:71-73`), from PR #142, with a comment giving the reason.
  - No `-Wall`, `-Wextra` or `-Werror`.
  - Both envs, `x4pro` (`:163-179`) and `x4pro-gh_release` (`:181-192`), inherit it through
    `${base.build_flags}`.
- **Framework defaults.** `framework-arduinoespressif32-libs/esp32s3/flags/cpp_flags` carries
  only `-Wno-*` / `-Wno-error=*` entries plus `-Wwrite-strings`, and no `-Wall`. The census
  compile line for a C library (QRCode) confirms the effective set is
  `-Wno-enum-conversion -Wno-error=deprecated-declarations -Wno-error=extra -Wno-error=unused-but-set-variable -Wno-error=unused-function -Wno-error=unused-variable -Wno-old-style-declaration -Wno-sign-compare -Wno-unused-parameter -Wwrite-strings -Wno-bidi-chars -Wformat -Wall`,
  where the trailing `-Wall` is the probe.
- **`lib/Logging/Logging.h:42`**: `logPrintf(...) __attribute__((format(printf, 3, 4)))`. This
  is part 1 and is already in place.
- **`lib/Logging/Logging.h:44-66`**. `LOG_ERR/INF/DBG` expand to `logPrintf` only under
  `ENABLE_SERIAL_LOG` and the matching `LOG_LEVEL`, and to nothing otherwise.
  - `x4pro` defines `-DENABLE_SERIAL_LOG -DLOG_LEVEL=2` (`platformio.ini:172-173`).
  - `x4pro-gh_release` defines only `-DLOG_LEVEL=0` (`:190`).
  - **Consequence:** under `-Wall`, every local that exists only to feed a log line
    becomes `-Wunused-variable` or `-Wunused-but-set-variable` in the release env only. The
    `x4pro` census cannot find those warnings. The release env must be censused separately,
    and it was.
- **CI** (`.github/workflows/ci.yml:78-103`) builds `x4pro` only, with `pio run -e x4pro | tee pio.log`.
  - Nothing counts or fails on warnings.
  - The release env is built only by `release-publish.yml:57`.
  - So "warnings at zero" is enforced today by nothing but the person reading the log.
- **Host tests** already build with `-Wall -Wextra -pedantic` (`test/CMakeLists.txt:42-46`).

## How PlatformIO scopes flags (control flow of a flag)

- **`build_flags` / `BUILD_FLAGS`** apply to every build unit in the env: `src/`, every
  private library under `lib/`, every `lib_deps` package (including the `freeink-sdk`
  `symlink://` libraries, `platformio.ini:141-153`), and the Arduino core.
  - The census proves the reach: warnings come from `.pio/libdeps/*/QRCode`, `WebSockets`,
    `Arduino-wolfSSL` and `freeink-sdk/libs/...`.
- **`build_src_flags` / `SRC_BUILD_FLAGS`** apply to `src/` only
  (`platformio/project/options.py:399-410`, "configures flags the only for project source
  files in the `src` folder").
  - `ProjectAsLibBuilder.build_flags` returns `SRC_BUILD_FLAGS` when it is set
    (`platformio/builder/tools/piolib.py:964-967`).
  - Private libraries under `lib/` do **not** receive it.
- **Per-library flags** come from a `library.json` `build.flags` entry
  (`PlatformIOLibBuilder.build_flags`, `piolib.py:623-625`), applied through
  `process_extra_options` → `env.ProcessFlags(self.build_flags)` (`piolib.py:296-298`).
  - Four libs under `lib/` have a `library.json` today: `BibleSearch`, `expat`, `miniz` and
    `uzlib`.
  - None of them sets `build.flags`.
  - The other `lib/` libraries have no manifest.
- **Repo `lib/` holds both our code and vendored third-party code.** Vendored: `lib/expat`
  (libexpat 2.7.3), `lib/miniz` (11.3.2), `lib/uzlib` (2.9.8). All three `library.json`
  files declare upstream versions, and uzlib names `pfalcon/uzlib`. Everything else under
  `lib/` is repo code (`Epub`, `GfxRenderer`, `hal`, `ProgressMapper`, ...).

The spec therefore has to choose how `-Wall` reaches `lib/<ours>` without reaching
`lib/expat`, `.pio/libdeps` or `freeink-sdk`. `build_src_flags` alone would leave every
`lib/` warning below unchecked.

## Census

### Baseline: `x4pro`, no probe (after `-t clean`)

Two warnings, both of which survive today without `-Wall`:

```
.pio/libdeps/x4pro/WebSockets/src/WebSocketsClient.cpp:573:31: 'virtual void NetworkClient::flush()' is deprecated [-Wdeprecated-declarations]
src/activities/network/CrossPointWebServerActivity.cpp:204:29: argument 1 null where non-null expected [-Wnonnull]
```

`RAM: 65172 B`, `Flash: 5490706 B` (83.8% of 6,553,600).

`-Wnonnull` is enabled by default under `-Wformat` (GCC docs), so it is already live. It is
the known leftover from the brief.
- The code is `constexpr const char* AP_PASSWORD = nullptr;`
  (`CrossPointWebServerActivity.cpp:28`) followed by
  `if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8)` (`:204`).
- The branch is dead, but GCC still diagnoses the `strlen(nullptr)` in it.

### Probe on `x4pro` (first build after the framework install)

255 unique warnings. Most of them came from the one-time Arduino-core rebuild that
`custom_sdkconfig` triggers (`platformio.ini:86-116`):
- `managed_components/espressif__esp-sr`: 204.
- `managed_components/espressif__rmaker_common`: 12.
- `framework-espidf/components/usb/{ext_port,hub}.c`: 6.

None of them reappear in the later `x4pro-gh_release` probe or in the baseline (0 matches
for `managed_components` / `framework-espidf`). They belong to the core rebuild, not to the
flag under study.

### Probe on `x4pro-gh_release`

77 unique warnings. The release env is the superset: it contains every `x4pro` repo
warning plus the log-only unused locals.

`RAM: 65148 B`, `Flash: 5412302 B`. Both builds succeeded (`pio run` exit 0).

### Repo-code warnings (`src/` plus our `lib/`), union of both probes

About 57 diagnostics (61 unique lines; each `-Wreorder` diagnostic spans 3 lines):

| Warning | Count | Sites | Nature |
|---|---|---|---|
| `-Wunused-variable` | 42 | Release only except `FontDownloadActivity.cpp:638` (`contentTop`, both envs). The release-only ones are in `EpubReaderActivity.cpp` (23 timing locals, `:385`, `:1255-1579`), `CrossPointWebServer.cpp:744,761,762,1625`, `MeetingWeekPrefetch.cpp:70-72`, `main.cpp:758`, `Epub.cpp:421,430,448,486`, `BookMetadataCache.cpp:292`, `Jpeg/PngToFramebufferConverter.cpp:502/472`, `FontDecompressor.cpp:525`, `SdCardFont.cpp:726`, `GfxRenderer.cpp:1725` | Log-only locals, empty when `LOG_*` compiles out. `contentTop` is genuinely dead. |
| `-Wunused-but-set-variable` | 2 | `CrossPointWebServerActivity.cpp:133` (`modeName`), `lib/Epub/Epub/Section.cpp:279` (`fileSize`) | Release only; log-only |
| `-Wswitch` | 4 | `BibleDownloadActivity.cpp:252` (`NoEpubEdition`), `WifiSelectionActivity.cpp:967` (`PASSWORD_ENTRY`), `EpubReaderActivity.cpp:714` (`AUTO_PAGE_TURN`, `ROTATE_SCREEN`) | See below |
| `-Wreorder` | 2 | `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h:57` vs `:30` (init at `:150`); `lib/ProgressMapper/ProgressMapper.cpp:234` vs `:228` (init at `:654`) | Mem-init order differs from declaration order |
| `-Wmaybe-uninitialized` | 2 | `src/activities/reader/ReaderBookmarks.cpp:130` (`*offset`, declared `:125`); `lib/Epub/Epub/Section.h:176`, inlined into `EpubReaderActivity::renderBook()` at `EpubReaderActivity.cpp:1112`, `offsetJump` declared `:1043` | Both read a `const std::optional<uint32_t>` built by a nested conditional over `optional`/`nullopt`, and both are guarded by `has_value()`. This is the known GCC false positive on `std::optional`'s payload. |
| `-Wdangling-reference` | 2 | `src/activities/settings/FontDownloadActivity.cpp:130,135`: `for (JsonVariant s : fObj["styles"].as<JsonArray>())` | GCC 13+ heuristic on the ArduinoJson proxy. `JsonArray` is a handle into the document, so this is a candidate false positive. The spec must confirm it against ArduinoJson 7.4.2. |
| `-Wparentheses` | 1 | `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:925` | `a && b \|\| c && d`. Precedence already gives the intended grouping, so adding parentheses changes nothing. |
| `-Wunused-function` | 1 | `lib/hal/HalGPIO.cpp:77`, `detectDeviceTypeWithFingerprint()` | Defined unconditionally; its only call is `:117` under `#if FREEINK_MCU_C3` (`:116`). |
| `-Wnonnull` | 1 | `CrossPointWebServerActivity.cpp:204` | See baseline |

Notes on the `-Wswitch` sites:
- **`BibleDownloadActivity.cpp:252-278`.** Every handled case `return`s, and the statement
  after the switch is `fail(tr(STR_BIBLE_DOWNLOAD_FAILED_HINT));` (`:278`). So
  `NoEpubEdition` already falls through to the generic failure. `NoEpubEdition` is declared
  at `src/network/PublicationDownloader.h:23` and produced at `PublicationDownloader.cpp:168`.
- **`WifiSelectionActivity.cpp:967`** is the render switch.
  - `PASSWORD_ENTRY` is set at `:362`.
  - `:945` already treats `PASSWORD_ENTRY` like `HIDDEN_SSID_ENTRY`, and the switch has a
    `HIDDEN_SSID_ENTRY` case that draws nothing (`:978-980`).
- **`EpubReaderActivity.cpp:714`** is the reader-menu action switch.
  - `AUTO_PAGE_TURN` and `ROTATE_SCREEN` are declared at `EpubReaderMenuActivity.h:22-23`.
  - `ROTATE_SCREEN` is only offered under `BEREAN_CAP_ROTATION` (`EpubReaderMenuActivity.h:53`).
  - `AUTO_PAGE_TURN` handling moved out of this activity in #171 (`7c080671`, HEAD). The spec
    must trace where each action is now handled before it chooses between an explicit empty
    case and a `default`.

### Third-party and `freeink-sdk` warnings, union of both probes

These are out of scope to fix, and must be excluded or suppressed explicitly.

| Source | Warning | Where it surfaces |
|---|---|---|
| `freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIIcon.h:8` | `-Wcomment` (a `//` line ending in `\`) | **A header included by `src/`** (e.g. via `src/components/UiAppHelpers.h:4`). It reports in many `src/` TUs, so scoping `-Wall` to our sources does not remove it. |
| `freeink-sdk/.../Ssd1677Driver.cpp:117` | `-Wunused-function` | SDK TU |
| `freeink-sdk/.../PaperMonoDriver.cpp:631` | `-Wunused-but-set-variable` (release only) | SDK TU |
| `lib/expat/xmlparse.c:144,146` | `-Wcomment` | Vendored TU |
| `.pio/libdeps/*/QRCode/src/qrcode.c` (9 lines) | `-Wunknown-pragmas` (`#pragma mark`) | libdep TU |
| `.pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h:355` | `-Wcomment` | libdep header. Whether any `src/` or `lib/` TU includes it is unverified; the census only saw it attributed to the header. |
| `.pio/libdeps/*/WebSockets/src/WebSocketsClient.cpp:573` | `-Wdeprecated-declarations` | Already present without `-Wall` (baseline) |

`freeink-sdk` is a submodule, and `.claude/agents/hal-dev.md` ("freeink-sdk is a submodule")
forbids editing it. The `FreeInkUIIcon.h` warning therefore has to be handled from our side.
Options the spec will weigh:
- Include the SDK headers as system headers (`-isystem`).
- A targeted `-Wno-comment`.
- A `#pragma GCC diagnostic` around the include.

## Existing idioms to mirror

- **Log-only locals.** `[[maybe_unused]]` is already the repo's idiom for exactly this case:
  - `src/study/BibleSearchIndexer.cpp:152,228,256` and
    `src/activities/reader/BibleSearchActivity.cpp:512` use
    `[[maybe_unused]] const unsigned long started = millis();`.
  - `BibleSearchIndexer.cpp:321` and `ActivityManager.cpp:332` mark parameters the same way.
- **Diagnostic pragmas.** There is **no** `#pragma GCC diagnostic` anywhere in `src/` or
  `lib/` (grep over `*.cpp`/`*.h`/`*.c`, excluding `builtinFonts`, returns none). A scoped
  push/ignore/pop would be the first in the repo.
- **Build-flag change with a reason.** The nearest example of this kind of change is PR #142's
  `-Wformat` line: a two-line `#` comment giving the reason, then the flag
  (`platformio.ini:71-73`). The `-Wno-bidi-chars` line (`:70`) and the wolfSSL "Do not repeat
  those defines here: GCC reports every library translation unit as a macro redefinition"
  comment (`:64-66`) show the same practice.
- **Build-time scripting.** `pre:` extra scripts already exist (`platformio.ini:131-137`), e.g.
  `scripts/patch_jpegdec.py`, which operates on `.pio/libdeps`. A per-library flag
  middleware would be new, but it would live in an established place.

## Sizing and tier

- About 57 repo diagnostics across about 20 files in `src/` and `lib/`.
- All fixes are local. No on-disk format, store, public API or cross-surface contract
  changes.
- The files span `ui`, `net`, `epub` and `hal`, which the brief already anticipates ("you
  touch files everywhere, so you run last and alone").
- Tier stays `heavy`. There is nothing to raise.

## Open questions for the spec

1. **Flag scope.** Options:
   - `-Wall` in `build_flags` plus explicit `-Wno-*` / per-library opt-outs for third-party code.
   - `build_src_flags` plus a mechanism for `lib/<ours>`.
   - A `pre:` script that adds `-Wall` only to the repo's own library builders.

   Whichever is chosen must also cover the `FreeInkUIIcon.h` header that is included into our
   own TUs.
2. **Log-only locals.** Mark them `[[maybe_unused]]` (the repo idiom), or fold the
   expression into the log call where the local exists only for it.
3. **`-Wswitch`.** Use explicit empty cases, not a `default:`, so that future enum additions
   stay diagnosed. Each case needs its current handler traced first.
4. **`-Wmaybe-uninitialized` and `-Wdangling-reference`.** Decide between restructuring and a
   scoped pragma. Restructuring must not change behaviour, and a pragma would be the first
   in the repo.
5. **Verification.** Nothing in CI fails on a warning. Decide whether to add a warning gate,
   such as `-Werror` on our code or a warning count in the `ci.yml` build step. The brief
   asks only for zero warnings before the flag lands; a gate that keeps it at zero is a scope
   question.
6. **Both envs.** The zero-warning check must build `x4pro-gh_release` as well as `x4pro`.
   Most of the warnings exist only there, and CI does not build it.
