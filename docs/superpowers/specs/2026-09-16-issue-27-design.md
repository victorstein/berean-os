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

Each store declares `static constexpr size_t SAVE_BUDGET` next to its existing
constants, picked up by `saveBudget()` at `PersistableStore.h:111-117`.

| Store | Real bound | Worst-case serialised | `SAVE_BUDGET` | Headroom |
|---|---|---|---|---|
| `CrossPointState` | fixed shape | ~1,180 B | **2048** | 1.7× |
| `CrossPointSettings` | fixed shape | ~1,600 B | **4096** | 2.5× |
| `WifiCredentialStore` | 8 networks | ~1,800 B | **8192** | 4.5× |
| `RecentBooksStore` | 10 entries, unbounded strings | — | `persist::DEFAULT_SAVE_BUDGET` | — |

Derivations, from the serialising code rather than from a guess:

- **`CrossPointState`** (`CrossPointState.cpp:43-57`): 11 keys (196 characters of
  key name), two 16-element `uint16_t` arrays (`CrossPointState.h:14,22,25`), two
  path strings, seven scalars. ~1,180 B with both paths at a generous 255.
- **`CrossPointSettings`** (`CrossPointSettings.cpp:63-105`): the generic loop
  writes one key per `SettingsList.h` row — 53 distinct keys, 898 characters of
  key name — plus 9 hand-written keys (136 characters) at the tail. Values are
  almost all single bytes; the only sizeable fields are `downloadFolder[64]`
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
new pattern is introduced by this change.

---

## Data and control flow

Unchanged above the store boundary. Below it:

```
before:  caller → saveToFile()      → storeMutex → toJson → writeDocToFile
                                                             ├ Storage.mkdir("/.crosspoint")
                                                             └ Storage.writeFile(path)          ← tears here

after:   caller → saveToFileAtomic() → storeMutex → toJson → measureJson
                                                           ├ over budget? LOG_ERR, return false (nothing written)
                                                           └ writeDocToFileAtomic
                                                             ├ Storage.mkdir("/.crosspoint")
                                                             ├ Storage.writeFile(path + ".tmp") ← tears here instead
                                                             ├ Storage.remove(path)
                                                             └ Storage.rename(tmp → path)
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

**The `storeMutex` hold roughly doubles.** Nothing waits on it: the read paths
deliberately do not take it, which is what `PersistableStore.h:31-36` and
`CrossPointSettings.h:348-354` exist to protect. The `CrossPointSettings.h:354`
warning the issue flags is about not locking `storeMutex` on the *render* path;
`saveToFileAtomic` locks in the same place `saveToFile` does (`:144` vs `:127`),
so the comment's *argument* is untouched and no mutex is added anywhere. Two
comment edits follow from the change and nothing else does: `CrossPointSettings.h:354`
names `saveToFile()` by name and should name `saveToFileAtomic()`, and both it and
`PersistableStore.h:31-36` should say that the hold is now four `storageMutex`
acquisitions rather than two — the margin for a future reader that takes the lock
narrows, and that is the fact a future author needs.

---

## Error handling

- **Over budget ≠ write failed.** `saveToFileAtomic` returns `false` for both
  (`PersistableStore.h:152` and `:154`). Callers cannot tell them apart; the
  serial log can (`PERSIST` "Refusing to save %s: %u bytes exceeds budget %u" at
  `:150-151` vs "Failed to write temp file" / "Failed to rename" at
  `PersistableStore.cpp:31,40`). Not fixed here — fixing it means changing
  `saveToFileAtomic`'s return type, which the issue forbids. See **A3**.
- **A refusal is never silent.** `saveToFileAtomic` logs it itself before
  returning (`PersistableStore.h:150`). This is true at all 56 sites without any
  of them doing anything, which is the substance of acceptance criterion 3. See
  **A6** for why it stops there.
- **A refusal writes nothing.** The `return false` at `PersistableStore.h:152`
  precedes `writeDocToFileAtomic` entirely — no temp file, no `remove`, no
  `rename`. The previous good file survives untouched. This is the property the
  whole change exists for.
- **A torn write costs the temp file only.** An interrupted
  `Storage.writeFile(tmp)` leaves `<path>.tmp` damaged and `<path>` intact.
- **The window where neither file exists is real and accepted.** Between
  `Storage.remove` and `Storage.rename` (`PersistableStore.cpp:38-39`) a power
  loss leaves no file, which reads as "no data yet" on next boot and initialises
  defaults. The comment at `:35-37` argues this is recoverable where a torn file
  is not; that reasoning is inherited, not re-litigated here. It does mean the
  change reduces the tear window rather than eliminating all loss.
- **The three sites that propagate keep propagating.** `RecentBooksStore.cpp:85`
  logs; `WifiCredentialStore.cpp:134,149` return the bool to callers that already
  return `false` for other reasons (`:124-126`, `:142-143`). Those callers
  (`WifiSelectionActivity.cpp:67,691`) discard it today and continue to. Not
  widened here — see **A6**.

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

**A4 — `WifiCredentialStore` gets 8192, not a tight figure from `MAX_PASSWORD_LENGTH`.**
*Why:* `MAX_PASSWORD_LENGTH = 64` (`WifiCredentialStore.h:36`) is enforced only on
the load path (`WifiCredentialStore.cpp:52,60`). `addCredential` (`:112`) does not
check it, and no cap on `enteredPassword` (`WifiSelectionActivity.h:77`) was
found. 64 is what the store *intends*, not what it *enforces*, so a budget derived
from it would refuse a save the UI happily produced.
*Attack it:* the alternative is to enforce the cap at `addCredential` and then
budget tightly. That is a behaviour change — it rejects a password the user typed
— and is out of scope here, but it is the correct long-term fix.

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
behaviour change and a separate issue. If the reviewer wants it folded in, say so
now.

**A6 — A budget refusal is reported by `LOG_ERR` only; no user-visible message. This narrows the issue.**
*Why:* acceptance criterion 3 says a refusal "must not become a silent no-op".
`saveToFileAtomic` already logs every refusal at `PersistableStore.h:150`, so no
refusal is silent. Going further — a popup — needs a string, and
`tr()` is mandatory for user-facing text (`CLAUDE.md`), and **this branch is
forbidden from touching `lib/I18n/translations/*.yaml`**. The mechanism exists
(`GUI.drawPopup` is used outside the reader at `BmpViewerActivity.cpp:229-231`;
the reader wraps it as `ReaderUtils::showMessage`, `ReaderUtils.h:233`, and uses
it for exactly this at `TagFilterActivity.cpp:124` and
`PassageSelectActivity.cpp:400`), but the string does not.
*Attack it:* the only existing key that would fit is `STR_FAILED_LOWER`
("failed", `english.yaml:134`), already used bare at `BmpViewerActivity.cpp:231`.
Reusing it for a settings-save refusal is worse than a log line. If a popup is
required, this issue has to be unblocked from the i18n constraint first.

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
`test/save_budget/SaveBudgetTest.cpp` has six cases including
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
2. **`PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix`** over the whole tree,
   not `-g` — CI formats everything (`CLAUDE.md`), and `-g` skips files that are
   newly committed rather than modified.
3. **`grep -rn 'saveToFile()' --include='*.cpp' src`** must return nothing. This
   is the completeness check for §1 and costs one command.

**Device-only, and the human tester's to run** — flag these rather than claim
them:

4. Change a setting, reboot, confirm it persisted. Repeat for a WiFi credential
   and for the open-book path.
5. **The power-pull test.** Pull power during a settings save; confirm the device
   boots with its settings intact rather than at defaults. This is the one test
   that actually exercises what the change is for, and nothing on the host or in
   CI substitutes for it.
6. `ESP.getFreeHeap()` unchanged across a settings save. Expected: the atomic path
   allocates two `std::string`s for the paths (`PersistableStore.cpp:24-25`) that
   the legacy path does not, both freed on return.
7. Confirm no stray `*.json.tmp` files accumulate under `/.crosspoint/` after
   normal use — a leftover means a `rename` failed and was logged rather than
   retried.

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
- **`remove`-then-`rename` has a no-file window** (`PersistableStore.cpp:38-39`).
  The change narrows the loss window; it does not close it.

## Open questions

Both are for the review, not for the implementer:

1. **A5** — accept `DEFAULT_SAVE_BUDGET` for `RecentBooksStore` with a comment, or
   fold in `title`/`author` truncation at `addBook` and budget tightly?
2. **A6** — is `LOG_ERR` sufficient for "not a silent no-op", or does this issue
   need unblocking from the i18n constraint so a refusal can reach the screen?

## Environment note for the implementer

This worktree cannot build yet. `git submodule status` shows `freeink-sdk`
uninitialised (leading `-`), and there is no `.venv`, so
`./bin/clang-format-fix` exits 1 at its own `:4-12`. Both are bootstrap, not
code. `~/.platformio/penv/bin/pio` is present (PlatformIO Core 6.1.19); the bare
`pio` is not on `PATH`.
