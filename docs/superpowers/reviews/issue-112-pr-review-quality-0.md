# PR #126 review: code quality, pass 0

Scope: `gh pr diff 126`, the five non-design files: `platformio.ini`, `.clangd`, `AGENTS.md`,
`USER_GUIDE.md` and `.github/ISSUE_TEMPLATE/bug_report.yml`. `docs/superpowers/` is the design
trail and is not reviewed here for quality. No C++ changed.

## Findings

None at BLOCKER, MAJOR or MINOR.

## What was checked

**It uses the existing pattern, not a new one.** Release logging is switched off by leaving out
the one define the tree already gates on. There is no new switch or macro.
- `ENABLE_SERIAL_LOG` gates the `LOG_*` macros (`lib/Logging/Logging.h:44-65`) and serial bring-up
  (`src/main.cpp:343-354`).
- It also gates the HWCDC teardown (`lib/hal/HalPowerManager.cpp:70`) and the SDK's own logging
  (`freeink-sdk/.../PaperMonoDriver.cpp:514` and others, `MemoryManager.cpp:26`,
  `BoardConfig.h:1654`).
- The release env now has the shape of `[env:x4pro]` (`platformio.ini:161-176`) minus that define.
- `WifiSelectionActivity.cpp:552` gates on `defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2`. It stays
  consistent with this change, because both halves are off in release.

**The `-DLOG_LEVEL=0 ; inert without ENABLE_SERIAL_LOG` line (`platformio.ini:187`) is not dead
config under a restating comment.**
- The value equals the header default (`Logging.h:30-32`), and nothing reads it outside
  `#ifdef ENABLE_SERIAL_LOG`. So the line has no effect on the build.
- It is kept on purpose (spec A-1): `AGENTS.md:173` documents the release env as "`LOG_LEVEL=0`, no
  serial logging", and the line keeps the config readable against that.
- The comment gives the non-obvious reason. `Logging.h:16` says "0 = ERR only", so a reader would
  otherwise expect errors to still log. It does not repeat what the line does.
- A trailing `;` comment is how the removed line was written (`-DLOG_LEVEL=1 ; Set log level…`).

**Nothing is left dead or duplicated in the diff.**
- The old `LOG_LEVEL=1` comment was replaced, not left behind.
- Outside `docs/superpowers` and `freeink-sdk`, `git grep` for `c++2a`, `LOG_LEVEL=1` and
  "lower level than development" returns nothing. Nothing stale remains after the doc edits.

**The CMD handler stays compiled in (`src/main.cpp:623-638`).**
- The handler is still built into release, but it cannot run: `Serial.begin()` is never called, so
  `logSerial.available()` returns -1 (PR body; spec A-2).
- This is a recorded spec decision. The PR body offers the `#ifdef ENABLE_SERIAL_LOG` wrap as
  optional hardening for the owner of `main.cpp` and does not edit that file.
- The handler already existed and this diff does not change it, so it is not scored.
- The wrap would be the cleaner end state. It reuses the guard that sits 280 lines above in the same
  file, and it removes the need to depend on nobody calling `Serial.begin()`.

**The docs are consistent with each other.**
- `AGENTS.md:176` and `.clangd:2` now match `platformio.ini:40` (`-std=gnu++2a`). `CLAUDE.md` is a
  symlink to `AGENTS.md`, so both pick up the change.
- The User Guide's "section 16" reference resolves to `USER_GUIDE.md:343` (`## 16. Firmware
  updates`), and SD firmware update is covered at `:349`.
- The bug-report text points to the User Guide instead of repeating its steps, which avoids a
  second copy of the instructions.

**Tests.**
- A build-flag change has no host-test seam. Spec A-9 records why.
- The verification is a set of before/after `strings` counts on the release binary: the
  `logPrintf` prefix goes 1 → 0 and heap `LOG_INF` goes 1 → 0.
- There are two controls. The handler string stays at 1, which documents that the handler is
  unreachable rather than absent. The dev image keeps the prefix, which guards the dev env.
- These checks test what the change does to the shipped image, not how it is implemented. That is
  the right design for a change that is only build configuration.

VERDICT: CLEAR
