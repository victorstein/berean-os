# Atomic, budgeted saves for the four inherited stores

**Status**: specified, not implemented
**Date**: 2026-09-16
**Issue**: #27 · **Branch**: `fix/27-atomic-store-saves`
**Research**: `docs/superpowers/research/2026-09-16-issue-27-research.md`

Modelled on `docs/superpowers/specs/2026-09-14-meetings-library-design.md` — the
same shape (problem, the constraint that shapes it, a numbered design, testing,
risks) for a change of comparable size. The `## Error handling` section follows
`docs/superpowers/specs/2026-08-18-tagged-highlights-design.md:319-325` (terse
bolded-lead bullets).

Where this spec contradicts the issue body, the research note says why; both
places are cited.

## What changed after review pass 0

Review: `docs/superpowers/reviews/issue-27-spec-review-0.md` — **CLEAR**, 0
BLOCKERs, 5 MAJORs, 6 MINORs. All eleven are applied here; three changed the
design rather than the prose.

| # | Was | Is now |
|---|---|---|
| MAJOR 1 | A4: "no cap on `enteredPassword` was found" | False. The UI caps at 64 (`WifiSelectionActivity.cpp:352`, enforced `KeyboardEntryActivity.cpp:250`); the unbounded producer is `CrossPointWebServer.cpp:1385`, which also mis-reports a refusal as "limit reached" (`:1419`). 8192 kept, rationale rewritten, **and the HTTP 400 message is now in scope**. |
| MAJOR 2 | "declare `SAVE_BUDGET` next to its existing constants" | That is a `private:` section in two stores, and an unreachable `SAVE_BUDGET` is silently ignored — every verification step still passes. **Four `static_assert`s added** as ladder step 2. |
| MAJOR 3 | "the window where neither file exists is real and accepted" | The legacy path already removes before writing (`SDCardManager.cpp:282-284`, `O_TRUNC` `:337`), so the window is pre-existing and *larger*. The change shrinks it; the diagram, §Error handling and §Risks all said otherwise. |
| MAJOR 4 | "no new pattern is introduced" | `measureJson` in the header instantiates ArduinoJson's serializer in 23 new TUs, against the invariant at `PersistableStore.h:16-20`. Measured **+3,824 B `.text`**; stated, and re-checked by ladder step 5. |
| MAJOR 5 | A6 declined popup *and* per-site logging together | Criterion 3 names `RecentBooksStore.cpp:62,74,102` by line and needs no i18n. Split into **A6a** (do it, ~6 lines) and **A6b** (popup still deferred). |
| MINOR 1-6 | — | Stale environment note deleted; "six cases" → five; the comment set is four, not two, and ladder step 4 widened to see headers; `CrossPointState` no longer called "fixed shape"; the settings key count marked an upper bound that double-counts three keys; `CardBooks.cpp:59` deleted rather than converted. |

Both Open Questions were ruled by the review and are closed below.

---

## The problem

`PersistableStore::saveToFileAtomic()` (`lib/Serialization/PersistableStore.h:143-155`)
measures the serialised document against a per-store budget, refuses rather than
truncates, and writes through a temp file and a rename. It has **zero callers**.

All four `PersistableStore` subclasses in the tree —

```
src/CrossPointSettings.h:8      CrossPointSettings   /.crosspoint/settings.json
src/CrossPointState.h:8         CrossPointState      /.crosspoint/state.json
src/WifiCredentialStore.h:26    WifiCredentialStore  /.crosspoint/wifi.json
src/RecentBooksStore.h:17       RecentBooksStore     /.crosspoint/recent.json
```

— save through `saveToFile()` (`PersistableStore.h:126-131`), which is
`writeDocToFile` (`PersistableStore.cpp:11-20`): one `Storage.writeFile` over the
live file, no measurement.

The failure chain is the one `SaveBudget.h:6-14` documents: a write interrupted
by a flat battery tears the real file, `readDocFromFileChecked`
(`PersistableStore.cpp:46-61`) fails to parse it, `loadFromFile` returns false,
the store keeps its defaults, and the next save commits those defaults over what
survived. Language, font choice, WiFi credentials and the open-book path are four
separate files, but they all sit on the same chain.

There is no "legacy path kept for the stores that already use it" in the sense
the comment at `PersistableStore.h:124-125` implies: those four stores *are* every
store. After this change `saveToFile()` has no store callers left at all.

## Goal

The four stores persist through `saveToFileAtomic()`, each with a budget derived
from its actual serialised shape, and a budget refusal is not swallowed silently.

## Non-goals

- **Deleting or reimplementing `saveToFile()`.** Issue constraint. It stays,
  unused by stores, as the thing a future store must not reach for.
- **Modifying `saveToFileAtomic` or `writeDocToFileAtomic`.** Issue constraint.
  Both are correct as written.
- **Adding a format version to the four files.** The storage discipline in
  `CLAUDE.md` requires one for stores *this project introduces*; these four ship
  today on every device in the field with no version key (`grep FORMAT_VERSION`
  over all four `.cpp` files returns nothing). A version a future build "refuses
  rather than reinterprets" added now would refuse every existing file on the
  first OTA. Versioning these needs a migration design, which is its own issue.
- **`src/util/BookmarkFile.cpp`** — issue constraint, separate task.
- **`lib/I18n/translations/*.yaml`** — issue constraint, separate task. This is
  load-bearing: see **A6**.
- **Bounding the strings that feed `RecentBooksStore`.** See **A5**.
- **Enforcing `MAX_PASSWORD_LENGTH` on the write path.** See **A4**.

---

## Architecture

### 1. Convert the call sites, all 56 of them

`grep -rn 'saveToFile()' --include='*.cpp' src | wc -l` → **55**, across 24 files.
The issue body lists 27; the research note has the full delta table. The largest
concentrations are `SettingsActivity.cpp` (8), `TextSettingsActivity.cpp` (7),
`WifiCredentialStore.cpp` (5), `StatusBarSettingsActivity.cpp` (5),
`RecentBooksStore.cpp` (4), `main.cpp` (4).

Each becomes `saveToFileAtomic()`. The signature is identical
(`PersistableStore.h:126` vs `:143`, both `bool ... () const`), so no call site
changes shape.

**The 56th is inside the base class.** `loadFromFile()` calls `saveToFile()` at
`PersistableStore.h:173` to persist a legacy-shape upgrade detected by
`fromJson`. That path is live for two of the four stores —
`CrossPointSettings.cpp:230` and `WifiCredentialStore.cpp:106` both call
`requestResave()` — and `WifiCredentialStore`'s case is a plaintext-password
upgrade, i.e. it rewrites credentials. Converting only the 55 external sites
would leave the one write that happens *while the file is known to be in an old
shape* on the non-atomic path. `PersistableStore.h:173` converts too.

**One call site is deleted rather than converted.** `CardBooks.cpp:59` reads
`if (RECENT_BOOKS.removeByPath(bookPath)) RECENT_BOOKS.saveToFile();`, but
`removeByPath` already persists (`RecentBooksStore.cpp:85`) and returns true only
when it removed something — so this is a second, unconditional write of the
document that was just saved. Today that is one redundant `writeFile`; converted,
it would be a redundant write-tmp + remove + rename, doubling the no-file window
on a file that was already correct. `CLAUDE.md`'s storage discipline says "Guard
redundant writes". Drop the trailing call; keep the `if`'s side effect.
(`RecentBooksActivity.cpp:72` is *not* this case — `pruneMissing()` does not
persist by design, `RecentBooksStore.h:55`.) That makes the count 54 converted,
1 deleted, plus the base class.

This is the repo's own prescription for this shape of change:
`.skills/refactor-for-review/SKILL.md` — "A signature or type change that ripples
to many call sites is its own PR: map every caller first, update them in one
topological pass, and land it separately, not as a rider on a feature." The
callers are mapped (research note); this branch is that PR; it carries nothing
else.

### 2. Why not a four-line wrapper — rejected, with the reason

The tempting alternative is to leave all 55 call sites alone and add to each
store:

```cpp
bool saveToFile() const { return saveToFileAtomic(); }   // NOT the design
```

`saveToFile()` is not virtual (`PersistableStore.h:126`), so this is name hiding.
It works for every `SETTINGS.saveToFile()`-style call, and **silently does not
work** for `PersistableStore.h:173`, where the base template calls its own
`saveToFile()` on `this` — precisely the resave path §1 identifies as the one
that matters most. A mechanism that covers 55 sites and quietly misses the
dangerous one is worse than no mechanism. Rejected. See **A1**.

### 3. Budgets, derived per store

Each store declares `static constexpr size_t SAVE_BUDGET`, picked up by
`saveBudget()` at `PersistableStore.h:111-117`.

**The constant must be reachable from `PersistableStore<T>` or it is inert.**
`requires { T::SAVE_BUDGET; }` (`:112`) is a soft test: a private, misspelled or
`.cpp`-local constant is not an error, the branch is simply not taken, and the
store silently gets 45,000. Putting it "next to the existing constants" lands it
in a `private:` section for two stores — `WifiCredentialStore.h:35-36` (private at
`:27`) and `RecentBooksStore.h:21` (private at `:18`) — which works today only
because of `friend class PersistableStore<…>` (`WifiCredentialStore.h:41`,
`RecentBooksStore.h:26`), a line nothing connects to the budget. Every store
therefore pins its budget with a `static_assert` (§Testing step 2), which is the
only thing that turns a silent 45,000 into a build error.

| Store | Real bound | Worst-case serialised | `SAVE_BUDGET` | Headroom |
|---|---|---|---|---|
| `CrossPointState` | fixed key set; 2 path strings assumed ≤ 255 B | ~1,180 B | **2048** | 1.7× |
| `CrossPointSettings` | fixed shape | ~1,600 B | **4096** | 2.5× |
| `WifiCredentialStore` | 8 networks | ~1,800 B | **8192** | 4.5× |
| `RecentBooksStore` | 10 entries, unbounded strings | — | `persist::DEFAULT_SAVE_BUDGET` | — |

Derivations, from the serialising code rather than from a guess:

- **`CrossPointState`** (`CrossPointState.cpp:43-57`): 11 keys (196 characters of
  key name), two 16-element `uint16_t` arrays (`CrossPointState.h:14,22,25`), two
  path strings, seven scalars. The key set is fixed but `openEpubPath`
  (`CrossPointState.h:16`) and `bibleCoverPath` (`:21`) are unbounded
  `std::string`s — the same property **A5** uses to refuse a tight budget
  elsewhere. **The assumption is 255 B each**, giving ~1,180 B; 2048 refuses only
  at a combined path length near 1,500 characters, which this device's directory
  layout cannot reach. Stated so it is attackable rather than buried.
- **`CrossPointSettings`** (`CrossPointSettings.cpp:63-105`): the generic loop
  writes one key per `SettingsList.h` row. `grep -oE '"[A-Za-z][A-Za-z0-9_]*"'
  src/SettingsList.h | sort -u` yields 53 names / 898 characters, which is an
  **upper bound, not a derived count**: three of them (`"fontFamily"`,
  `"fontSize"`, `"longPressMenuFunction"`) are dynamic entries the loop skips
  (`CrossPointSettings.cpp:69`) and the tail writes by hand, so they are counted
  twice. The 9 tail keys total 127 characters. Both errors inflate the estimate,
  which is why 4096 remains ≥ 2.4× the real worst case. Values are almost all
  single bytes; the only sizeable fields are `downloadFolder[64]`
  (`CrossPointSettings.h:274`) and `sdFontFamilyName[32]` (`:293`).
- **`WifiCredentialStore`** (`WifiCredentialStore.cpp:9-24`): `MAX_NETWORKS = 8`
  (`WifiCredentialStore.h:35`) × (`ssid` ≤ 32 B by 802.11, `password_obf`,
  `password_len`, `password_crc32`), plus `lastConnectedSsid`. The 4.5× headroom
  rather than 2.5× is deliberate — see **A4**.
- **`RecentBooksStore`** (`RecentBooksStore.cpp:11-20`): `MAX_RECENT_BOOKS = 10`
  (`RecentBooksStore.h:21`) is enforced (`RecentBooksStore.cpp:57-59`), but
  `title` and `author` are `std::string` copied straight from EPUB metadata
  (`BookMetadataCache.h:15-16` → `EpubReaderActivity.h:147` →
  `ReaderActivity.cpp:60`) with no truncation anywhere on the path. There is no
  honest worst case to derive. See **A5**.

**This would be the first `SAVE_BUDGET` declaration in the tree.** `grep -rn
SAVE_BUDGET src lib` finds only the machinery at `PersistableStore.h:109-117`;
the free-function modules use a differently named constant
(`HighlightFile.h:42`, `PassageDoc.h:26`). The `if constexpr (requires {
T::SAVE_BUDGET; })` branch at `PersistableStore.h:112` has never been compiled
with the branch taken, so the build in §Testing is what first exercises it.

### 4. What is not modelled on anything

There is no in-repo precedent for a `PersistableStore` subclass calling
`saveToFileAtomic()` — this change creates the first one. The nearest example is
the free-function form, and the four stores should read like it minus the parts
the base class already does:

```cpp
// src/study/TagPaletteFile.cpp:70-76 — the pattern, in its free-function form
if (measureJson(json) > persist::DEFAULT_SAVE_BUDGET) {
  LOG_ERR(MODULE, "Tag palette exceeds the save budget; not written");
  return SaveResult::TooLarge;
}
Storage.mkdir(BEREAN_DIR);
return PersistableStoreBase::writeDocToFileAtomic(PATH, json) ? SaveResult::Ok : SaveResult::WriteFailed;
```

`saveToFileAtomic` is exactly this, with `T::getFilePath()` for `PATH` and
`saveBudget()` for the constant. Same at `PubKeyRegistry.cpp:36-42`,
`MeetingWeekCache.cpp:50`, `MigrationRunner.cpp:119`, `PassageFile.cpp:106`. No
new *call* pattern is introduced — but the change does relax one documented
invariant, quantified in §Data and control flow below.

---

## Data and control flow

Unchanged above the store boundary. Below it:

```
before:  caller → saveToFile()      → storeMutex → toJson → writeDocToFile
                                                             ├ Storage.mkdir("/.crosspoint")
                                                             └ Storage.writeFile(path)
                                                               ├ vol().remove(path)   ← real file gone first
                                                               ├ open(O_CREAT|O_TRUNC)
                                                               └ print(whole document) ← no-file window spans the WHOLE write

after:   caller → saveToFileAtomic() → storeMutex → toJson → measureJson
                                                           ├ over budget? LOG_ERR, return false (nothing written)
                                                           └ writeDocToFileAtomic
                                                             ├ Storage.mkdir("/.crosspoint")
                                                             ├ Storage.writeFile(path + ".tmp") ← tears here instead
                                                             ├ Storage.remove(path)
                                                             └ Storage.rename(tmp → path)  ← no-file window is now 2 dir updates
```

Each `Storage.*` call takes `storageMutex` independently
(`HalStorage.cpp:56,89,93,94`), so the cost delta per save is **two extra
directory operations**, not two extra file writes. `storeMutex` is held across
all of them (`PersistableStore.h:144`), as it already is for the legacy path
(`:127`).

**All four paths are under `/.crosspoint/`**, so `writeDocToFileAtomic`'s
hardcoded `Storage.mkdir("/.crosspoint")` (`PersistableStore.cpp:23`) is correct
for all four — verified against `CrossPointSettings.h:386`, `CrossPointState.h:32`,
`WifiCredentialStore.h:44`, `RecentBooksStore.h:29`.

**Frequency.** No converted call site is on a per-page-turn path. The issue's
premise that `EpubReaderActivity.cpp:151` is a progress save is wrong: `:151` is
inside `moveFinishedBookToReadFolder` (`:133-153`) and fires once per finished
book. Reading progress never touches a `PersistableStore` — it is a 10-byte
binary record through `ProgressFile::writeAtomic` (`ProgressFile.h:32`, called
from `EpubReaderUtils.h:35`), already atomic. The remaining `CrossPointState`
writes are once-per-transition (`ReaderActivity.cpp:59,69`, `main.cpp:263,521,577`,
`SleepActivity.cpp:446`, `PublicationDownloader.cpp:123`), and
`LauncherActivity.cpp:107` is already change-guarded. **No debounce is part of
this change.** See **A2**.

**Flash: +3,824 bytes of `.text`, and it relaxes a documented invariant.**
`saveToFileAtomic` calls `measureJson(doc)` *in the header*
(`PersistableStore.h:148`). It is a member of a class template, so it instantiates
in every TU that calls it, pulling ArduinoJson's serializer into 23 TUs that do
not have it today (of the 24 call-site files, only
`src/network/CrossPointWebServer.cpp` already instantiates it). That is the exact
thing `PersistableStoreBase` exists to prevent, per its own comment at
`PersistableStore.h:16-20`: "GCC emits the JSON serializer/parser templates as
local `.isra` clones per TU (~0.5KB each), so keeping `serializeJson`/`deserializeJson`
out of the stores is what makes the abstraction flash-neutral." `saveToFile()`
honours it — it calls only the out-of-line `writeDocToFile`.

Measured on the target toolchain (`xtensa-esp-elf-g++` 14.2.0, `-Os`,
`-fno-exceptions -fno-rtti -ffunction-sections`, ArduinoJson 7.4.2 per
`platformio.ini:151`), partial-linked at 24 TUs so COMDAT folding applies: 4,974 B
`.text` for 24 legacy TUs versus 8,798 B for 24 atomic ones. The mechanism is
~1,662 B one-time for `JsonSerializer<DummyWriter>` (weak, folded once) plus ~94 B
per additional calling TU (an `.isra` local clone that does not fold). **+3,824 B
is 0.06 % of the 6,553,600 B app partition** (`partitions.csv:4-5`) — affordable,
and stated rather than assumed away. §Testing step 5 checks the real link against
this prediction.

**The `storeMutex` hold roughly doubles.** Nothing waits on it: the read paths
deliberately do not take it, which is what `PersistableStore.h:31-36` and
`CrossPointSettings.h:348-354` exist to protect. The `CrossPointSettings.h:354`
warning the issue flags is about not locking `storeMutex` on the *render* path;
`saveToFileAtomic` locks in the same place `saveToFile` does (`:144` vs `:127`),
so the comment's *argument* is untouched and no mutex is added anywhere. Four
comments name `saveToFile()` and go stale; the change fixes all four:

| Comment | What is wrong after the change |
|---|---|
| `CrossPointSettings.h:354` | names `saveToFile()`; also should say the hold is now four `storageMutex` acquisitions, not two |
| `PersistableStore.h:172` | "`saveToFile()` takes storeMutex itself" sits directly above the line §1 converts |
| `PersistableStore.h:41-42`, `:96` | the deadlock warning is still true but must name `saveToFileAtomic()`, which takes the same lock at `:144` |
| `WifiCredentialStore.h:32` | "`saveToFile()` snapshots under this mutex from `toJson()`" — true in substance, wrong by name |

---

## Error handling

- **Over budget ≠ write failed.** `saveToFileAtomic` returns `false` for both
  (`PersistableStore.h:152` and `:154`). Callers cannot tell them apart; the
  serial log can (`PERSIST` "Refusing to save %s: %u bytes exceeds budget %u" at
  `:150-151` vs "Failed to write temp file" / "Failed to rename" at
  `PersistableStore.cpp:31,40`). Not fixed here — fixing it means changing
  `saveToFileAtomic`'s return type, which the issue forbids. See **A3**.
- **A refusal is never silent, at two levels.** `saveToFileAtomic` logs the
  refusal itself before returning (`PersistableStore.h:150`), so no site can
  swallow it. That log names a file and two numbers and not the *operation* that
  was lost, so the three sites acceptance criterion 3 names by line also get a
  local `LOG_ERR` naming what was lost — see **A6a**.
- **A refusal writes nothing.** The `return false` at `PersistableStore.h:152`
  precedes `writeDocToFileAtomic` entirely — no temp file, no `remove`, no
  `rename`. The previous good file survives untouched. This is the property the
  whole change exists for.
- **A torn write costs the temp file only.** An interrupted
  `Storage.writeFile(tmp)` leaves `<path>.tmp` damaged and `<path>` intact.
- **The no-file window shrinks; it is not introduced.** The legacy path already
  deletes the real file before writing a byte: `SDCardManager::writeFile` calls
  `vol().remove(path)` at `SDCardManager.cpp:282-284` and then opens
  `O_RDWR | O_CREAT | O_TRUNC` (`:337`). So today the unprotected interval spans
  the entire serialise-and-write; after this change it is the two directory
  updates between `Storage.remove` and `Storage.rename`
  (`PersistableStore.cpp:38-39`). A power loss in either window reads as "no data
  yet" and initialises defaults, which `PersistableStore.cpp:35-37` argues is
  recoverable where a torn file is not. This change is an unambiguous
  improvement on that axis, not a trade.
- **A leftover `.tmp` is hygiene, not a hazard.** `O_TRUNC`
  (`SDCardManager.cpp:337`) means an interrupted previous save's
  `settings.json.tmp` is truncated on the next write rather than appended to.
- **The three sites that propagate keep propagating.** `RecentBooksStore.cpp:85`
  logs; `WifiCredentialStore.cpp:134,149` return the bool to callers that already
  return `false` for other reasons (`:124-126`, `:142-143`). The UI callers
  (`WifiSelectionActivity.cpp:67,691`) discard it today and continue to.
- **One converted path already reports a refusal to a human, and reports it
  wrongly.** `CrossPointWebServer::handlePostWifiNetwork` calls `addCredential`
  at `:1406,1408,1418` and answers `400 "Cannot add network (limit reached)"`
  (`:1419-1421`) or `400 "Failed to update Wi-Fi network"` (`:1412`). After this
  change a budget refusal reaches `:1419` and is reported as a limit that was not
  reached. **Fix it in this change**: widen `:1419` to `"Cannot add network"`.
  One line, in a file this change already converts (`:1320`).

---

## Assumptions

Every behavioural decision in this spec, stated so it can be attacked. **A5** and
**A6** are the two that contradict the issue body.

**A1 — Convert call sites; do not hide the base method in the derived stores.**
*Why:* name hiding misses `PersistableStore.h:173`, the resave-after-legacy-upgrade
write, which for `WifiCredentialStore.cpp:106` is a credentials rewrite.
*Attack it:* the cost is a 29-file diff for a 2-line intent. If the reviewer
prefers the wrapper plus an explicit fix at `:173`, that is a smaller diff with a
sharper trap.

**A2 — No debounce, no write-coalescing, anywhere.**
*Why:* the hot-path premise is false (see Data and control flow). The most
frequent converted site is a settings row activation
(`TextSettingsActivity.cpp:337,345`, `StatusBarSettingsActivity.cpp:161-203`),
which is one user button press and already costs an e-ink refresh.
*Attack it:* if two extra directory ops per settings keypress is measurably felt
on the panel, this assumption is wrong and the answer is a debounce, not a
smaller budget.

**A3 — "Too large" and "write failed" stay indistinguishable to callers.**
*Why:* separating them means changing `saveToFileAtomic`'s return type, which the
issue forbids. The free-function modules do distinguish them
(`TagPaletteFile.h:21`, `enum class SaveResult { Ok, TooLarge, WriteFailed }`, returned at `TagPaletteFile.cpp:72,76`), so
the asymmetry is real and known.
*Attack it:* if a caller will ever need to act differently on the two, this is
the cheap moment to say so.

**A4 — `WifiCredentialStore` gets 8192, on the strength of the load-path bound.**
*Why:* three producers, not one. The **UI caps at 64**:
`WifiSelectionActivity.cpp:352` passes `64,  // Max password length` to
`KeyboardEntryActivity`, and `32` for a hidden SSID at `:375`, both enforced at
`KeyboardEntryActivity.cpp:250` (`if (maxLength != 0 && text.length() + n > maxLength) return;`).
The **web server does not cap at all**: `CrossPointWebServer.cpp:1376` checks only
that the SSID is non-empty, `:1385` takes `password` unbounded, and `:1406,1408,1418`
pass it to `addCredential`, which does not check either (`WifiCredentialStore.cpp:112`).
The **load path bounds the steady state**: `fromJson` discards any credential whose
`password_len > MAX_PASSWORD_LENGTH` and requests a resave
(`WifiCredentialStore.cpp:52-56`), so an oversized web-written password survives
only until the next boot. The on-disk worst case is therefore computable —
8 × (ssid ≤ 32 + base64(64) ≈ 88 + `password_len` + `password_crc32` + ~45 B of key
name + ~25 B of punctuation) + `lastConnectedSsid` ≈ 1.7 KB — and 8192 is ~4.5×
it, with the headroom absorbing a web-written password between its write and the
next boot.
*Attack it:* the durable fix is bounding `password` at
`CrossPointWebServer.cpp:1385` to `MAX_PASSWORD_LENGTH`, after which 4096 would do.
That is an API behaviour change (it rejects a payload the endpoint accepts today)
and is out of scope here.
*Corrected after review pass 0* — the original A4 claimed no UI cap existed and
never opened the web server. See **What changed after review pass 0**.

**A5 — `RecentBooksStore` keeps `persist::DEFAULT_SAVE_BUDGET`. This contradicts the issue.**
*Why:* acceptance criterion 2 names it as one of "the two that most need a
deliberate figure". Its entry count is capped at 10
(`RecentBooksStore.cpp:57-59`) but `title` and `author` are unbounded
`std::string`s from EPUB metadata (`BookMetadataCache.h:15-16`,
`ReaderActivity.cpp:60`). A "deliberate figure" here is a figure that can refuse
a legitimate save *in the field*, on a real user's long-titled book, where the
only symptom is that recents stops updating and a `PERSIST` line lands on a
serial port nobody is watching. Criterion 2's own escape hatch — "or documents in
a comment why `persist::DEFAULT_SAVE_BUDGET` is the right ceiling for it" —
applies, and the comment should say exactly this.
*Attack it:* the real fix is truncating `title`/`author` at `addBook`
(`RecentBooksStore.cpp:42`), after which a tight budget is safe. That is a
designed field-length decision — the repo's precedent is `PassageDoc.h:26-28`,
which sets `MAX_SNIPPET_BYTES = 120` and `MAX_REFERENCE_BYTES = 48` alongside its
budget — not a rider on this change.
*Accepted at review pass 0, with two conditions:* the comment on
`RecentBooksStore`'s budget must state the concrete worst case it accepts (10
entries × 4 unbounded strings), and the truncation follow-up must be **filed as an
issue**, not left as a sentence here.

**A6a — the three call sites acceptance criterion 3 names by line get a local `LOG_ERR`.**
*Why:* criterion 3 does not say "a refusal must be logged somewhere"; it says the
sites that discard the return value "either handle a false or `LOG_ERR` on it",
and it names `RecentBooksStore.cpp:62,74,102`. The base-class log
(`PersistableStore.h:150`) emits a path and two numbers with no indication of
*which operation* was lost — a book add at `:62`, a metadata update at `:74`, a
path repoint at `:102`. The store already shows the pattern four lines below, at
`:85-87`. Costs ~6 lines, needs no string and no i18n.
*Attack it:* if the base-class log is judged sufficient, this is 6 wasted lines —
but the criterion names these three lines specifically, so declining it is
narrowing the issue rather than interpreting it.
*Added at review pass 0* — the original A6 declined this along with the popup.

**A6b — no user-visible popup. This narrows the issue, deliberately.**
*Why:* going further than a log needs a string, `tr()` is mandatory for
user-facing text (`CLAUDE.md`), and **this branch is forbidden from touching
`lib/I18n/translations/*.yaml`**. The mechanism exists (`GUI.drawPopup` is used
outside the reader at `BmpViewerActivity.cpp:229-231`; the reader wraps it as
`ReaderUtils::showMessage`, `ReaderUtils.h:233`, and uses it for exactly this at
`TagFilterActivity.cpp:124` and `PassageSelectActivity.cpp:400`), but the string
does not.
*Attack it:* the only existing key that would fit is `STR_FAILED_LOWER`
("failed", `english.yaml:134`), already used bare at `BmpViewerActivity.cpp:231`.
Reusing it for a settings-save refusal is worse than a log line. The one path that
*does* reach a human is the web server, and it is fixed in §Error handling rather
than by a popup. UI surfacing is filed separately (it spans #27 and #28).

**A7 — `PersistableStore.h:173` converts, changing behaviour for all four stores at once.**
*Why:* it is the only `saveToFile()` caller that survives §1, and it writes on the
legacy-upgrade path. All four subclasses are converting anyway, and there are
exactly four (`grep -rn "public PersistableStore<"` → 4 hits), so nothing else
can be affected.
*Attack it:* this edits the base template, which the issue's "call-site change"
framing did not anticipate. It is still not a change to `saveToFileAtomic` or
`writeDocToFileAtomic`, which are what the constraints name.

**A8 — `saveToFile()` survives with zero store callers and no deprecation marker.**
*Why:* the issue forbids deleting it, and `[[deprecated]]` with `-Werror`-adjacent
settings is a build risk not worth taking here. The comment at
`PersistableStore.h:124-125` should be corrected, though: after this change it is
not "kept for the stores that already use it" — no store uses it.
*Attack it:* if the reviewer wants a gate that keeps it at zero callers, the
repo's precedent is a grep script beside `scripts/i18n_orphans.sh` and
`scripts/settings_snapshot.py`, both introduced for exactly this reason (the host
suite cannot reach these files —
`docs/superpowers/notes/phase-0-baseline.md`). Deliberately **not** proposed as
part of this change; it is scope the issue did not ask for.

---

## Testing

**No host test can cover this change.** `PersistableStore.h:3` includes
`<Arduino.h>` unconditionally, so nothing that instantiates a `PersistableStore`
subclass is host-buildable. The repo already records this where it bit:
`test/highlight_file/CMakeLists.txt:1-4` — "HighlightFile.cpp itself pulls
Arduino.h transitively through PersistableStore.h and cannot be built here."

What the host suite already covers is the arithmetic:
`test/save_budget/SaveBudgetTest.cpp` has five cases including
`HonoursAPerStoreBudget` (`:32-36`) and
`DefaultBudgetLeavesHeadroomUnderTheReadCap` (`:28-30`). That is the whole of
what is reachable, and it needs no additions — this change adds no new pure
logic. A spec that promised a new test here would be promising something the
build cannot deliver.

So the verification ladder is short and honest:

1. **`~/.platformio/penv/bin/pio run`** — once, after the last edit. This is the
   first compile of the `if constexpr (requires { T::SAVE_BUDGET; })` branch at
   `PersistableStore.h:112` with the branch taken, so a build failure there is a
   real finding, not noise.
2. **The four `static_assert`s compile.** This is the only check that can catch a
   `SAVE_BUDGET` that is silently inert (§3). One per store, at namespace scope in
   the store's `.cpp` where the class is complete:

   ```cpp
   static_assert(CrossPointState::saveBudget() == 2048,
                 "SAVE_BUDGET is not reaching saveBudget() -- check access and spelling");
   ```

   `saveBudget()` is public, so this compiles even where `SAVE_BUDGET` is private.
   `CrossPointSettings` asserts `4096`, `WifiCredentialStore` `8192`, and
   `RecentBooksStore` asserts `== persist::DEFAULT_SAVE_BUDGET` so **A5**'s
   decision is pinned rather than assumed.
3. **`PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix`** over the whole tree,
   not `-g` — CI formats everything (`CLAUDE.md`), and `-g` skips files that are
   newly committed rather than modified.
4. **`grep -rn 'saveToFile()' src`** — note: no `--include`, because two of the
   remaining occurrences are in headers. Expected output is exactly the corrected
   comments enumerated in §Data and control flow, and no call.
5. **Flash delta against `main`.** Record the `firmware.bin` size before and
   after. The prediction is **+3,824 B** (§Data and control flow); anything past
   ~6 KB means COMDAT folding did not hold in the real link and the mechanism
   needs re-examining before merge.

**Device-only, and the human tester's to run** — flag these rather than claim
them:

6. Change a setting, reboot, confirm it persisted. Repeat for a WiFi credential
   and for the open-book path.
7. **The power-pull test.** Pull power during a settings save; confirm the device
   boots with its settings intact rather than at defaults. This is the one test
   that actually exercises what the change is for, and nothing on the host or in
   CI substitutes for it.
8. `ESP.getFreeHeap()` unchanged across a settings save. Expected: the atomic path
   allocates two `std::string`s for the paths (`PersistableStore.cpp:24-25`) that
   the legacy path does not, both freed on return.
9. Confirm no stray `*.json.tmp` files accumulate under `/.crosspoint/` after
   normal use. This is hygiene, not correctness: `O_TRUNC`
   (`SDCardManager.cpp:337`) means a stale `.tmp` is overwritten, never appended
   to. A leftover means a `rename` failed and was logged rather than retried.

---

## Risks

- **A 29-file diff for a two-line intent** (24 call-site files + 4 store headers +
  `PersistableStore.h`). Mitigated by it being mechanical and by check 3 above,
  but it is the sprawl shape `.skills/refactor-for-review/SKILL.md` warns about.
  It is justified only because the ripple *is* the change, and it carries nothing
  else.
- **First use of an untested branch.** `SAVE_BUDGET` has never been declared on a
  store; `saveToFileAtomic` has never been called. Both are simple and both are
  now in the boot path of every device.
- **A budget set wrong is a silent field failure**, not a crash: the store stops
  persisting and logs to a serial port nobody is watching. This is why **A5**
  refuses to invent a tight figure for the one store whose growth is user-driven.
- **A no-file window remains** (`PersistableStore.cpp:38-39`), two directory
  updates wide. It is not new and it is much smaller than the legacy path's, which
  spans the whole write (`SDCardManager.cpp:282-284,337`) — but this change
  narrows that window rather than closing it.
- **+3,824 B of flash**, and the `PersistableStore.h:16-20` "flash-neutral"
  invariant is knowingly relaxed. Measured, affordable, and re-checked by
  §Testing step 5 against the real link.

## Open questions — both closed at review pass 0

1. **A5 — `RecentBooksStore` keeps `DEFAULT_SAVE_BUDGET`: ACCEPTED.** Acceptance
   criterion 2 carries its own escape hatch ("or documents in a comment why
   `persist::DEFAULT_SAVE_BUDGET` … is the right ceiling for it") and A5's reason
   is exactly the documentation it asks for. Truncation is **not** folded in; the
   repo's precedent (`PassageDoc.h:26-28`) shows field-length caps are a designed
   decision, not a rider. Conditions attached: the comment states the worst case
   it accepts, and the truncation follow-up is filed as an issue.
2. **A6 — YES for the popup, NO as originally written.** The popup stays deferred
   (**A6b**); the i18n constraint is not lifted. But the three sites criterion 3
   names by line get their local `LOG_ERR` (**A6a**) and the misleading HTTP 400 at
   `CrossPointWebServer.cpp:1419` is fixed (§Error handling). With those two,
   criterion 3 is met literally rather than by reinterpretation.

## Environment note for the implementer

The worktree builds. The one fact that still bites: `~/.platformio/penv/bin/pio`
is the pio entry point — the bare `pio` is not on `PATH` — and the format wrapper
needs `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix`, because
`bin/clang-format-fix:4-12,26-31` exits 1 rather than passing falsely when it
cannot find a clang-format 21.
