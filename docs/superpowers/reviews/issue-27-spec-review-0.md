# Adversarial review — issue #27 spec, pass 0

**Target**: `docs/superpowers/specs/2026-09-16-issue-27-design.md`
**Against**: `gh issue view 27`, `docs/superpowers/research/2026-09-16-issue-27-research.md`
**Worktree**: `/Volumes/stein/.herdr/worktrees/berean-os/fix-27-atomic-store-saves` @ `3065d001`
**Date**: 2026-09-16

## What holds up

Before the findings, the parts I tried to break and could not. The citation
accuracy is unusually high: I spot-checked 30+ `file:line` references and every
one resolved to what the spec says it does, including the non-obvious ones
(`EpubReaderActivity.cpp:151` really is inside `moveFinishedBookToReadFolder`,
`:133-153`; `ProgressFile::writeAtomic` really is `src/activities/reader/ProgressFile.h:32`
called from `EpubReaderUtils.h:35`; `LauncherActivity.cpp:107` really is
change-guarded; `HalStorage.cpp:56,89,93,94` are `writeFile`/`mkdir`/`remove`/`rename`).

The call-site census reproduces exactly:

```
$ grep -rn 'saveToFile()' --include='*.cpp' src | wc -l
55
$ grep -rn "public PersistableStore<" src lib | wc -l
4
```

and the per-store split (Settings 35, State 9, Wifi 5, Recent 6) is right, which
means the research note's correction of the issue body's 27 is correct and the
issue body is what is wrong. The `SettingsList.h` key derivation reproduces to the
character (`53` unique, `898` chars). §2's rejection of the name-hiding wrapper is
correct C++ and correct about *why* — `saveToFile()` is non-virtual at
`PersistableStore.h:126` and `:173` calls it on `this` through the base.
`writeDocToFile` keeps a legitimate caller after the change
(`src/util/BookmarkFile.cpp:62`), so A8's "don't delete it" survives.

`A2` is sound and I could not break it. `A7` is sound. The "no host test can
cover this" claim is correct.

Five MAJORs follow. None reverses a decision, none needs a judgment I cannot make
from the code, and I rule on both Open Questions at the end so the implementer is
not blocked.

---

## MAJOR 1 — A4's cited evidence is false, and the real unbounded write path into `WifiCredentialStore` is a file the spec never opened

**Claim.** A4 (spec:299-307): "`MAX_PASSWORD_LENGTH = 64` … is enforced only on the
load path … `addCredential` (`:112`) does not check it, and **no cap on
`enteredPassword` (`WifiSelectionActivity.h:77`) was found**. 64 is what the store
*intends*, not what it *enforces*, so a budget derived from it would refuse a save
the UI happily produced." The research note says the same (research:166-172).

**Problem.** The UI *does* cap it, at exactly 64. The negative claim is wrong, and
the path that actually is unbounded — the web server — is never mentioned by
either document, even though the research note cites the same file at `:1320` for
a different reason.

**Evidence.**

The password keyboard is constructed with an explicit bound:

```cpp
// src/activities/network/WifiSelectionActivity.cpp:350-353
startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD),
                                                               "",  // No initial text
                                                               64,  // Max password length
                                                               InputType::Text),
```

and the hidden-SSID entry is bounded the same way at `WifiSelectionActivity.cpp:375`
(`32,  // Max SSID length (IEEE 802.11: 32 bytes)`). The bound is enforced, not
decorative — `KeyboardEntryActivity.cpp:250`:

```cpp
if (maxLength != 0 && text.length() + n > maxLength) return;
```

Both `addCredential` call sites in the UI (`WifiSelectionActivity.cpp:67,691`)
pass `enteredPassword`, which is only ever assigned from that keyboard result
(`:358`) or from an already-loaded credential (`:331,402,650`) that the load path
has already capped at 64 (`WifiCredentialStore.cpp:52-56`).

The genuinely unbounded producer is `CrossPointWebServer::handlePostWifiNetwork`
(`src/network/CrossPointWebServer.cpp:1362`), which validates only that the SSID
is non-empty:

```cpp
// CrossPointWebServer.cpp:1376-1379
std::string ssid = doc["ssid"] | std::string("");
if (ssid.empty()) { server->send(400, "text/plain", "SSID is required"); return; }
// :1385 — no bound of any kind
std::string password = doc["password"] | std::string("");
```

and then calls `addCredential` at `:1406`, `:1408` and `:1418`.

**Why it matters beyond the footnote.** Two things fall out that the spec's Error
handling section and A6 both assert are not true:

1. A budget refusal **already reaches a human** on this path. `addCredential`
   returns false, and `CrossPointWebServer.cpp:1419-1421` answers
   `400 "Cannot add network (limit reached)"` — which after this change becomes a
   *wrong* message for a budget refusal, and `:1412` answers
   `"Failed to update Wi-Fi network"`. So §Error handling's "Callers cannot tell
   them apart" (spec:242) is true, but "the serial log can" is not the only
   consequence: one caller turns the indistinguishability into a misleading HTTP
   400.
2. The 8192 figure is defensible, but for the opposite reason to the one given.
   The on-disk steady state really is bounded, because `fromJson` discards any
   credential with `password_len > MAX_PASSWORD_LENGTH` and requests a resave
   (`WifiCredentialStore.cpp:52-56`) — so an oversized web-written password
   survives exactly until the next boot. That is a *stronger* argument for a
   computable worst case than the one A4 makes, and it makes the derivation
   honest: 8 × (ssid ≤ 32 + base64(64) ≈ 88 + `password_len` + `password_crc32` +
   ~45 chars of key name + ~25 of JSON punctuation) + `lastConnectedSsid`
   ≈ 1.7 KB, which is what the table already says.

**Concrete fix.**
- Rewrite A4. Delete the "no cap on `enteredPassword` was found" sentence. State
  that the UI enforces 64/32 (`WifiSelectionActivity.cpp:352,375`,
  `KeyboardEntryActivity.cpp:250`), that `CrossPointWebServer.cpp:1376,1385` is
  the unbounded producer, and that `WifiCredentialStore.cpp:52-56` bounds the
  steady state regardless. Keep **8192**; the number is right, the reason was not.
- Add a bullet to §Error handling: `CrossPointWebServer.cpp:1419-1421` reports a
  refusal as "limit reached", which is wrong for a budget refusal. Either widen
  that message to "Cannot add network" or split it — one line, same file the change
  already touches (`:1320` is a converted call site).
- Correct research note lines 166-172 the same way, since the spec inherited the
  error from it.

---

## MAJOR 2 — a silently-ignored `SAVE_BUDGET` is invisible to every step of the verification ladder

**Claim.** §3 (spec:122-123): "Each store declares `static constexpr size_t
SAVE_BUDGET` **next to its existing constants**, picked up by `saveBudget()` at
`PersistableStore.h:111-117`." §Testing step 1 (spec:381-384) treats `pio run` as
the thing that first exercises the `if constexpr` branch.

**Problem.** `requires { T::SAVE_BUDGET; }` is a *soft* test. If the constant is
not reachable from `PersistableStore<T>` — wrong access, wrong spelling, declared
in the `.cpp` instead of the header — the branch is simply not taken, the store
silently gets `persist::DEFAULT_SAVE_BUDGET` (45,000), and **every** step of the
spec's ladder passes: it compiles, it formats, and the grep in step 3 returns
nothing. The one property the change is supposed to add for three of the four
stores would be absent and nothing would say so.

This is not hypothetical for this codebase. "Next to its existing constants" puts
it in a `private:` section for two of the four stores — `WifiCredentialStore.h:35-36`
(`MAX_NETWORKS`, `MAX_PASSWORD_LENGTH`, after `private:` at `:27`) and
`RecentBooksStore.h:21` (`MAX_RECENT_BOOKS`, after `private:` at `:18`). It works
today only because of `friend class PersistableStore<…>` at `WifiCredentialStore.h:41`
and `RecentBooksStore.h:26` — a line 6 to 15 rows away that nothing connects to the
budget, and that a future cleanup would remove without a diagnostic.

**Evidence.** Compiled with this project's own toolchain, not a host approximation:

```
$ ~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-g++ --version
xtensa-esp-elf-g++ (crosstool-NG esp-14.2.0_20251107) 14.2.0

$ cat sa.cpp   # saveBudget() copied verbatim from PersistableStore.h:111-117
class A : public PersistableStore<A> { private: static constexpr size_t SAVE_BUDGET = 8192;
                                       friend class PersistableStore<A>; };
class B : public PersistableStore<B> { private: static constexpr size_t SAVE_BUDGET = 8192; };
static_assert(A::saveBudget() == 8192,  "A friended private budget must apply");
static_assert(B::saveBudget() == 45000, "B unfriended private budget silently ignored");

$ xtensa-esp-elf-g++ -std=c++2a -fsyntax-only sa.cpp && echo OK
OK
```

Clang 21 on the host agrees (`privateWithFriend=8192`, `privateNoFriend=45000`).
No warning is emitted in either case.

**Concrete fix.** Add a fourth line to §Testing's ladder and one line per store,
turning the silent degradation into a build error. This is checked by `pio run`,
costs nothing at runtime, and does not touch `saveToFileAtomic`:

```cpp
// src/CrossPointState.cpp, at namespace scope after the class is complete
static_assert(CrossPointState::saveBudget() == 2048,
              "SAVE_BUDGET is not reaching saveBudget() -- check access and spelling");
```

Verified this compiles even when `SAVE_BUDGET` is private, because `saveBudget()`
is public and returns the value (tested on the xtensa toolchain above). Add the
equivalent for `CrossPointSettings` (4096) and `WifiCredentialStore` (8192), and
for `RecentBooksStore` assert `== persist::DEFAULT_SAVE_BUDGET` so A5's decision is
also pinned rather than assumed.

§3 should additionally say, in one sentence, that the constant must be public or
covered by the store's existing `friend class PersistableStore<…>` declaration —
otherwise it is inert.

---

## MAJOR 3 — the legacy path is mis-described, and the spec accepts a cost it does not actually introduce

**Claim.** The flow diagram (spec:188-199) renders the before-state as
`Storage.writeFile(path)  ← tears here` and the after-state as introducing
`Storage.remove(path)` + `Storage.rename(…)`. §Error handling then says
(spec:256-261) "**The window where neither file exists is real and accepted.**
Between `Storage.remove` and `Storage.rename` … a power loss leaves no file …
It does mean the change reduces the tear window rather than eliminating all loss."
§Risks repeats it (spec:422-423).

**Problem.** The legacy path already removes the destination before writing a
byte, so the "no file exists" window is not introduced by this change — it is
*pre-existing and strictly larger*, spanning the entire serialise-and-write rather
than two directory-entry updates. The spec presents an unambiguous improvement as
a trade-off, and mislabels where the legacy path fails.

**Evidence.** `Storage.writeFile` → `SDCardManager::writeFile`:

```cpp
// freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:276-295
bool SDCardManager::writeFile(const char* path, const String& content) {
  ...
  if (vol().exists(path)) {
    vol().remove(path);          // <-- the real file is gone before anything is written
  }
  FsFile f;
  if (!openFileForWrite("SD", path, f)) { ... }
  const size_t written = f.print(content);
```

and `openFileForWrite` opens `O_RDWR | O_CREAT | O_TRUNC`
(`SDCardManager.cpp:337`). So the legacy sequence is remove → create → write the
whole document; the atomic sequence is write the whole document to `.tmp` →
remove → rename. The unprotected interval shrinks from "the length of the write"
to "two directory updates".

Two useful corollaries the spec should also carry, both from the same lines:
`O_TRUNC` means a leftover `settings.json.tmp` from a previous interrupted save is
truncated rather than appended to, so verification step 7's "stray `.tmp`" check
is a hygiene check and not a correctness hazard.

**Concrete fix.** Correct the `before:` branch of the diagram to
`Storage.writeFile(path)` → `remove + create + write ← no-file window spans the whole write`,
and rewrite the §Error handling bullet to say the change *shrinks* the pre-existing
no-file window rather than accepting a new one, citing `SDCardManager.cpp:282-284,337`.
Drop or requalify the matching §Risks bullet.

---

## MAJOR 4 — the flash cost is unmeasured, unmentioned, and lands against an invariant documented in the file being edited

**Claim.** §4 (spec:180): "**No new pattern is introduced by this change.**"
§Data and control flow (spec:202-204): "the cost delta per save is **two extra
directory operations**". Verification 6 accounts for RAM (two `std::string`s) and
nothing accounts for flash.

**Problem.** `saveToFileAtomic` calls `measureJson(doc)` *in the header*
(`PersistableStore.h:148`). It is a member of a class template, so it is
instantiated in every translation unit that calls it — pulling ArduinoJson's
serializer into 23 TUs that currently do not have it. That is precisely the thing
`PersistableStoreBase` exists to prevent, per its own class comment:

```cpp
// lib/Serialization/PersistableStore.h:16-20
// All ArduinoJson parse/serialize machinery is instantiated once here (in
// PersistableStore.cpp) instead of in every store's translation unit. GCC
// emits the JSON serializer/parser templates as local .isra clones per TU
// (~0.5KB each), so keeping serializeJson/deserializeJson out of the stores
// is what makes the abstraction flash-neutral.
```

`saveToFile()` honours that invariant — it calls only the out-of-line
`writeDocToFile`. `saveToFileAtomic()` does not. Of the 24 call-site TUs, exactly
one already instantiates the serializer:

```
$ for f in $(grep -rln 'saveToFile()' --include='*.cpp' src); do \
    grep -q 'serializeJson\|measureJson' "$f" && echo "$f"; done
src/network/CrossPointWebServer.cpp
```

**Evidence — measured, not asserted.** Built with the target toolchain at `-Os`
with the project's `-fno-exceptions -fno-rtti -ffunction-sections`, against
ArduinoJson 7.4.2 (the pinned version, `platformio.ini:151`). One TU calling
`writeDocToFile` versus one calling `measureJson` + `writeDocToFileAtomic`:

```
   text	   data	    bss	    dec	filename
    218	      4	      0	    222	base_tu.o      (legacy shape)
   1880	      4	      0	   1884	atomic_tu.o    (atomic shape)
```

Partial-linked at 24 TUs, so COMDAT folding is applied:

```
   text	   data	    bss	    dec	filename
   4974	      4	      0	   4978	legacy24.o     (24 legacy TUs)
   6636	      4	      0	   6640	mixed1.o       (1 atomic + 23 legacy)
   8798	      4	      0	   8802	atomic24.o     (24 atomic TUs)
```

**≈ +3,824 bytes of `.text`**: ~1,662 B one-time for
`JsonSerializer<DummyWriter>` (weak/COMDAT, folded once) plus ~94 B per additional
calling TU (a 31-byte `MemoryPoolList::getSlot$isra$0` local clone that does *not*
fold, plus a larger caller body).

**This is not a blocker.** 3.8 KB is 0.06 % of the 6,553,600 B app partition
(`partitions.csv:4-5`). But CLAUDE.md requires the mechanism to be stated rather
than a cost silently assumed away, and §4's "no new pattern is introduced" is
false about the one property `PersistableStore.h:13-20` was written to defend.

**Concrete fix.** Add a paragraph to §Data and control flow giving the figure and
the mechanism (header-instantiated `measureJson`, mostly COMDAT-folded, ~94 B per
calling TU), and note that it knowingly relaxes the `PersistableStore.h:16-20`
invariant for an affordable amount. Add a §Testing step: record the `pio run` flash
delta against `main` and flag it if it exceeds ~6 KB, which would mean the folding
assumption did not hold in the real link.

---

## MAJOR 5 — A6 bundles two separable asks and declines both; one of them is inside the issue's constraints and costs six lines

**Claim.** A6 (spec:325-334) and §Error handling (spec:246-249): "A refusal is
never silent. `saveToFileAtomic` logs it itself … This is true at all 56 sites
without any of them doing anything, **which is the substance of acceptance
criterion 3**."

**Problem.** Criterion 3 does not say "a refusal must be logged somewhere". It
says: *"Call sites that currently discard the return value
(`RecentBooksStore.cpp:62,74,102` at minimum) either handle a false or `LOG_ERR`
on it."* A6's entire justification is about a **popup** — `tr()`, the i18n
constraint, `STR_FAILED_LOWER`. All of that is correct and the popup is correctly
deferred. None of it applies to a local `LOG_ERR` at three named call sites, which
needs no string, no YAML, and no unblocking. A6 uses an argument about the
expensive half to decline the cheap half that the issue named explicitly.

The local log is not redundant with the base-class one. `PersistableStore.h:150`
emits `Refusing to save /.crosspoint/recent.json: N bytes exceeds budget M` — a
file and two numbers, with no indication of *which operation* was lost. At
`RecentBooksStore.cpp:62` what was lost is a book add; at `:74` a metadata update;
at `:102` a path repoint after a book was moved into `/Read`. The store already
demonstrates the pattern it should follow four lines below, at `:85-87`:

```cpp
if (!saveToFile()) {
  LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
}
```

**Evidence.** Issue #27 brief, acceptance criterion 3 (verbatim above);
`RecentBooksStore.cpp:62,74,85,102`; `PersistableStore.h:150-151`.

**Concrete fix.** Split A6 in two.
- **A6a**: the three sites the issue names get a local `LOG_ERR` in the `:85`
  style, naming the operation. In scope, ~6 lines, no i18n. Do it.
- **A6b**: no user-visible popup — keep A6's existing reasoning verbatim, and
  note that `CrossPointWebServer.cpp:1412,1419` is the one converted path that
  *does* already surface a failure to a human (see MAJOR 1).

Then delete the sentence "This is true at all 56 sites without any of them doing
anything, which is the substance of acceptance criterion 3" — it is the one place
the spec asserts satisfaction of a criterion it is narrowing.

---

## MINOR 1 — the Environment note is stale; both bootstrap steps are already done

Spec:434-441 tells the implementer "This worktree cannot build yet", citing an
uninitialised `freeink-sdk` and a missing `.venv`. Research:196-218 says the same.
Both are false as of this review:

```
$ git submodule status
 310ec61506fc915836db7799a2e7f4fc135a570d freeink-sdk (remotes/origin/fix/interaction-table-theme-token-race-29-g310ec61)
$ .venv/bin/clang-format --version
clang-format version 21.1.8
```

No leading `-` on the submodule line, and the venv binary clears
`bin/clang-format-fix:26-31`'s version-21 gate. **Fix:** delete the section, or
reduce it to the one fact still true — `~/.platformio/penv/bin/pio` is the pio
entry point and the bare `pio` is not on `PATH`.

## MINOR 2 — "six cases" in the save-budget suite; there are five

Spec:373 and research:139 both say `test/save_budget/SaveBudgetTest.cpp` has six
cases. `grep -c '^TEST(' test/save_budget/SaveBudgetTest.cpp` → `5`, in a 36-line
file. The two cases named by line (`:28-30`, `:32-36`) are correct. **Fix:** five.

## MINOR 3 — "Two comment edits follow from the change and nothing else does" is false

Spec:230 asserts a closed set of two. At least two more go stale, and one becomes
directly wrong:

- `PersistableStore.h:172` — `// Deliberately outside the lock: saveToFile() takes
  storeMutex itself.` sits immediately above the line §1 converts. After the change
  it names a function the code below no longer calls.
- `src/WifiCredentialStore.h:32` — `// I/O; saveToFile() snapshots under this mutex
  from toJson().` Describes `credentialMutex`, still true in substance, wrong by
  name.
- `PersistableStore.h:41-42` and `:96` warn that calling `saveToFile()` from inside
  `fromJson()` deadlocks. Still true, but both should now name `saveToFileAtomic()`
  as well, since it takes the same lock at `:144`.

**Fix:** change "Two comment edits" to the enumerated set, and note that step 3's
grep (`--include='*.cpp'`) cannot see the two `.h` occurrences — either widen it to
`grep -rn 'saveToFile()' src` (expected: only the two corrected comments) or add a
second command.

## MINOR 4 — `CrossPointState` is called "fixed shape" but carries two unbounded strings

The §3 table (spec:127) lists `CrossPointState` under "Real bound: fixed shape".
Two of its eleven serialised fields are unbounded `std::string` SD paths —
`openEpubPath` (`CrossPointState.h:16`) and `bibleCoverPath` (`:21`) — which is the
same property A5 uses to *refuse* a tight budget for `RecentBooksStore`. The
derivation at spec:134-136 quietly assumes 255 bytes each without saying so, and
2048 refuses at a combined path length of roughly 1,500 characters.

That is almost certainly unreachable on this device, so **keep 2048**. **Fix:**
replace "fixed shape" with "fixed key set; two path strings, assumed ≤ 255 B each"
and state the refusal threshold, so the assumption is attackable rather than buried.

## MINOR 5 — the `CrossPointSettings` key arithmetic double-counts, in the safe direction

Spec:137-141: "53 distinct keys, 898 characters of key name — plus 9 hand-written
keys (136 characters)". The grep reproduces 53/898 exactly, but three of those
53 — `"fontFamily"`, `"fontSize"`, `"longPressMenuFunction"` — are the dynamic
entries the generic loop skips (`CrossPointSettings.cpp:69`,
`if (!info.valuePtr && !info.stringOffset) continue;`) and the tail writes by hand,
so they are counted twice. And the nine tail key names total 127 characters, not
136. Both errors inflate the estimate, so 4096 is still ≥ 2.4× the real worst case.
**Fix:** say the 53 is an upper bound that includes three keys the tail also writes,
rather than presenting it as a derived count.

## MINOR 6 — `CardBooks.cpp:59` is a redundant save that becomes a redundant *atomic* save

```cpp
// src/util/CardBooks.cpp:59
if (RECENT_BOOKS.removeByPath(bookPath)) RECENT_BOOKS.saveToFile();
```

`removeByPath` already persists (`RecentBooksStore.cpp:85`) and returns true only
when it removed something — so this is a second, unconditional write of the same
document. Today that is one extra `writeFile`; after the change it is a second
full write-tmp + remove + rename of `recent.json`, doubling the no-file window on
a file that was just correctly saved. CLAUDE.md's storage discipline says "Guard
redundant writes". (`RecentBooksActivity.cpp:72` is *not* an instance of this —
it is guarded by `pruneMissing()`, which does not persist by design,
`RecentBooksStore.h:55`.)

**Fix:** drop the trailing `RECENT_BOOKS.saveToFile()` at `CardBooks.cpp:59`, or
add a one-line note to §1 saying it is knowingly kept. Either is fine; silently
converting it is not.

---

## Rulings on the spec's two Open Questions

The spec routes both to the review (spec:425-432). Both are answerable from the
issue text and the code, so neither needs to go back to the human.

**Open question 1 — A5, `RecentBooksStore` keeps `DEFAULT_SAVE_BUDGET`: ACCEPTED.**
Acceptance criterion 2 contains its own escape hatch — *"or documents in a comment
why `persist::DEFAULT_SAVE_BUDGET` (45,000 B) is the right ceiling for it"* — and
A5's reason (10 entries × 4 strings, `title`/`author` unbounded from EPUB metadata
via `BookMetadataCache.h:15-16` → `ReaderActivity.cpp:60`, no truncation on the
path) is exactly the documentation that hatch asks for. Do not fold in truncation:
the repo's own precedent for bounding user strings at write time —
`PassageDoc.h:26-28` (`SAVE_BYTE_BUDGET = 200000`, `MAX_SNIPPET_BYTES = 120`,
`MAX_REFERENCE_BYTES = 48`) — shows it is a designed field-length decision, not a
rider. Requirements on the accepted answer: the comment must state the concrete
worst case it is accepting (10 × 4 unbounded strings), and A5's "that is a separate
issue" must become a filed issue rather than a sentence.

**Open question 2 — A6, is `LOG_ERR` sufficient: YES for the popup, NO as written.**
Do not unblock this issue from the i18n constraint; A6b's reasoning is correct and
`STR_FAILED_LOWER` (`english.yaml:134`, used bare at `BmpViewerActivity.cpp:231`)
would indeed be worse than a log line. But see MAJOR 5: the three call sites the
criterion names by line still get their local `LOG_ERR`, and the misleading HTTP 400
at `CrossPointWebServer.cpp:1419` still gets fixed. With those two done, criterion 3
is met literally rather than by reinterpretation, and nothing about the popup
decision changes.

---

## Summary

No BLOCKERs. The spec's research is accurate where it looked, and its two
structural calls (convert the sites rather than hide the base method; convert
`PersistableStore.h:173` too) are both right for the reasons given. The five MAJORs
are one false assumption with a real seam behind it (A4 / the web server), one
verification gap that would let the change's headline feature fail silently
(`SAVE_BUDGET`), two corrections to the cost and mechanism narrative (the legacy
path's existing no-file window; +3.8 KB of flash), and one acceptance criterion
narrowed past what its own constraints require. All five are fixable in the spec
plus about six lines of implementation, and both Open Questions are ruled above.

BLOCKERS: 0
MAJORS: 5
MINORS: 6

VERDICT: CLEAR
