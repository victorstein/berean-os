Tier: heavy

# PR #190 — code quality review, pass 0

Scope: `gh pr diff 190` (`lib/Input/Input/StableLevel.h`, `lib/Input/Input/WakeupClassifier.h`,
`lib/hal/HalGPIO.{h,cpp}`, `test/stable_level/`, `test/wakeup_classifier/`, `bin/bootstrap`,
`.gitmodules`, `.claude/agents/hal-dev.md`, the `freeink-sdk` gitlink) plus the SDK change
`git -C freeink-sdk show 67f7e012`. Owner decisions (GPIO21, the `berean` fork, the `.gitmodules`
repoint, `usbDetect = 21`) and the omission of `test/CMakeLists.txt` are taken as settled.

## Pattern fit

- `WakeupClassifier.h` follows the pattern of `src/boot/BootDecisions.h`: header-only, inline, and free of
  firmware includes so it can run in a host test. It lives in `lib/Input` because `lib/hal` cannot
  include `src/`. Spec A7 records that trade-off.
- `StableLevel.h` sits in the same `input` namespace and directory as `NavKeyGestures`. It is fed
  from `HalGPIO::update()` in the same way `navGestures` is (`HalGPIO.cpp:152-176`).
- The ESP-IDF to enum translation stays in `HalGPIO.cpp` (`HalGPIO.cpp:375-416`), inside an anonymous
  namespace next to its only caller. The switches use `default:` for the open IDF enums and
  exhaustive `case`s for the closed `WakeupClass`. That is the correct split for `-Wswitch`.
- `readUsbDetectPin()` removes the only other copy of the `digitalRead(...) == HIGH` expression
  (`HalGPIO.cpp:365,371`). Nothing new duplicates code that already exists in the repo. The repo has
  no other debouncer for a level signal: `InputManager`'s debounce is private to buttons, and the
  one in `HalTiltSensor` is a cooldown.
- Test CMake files mirror `test/nav_key_gestures/CMakeLists.txt` line for line. They leave out the
  `.cpp` source because both new headers are header-only.
- The comments are "why" comments, not narration:
  - the float window and the pull-down (`HalGPIO.cpp:143-144`);
  - the `usbCanWakeFromOff` semantics (`WakeupClassifier.h:12-14`);
  - the reason for the `bootstrap` sync (`bin/bootstrap:22-23`);
  - the bench provenance on the edge-sequence tests.
- No dead or commented-out code was found.

## Tests

- `StableLevelTest` covers seeding, single-glitch rejection, and the two-sample change. It also
  checks that alternating samples never settle, and it encodes both bench edge sequences from #185.
  The `feed` helper keeps the sequences readable.
- `WakeupClassifierTest` pins every row of spec §4.5. It runs `usbCanWakeFromOff` both ways where
  that changes the result, including the X4 Pro regression row. The named `USB` / `NO_USB` /
  `OTHER_BOARD` / `X4_PRO` constants make the boolean arguments readable at the call site.

Both suites test behaviour rather than just being present. The one untested seam is the enum mapping
in `HalGPIO.cpp`. The spec (§6.2) and the intent review already acknowledge it, and it cannot be
covered without a host build of `HalGPIO.cpp`.

## Findings

### MINOR 1 — `WakeupClass` duplicates `HalGPIO::WakeupReason` and needs a mapping switch

`input::WakeupClass` (`WakeupClassifier.h:9`) has the same four members, in the same order, as
`HalGPIO::WakeupReason` (`HalGPIO.h:138`). `toWakeupReason` (`HalGPIO.cpp:402-414`) exists only to
translate one into the other. The result is two sources of truth for one concept, plus a switch that
has to stay in step with both.

Making `HalGPIO` alias the host-testable enum would remove the switch and the second definition:

    using WakeupReason = input::WakeupClass;

Every caller that spells `HalGPIO::WakeupReason::PowerButton` (`main.cpp:333,380,438-459`,
`HalDisplay.cpp:30-31`) still compiles unchanged. The spec chose "maps the result back" (§4.4 A7),
but it gives no reason for that choice. The current code is correct, so this is optional cleanup.

### MINOR 2 — the new wakeup `LOG_INF` prints twice per boot, and `x4ProUsbDetect()` reads as a noun

- `getWakeupReason()` now logs at INF on every call (`HalGPIO.cpp:425-426`), and it is called twice
  per boot (`main.cpp:375`, `HalDisplay.cpp:29`). The serial log therefore gets the same "Wakeup:"
  line twice. Two ways to fix it:
  - log once at the `main.cpp:375` call site, next to its sibling `LOG_INF("MAIN", "Device: %s", ...)`;
  - drop the `HalDisplay` call's copy to `LOG_DBG`.
- The predicate `x4ProUsbDetect()` (`HalGPIO.h:72`, `HalGPIO.cpp:373`) does not read as a question,
  unlike its siblings `deviceIsX3()`, `isXteinkDevice()` and `hasHomeKey()`. The comment above it
  (`HalGPIO.h:71`) is there to make up for the name.
  - A name like `debouncesUsbDetect()` would make that comment unnecessary.
  - The spec fixes the current name (§4.2), so this is cosmetic only.

Neither item affects behaviour. Both can be fixed inline or left.

VERDICT: CLEAR
