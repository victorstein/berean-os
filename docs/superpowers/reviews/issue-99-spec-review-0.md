# Spec review 0: issue #99, the in-memory HalStorage fake

Reviewed: `docs/superpowers/specs/2026-09-26-issue-99-design.md` against issue #99
(`gh issue view 99 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-26-issue-99-research.md`.

## Verified and sound

These claims were checked against source and hold. They are listed so the next pass
doesn't re-litigate them.

- **A-4:** `readFile` has `maxSize = 50000` and truncates silently
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-208`). It
  returns `{""}` when the open fails (`:197-199`).
- **A-5 / A-9:** `writeFile` does `exists` → `remove`, and only then opens. A failed
  open after the remove returns false (`SDCardManager.cpp:282-290`). Success is
  `written == content.length()` (`:294`).
- **A-6 on both filesystems:** `rename` uses `O_CREAT | O_EXCL | O_WRONLY` on FAT
  (`SdFat/src/FatLib/FatFile.cpp:974`) and on exFAT
  (`ExFatLib/ExFatFileWrite.cpp:312`). `mkdir` uses `O_EXCL` on FAT
  (`FatFile.cpp:379`) and on exFAT (`ExFatFileWrite.cpp:199`). The spec cites only
  FAT, but the claim holds for exFAT cards too.
- **Pass-throughs:** `SDCardManager.h:57-61` passes `mkdir(path, pFlag)`, `exists`,
  `remove` and `rename` straight to `vol()`.
- **`TagPaletteFile` result arms:** the arms the tests expect match
  `src/study/TagPaletteFile.cpp:38-75` and `TagPaletteFile.h:14,21`: Loaded, Empty,
  RecoveredFromTemp, Failed, Ok, TooLarge and WriteFailed.
- **Over-budget palette is reachable:** tombstoned entries aren't counted against
  `MAX_ACTIVE_TAGS` (`TagPalette.cpp:19`), and `toJson` emits every entry
  (`:68-73`).
- **Output side of A-11:** `serializeJson` into the proposed `String` compiles and
  appends through the default `Writer` (`Serialization/Writer.hpp:13-27`).
- **Baseline:** 851 tests (`ctest --test-dir build/test -N` → `Total Tests: 851`).
- **Line citations:** all check out. That covers the `TempAdoptionTest.cpp:3-7`
  limitation text, the `ci.yml:188-194` configure/build/test lines, the
  `test/CMakeLists.txt:42-46` warning flags (no `-Werror`), `:102`
  `temp_adoption`, and the data-dev "Shared files" rule
  (`.claude/agents/data-dev.md:22-27`).

## Findings

### 1. BLOCKER: the specified `String` can't be parsed from, so `PersistableStore.cpp` doesn't compile

**Claim (A-11, spec :102 and :207-222).** The stub `String`, with `c_str()`,
`length()`, `isEmpty()` and two `write` overloads, "is the shape ArduinoJson 7.4.2
adapts on input (`StringObject.hpp:14-17`)". Goal 1 says `PersistableStore.cpp`
compiles against it unmodified.

**Problem.** `StringObject.hpp` is the `StringAdapter`, which is used for keys and
string values. It is not what `deserializeJson` uses to read its input.
`deserializeJson(doc, json)` (`PersistableStore.cpp:55`) goes through
`makeReader(input)` (`Deserialization/deserialize.hpp:63-67`,
`Deserialization/Reader.hpp:64-67`). The only readers for a class type are these:

- `IteratorReader`, which needs `TSource::const_iterator` plus `begin()`/`end()`
  (`Readers/IteratorReader.hpp:35-40`).
- `ArduinoStringReader`, which is compiled out off-Arduino
  (`Reader.hpp:50-52`, `Configuration.hpp:177-178`).
- The default `Reader`, which calls `source_->read()` (`Reader.hpp:20-23`).

The proposed class has no `const_iterator` and no `read()`, so it fails to compile.
The research note (§3.1) makes the same mistake, so this is a false premise carried
from research into a labelled assumption.

**Evidence.** I compiled the real `lib/Serialization/PersistableStore.cpp` against
the spec's `String` block, copied verbatim, plus a minimal `HalStorage.h` and the
existing `test/stubs/Logging.h`:

```
ArduinoJson/Deserialization/Reader.hpp:22:21: error: no member named 'read' in 'String'
lib/Serialization/PersistableStore.cpp:55:22: note: in instantiation of function template
  specialization 'ArduinoJson::deserializeJson<ArduinoJson::JsonDocument &, String &, 0>' requested here
```

**Fix.** Add an iterator range to the stub `String`, and correct A-11's evidence to
cite `IteratorReader.hpp:35-40` for input:

```cpp
using const_iterator = std::string::const_iterator;
const_iterator begin() const { return s_.begin(); }
const_iterator end() const { return s_.end(); }
```

With those three lines added, the same compile of `PersistableStore.cpp` succeeds
with no errors or warnings under `-Wall -Wextra -pedantic`. It still doesn't
satisfy `is_std_string` (no `append`/`push_back`), so the `Writer` analysis at spec
:224-229 is unchanged. A standalone serialize-then-deserialize round trip with the
fixed class exits 0.

### 2. MAJOR: the chained rename-failure → adoption test contradicts the sticky hooks

**Claim.** The `AtomicWriteTest` bullet (spec :359-361) says that after
`failRenamesFrom(path.tmp)`, "A following `readDocFromFileAdopting` returns Ok with
the new document **and promotes it**." The Data-flow section repeats it at :266-267
("One test chains the two to prove it").

**Problem.** A-10 (:101) makes hooks "sticky per path until
`storage_fake::reset()`". `reset()` also wipes every file and directory (:190,
:350). The promotion is `Storage.rename(tmpPath, path)`
(`PersistableStore.cpp:80`), which is a rename *from* `path.tmp`, the same hooked
path. So it fails again. The adopting read returns Ok, but the primary stays absent
and the `.tmp` stays put, which is the opposite of "promotes it".

`storage_fake` (:189-197) has no way to clear a hook while keeping the card. The
test as specified can't be written. An implementer would either silently drop the
promotion assertion or invent a control the spec doesn't declare.

**Evidence.** Spec :101 (sticky until `reset()`), :190 (`reset()` empties the
card), :194 (the only rename control), :359-361 (the promotion assertion), and
`PersistableStore.cpp:80` (the promote is a rename from `tmpPath`).

**Fix.** Pick one of these and state it in A-10 and in the controls block:

- Add `void clearFailures();` (hooks only, the card is kept) to `storage_fake`. The
  test then calls it between the failed write and the adopting read. This is the
  smaller change, and #98's tests will want the same thing.
- Or keep hooks immutable and change the bullet to assert the sticky outcome: Ok,
  document loaded, primary still absent, `.tmp` still present. That duplicates the
  existing `AdoptingReadTest` rename-fails case and loses the "write failure is
  recoverable" proof the chain exists for.

The first option keeps the spec's intent.

### 3. MINOR: comments this change makes false aren't in "Files touched"

**Claim.** The spec says (:231-234) that of the stubs-path suites, none includes
`<Arduino.h>` apart from `font_page_slots`.

**Problem.** The same spec makes the new `test/stubs/HalStorage.h` include
`<Arduino.h>` (:140). The pagination suite includes `<HalStorage.h>`
(`TextBlock.cpp`), so it now pulls in the stub `Arduino.h` too. That is harmless:
there are no `String` or `millis` identifiers in `test/pagination/*.cpp` or the
sources it compiles (grep: 0 hits). But several comments, written for the merged
state, become wrong:

- `test/stubs/Arduino.h:3-7`: "none of them includes <Arduino.h>, and
  test/pagination exists specifically to keep it out", and "for millis() and
  micros()" only.
- `test/pagination/CMakeLists.txt:8-10`: "which keeps <Arduino.h> … out of the
  build". `:26` also still says "no-op … HalStorage.h".
- `test/temp_adoption/TempAdoptionTest.cpp:3-7`: "PersistableStore.cpp cannot be
  built on the host … the Storage call sequence in readDocFromFileAdopting stays
  device-verified only". That is exactly what `AdoptingReadTest` now covers.
- `test/highlight_file/CMakeLists.txt:1-4`,
  `test/credential_integrity/CMakeLists.txt:12-13` and
  `test/bookmark_save_action/CMakeLists.txt:1-2` say the same thing about
  `PersistableStore.h` pulling in `Arduino.h`.

**Fix.**

- Correct spec :231-234 to say the stub `Arduino.h` now reaches pagination through
  `HalStorage.h`, and that this is harmless.
- Add `test/stubs/Arduino.h`, `test/pagination/CMakeLists.txt` and
  `TempAdoptionTest.cpp` to "Files touched", as comment-only edits.
- The per-store CMakeLists comments stay true until those stores get suites, so
  those can be left alone or reworded to say "not yet built here".

### 4. MINOR: the "defined subset" and "real signatures" miss two mechanical details

**Problem.**

- **Missing definitions.** The inline `getInstance()` returns the static `instance`
  (`lib/hal/HalStorage.h:48,53`), so any `Storage.` call needs a definition of
  `HalStorage HalStorage::instance`. That in turn needs a body for the declared
  `HalStorage()` (`:15`). Neither is in the "Defined `HalStorage` methods" list
  (spec :170-173).
- **Dangling `override`.** A-2 drops the `Print` base but keeps "the real
  signatures". The real `write(const uint8_t*, size_t)` and `write(uint8_t)` are
  marked `override` (`:88,90`), which doesn't compile without a base.

**Fix.**

- Add `HalStorage::HalStorage()` and the `instance` definition to the defined list
  in `HalStorageFake.cpp`.
- State in A-2 that `override` is dropped from those two declarations along with the
  `Print` base.

Both would surface at the first build, so neither risks a silent divergence.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
