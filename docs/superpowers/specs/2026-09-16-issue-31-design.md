# Strip the unused translation keys

**Date:** 2026-09-16
**Status:** Design v1
**Target:** bereanOS, `x4pro` build target (ESP32-S3, 8 MB PSRAM, 800x480 e-ink)
**Delivery:** `origin` (victorstein/berean-os), branch `refactor/31-unused-i18n-keys`
**Closes:** #31
**Builds on:** `docs/superpowers/research/2026-09-16-issue-31-research.md`
**Modelled on:** `docs/superpowers/specs/2026-09-16-issue-38-design.md` — the
nearest existing example: the other pure-deletion spec on this issue series, with
a Problem / Goal / Non-goals / Assumptions / Architecture / Flow / Error handling
/ Testing shape, and the only other spec that reasons about the i18n gates. This
one keeps that shape and that section order. Where #38's non-goals say
"`lib/I18n/translations/*.yaml` — a separate task owns the translation files"
(`2026-09-16-issue-38-design.md:77-78`), **this is that task.**

## Problem

`scripts/gen_i18n.py` reports on every build that `english.yaml` defines keys no
source file references:

```
$ python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --verbose
  Total: 443  |  Used in code: 420  |  Never used: 23
```

Most are OPDS, Calibre and general-reader leftovers that the Phase 0 strip
(`docs/superpowers/plans/2026-09-13-phase-0-fork-and-strip.md:731,844`) did not
finish. They sit in front of translators: `docs/i18n.md:254-260` tells a
translator to open their language file and "add or update translations" for the
keys in it, with nothing marking which keys are wanted.

**The cost is translator attention and nothing else.** `platformio.ini:131`
registers the generator as `pre:scripts/gen_i18n.py`; imported by SCons it takes
the `else` branch and calls `main(strip_unused=True)` (`gen_i18n.py:1001`), so
every firmware build already drops these keys before generating. Observed in the
baseline build:

```
  Stripping 23 unused string(s) from output.
Code generation complete!
  Languages: 32
  String keys: 420
```

So the firmware is already clean and **this change cannot save a byte of flash**.
A spec, plan or PR description that promises a size win is wrong.

The issue body is stale in four ways, all established in the research note and
none of them cosmetic:

1. **The count is 23, not 22** (research §2). The worktree arrived based on
   `ded48d17`, which predates `4a107d5c` ("remove Show page as QR"); that commit
   orphaned `STR_DISPLAY_QR`. The branch was fast-forwarded to `c48e3ec7` before
   anything was measured.
2. **`STR_HIGHLIGHTS_TOO_LARGE` must stay** (research §4), so the removal set is
   22 — and acceptance criterion 5 ("zero unused keys") becomes unsatisfiable.
3. **"Carried in all 32 languages" holds for seven of the keys** (research §5).
   Five are in 28 files, two in 27, `STR_HIGHLIGHTS_TOO_LARGE` in one.
4. **`docs/translators.md` does not ask translators to keep keys current**
   (research §11) — it is 66 lines of contributor credits. `docs/i18n.md:254-260`
   is the instruction the issue means.

## Goal

Remove 22 unreferenced `STR_*` keys from every file under
`lib/I18n/translations/` that defines them — 666 lines across all 32 files —
while leaving the generated C++ **byte-identical** to what the build produces
today, and leaving `STR_HIGHLIGHTS_TOO_LARGE` in place for #39.

## Non-goals

- **Removing `STR_HIGHLIGHTS_TOO_LARGE`.** #39 asks #31 by name not to. See A2.
- **The 17 keys that exist only outside `english.yaml`.** Real dead weight, and
  invisible to the "unused" report. See A3.
- **Updating `AGENTS.md`'s `tr(STR_LOADING)` example** (`:336`, `:709`). See A4.
- **Fixing `scripts/i18n_orphans.sh`'s dependence on a generated file.** See A5.
- **Teaching `gen_i18n.py --strip-unused` to rewrite the YAML.** See A6.
- **Any `_language_name`, `_language_code` or `_order` line.** All three are
  present in 32/32 files today; criterion 4 is met by not touching them, and
  `_order` is load-bearing for a persisted setting (Architecture, "What cannot
  move").
- **`docs/i18n.md`, `docs/translators.md`, `lib/I18n/I18n.{h,cpp}`,
  `scripts/gen_i18n.py`, or anything under `src/` or `test/`.** The brief's
  "touch nothing outside `lib/I18n/translations/`" is kept, with A4's one
  acknowledged consequence.
- **Re-sorting, re-grouping or reformatting the YAML.** A pure line-delete keeps
  the diff reviewable; see A6.

---

## Assumptions

Every behavioural decision in this spec, stated so the review can attack it.

**A1 — The removal set is the 23 measured on `c48e3ec7`, minus one, not the 22
listed in the issue.** The issue's list was measured on `main` before `4a107d5c`
and `d043fd84` merged; the brief itself says "re-derive the list yourself". The
re-derivation (research §2, §7) found exactly one difference: `STR_DISPLAY_QR`
is now an orphan, which is what acceptance criterion 3 asks for. Three
independent checks back the set: no key is kept alive by a comment alone; none
of the 22 is referenced anywhere in `src/`, `lib/`, `test/`, `scripts/` or
`freeink-sdk/` **on `main`**; and no local branch *introduces* a new reference to
any of them. That third check needs stating precisely, because the raw grep is
not empty: eight branches still carry the pre-`4a107d5c` QR code and so reference
`STR_DISPLAY_QR` at `src/activities/reader/EpubReaderMenuActivity.cpp:77` and
`src/activities/reader/QrDisplayActivity.cpp:34` — among them
`feature/buscar-catalog-search`, `feature/publications-library`,
`fix/27-atomic-store-saves`, `fix/30-launcher-wake-refresh` and
`fix/37-gate-screen-rotation` — and the abandoned `docs/berean-os-design` branch
predates the Phase 0 strip and carries all 22. Every one of those is a branch
that has not yet caught up with `main`, not a branch reviving the key: `main`,
`refactor/38-remove-qr-display`, `fix/28-bookmark-save-budget` and this branch
are all clean. None resurrects a reference unless it is merged without first
rebasing past `4a107d5c` — in which case Error handling case 1 fails the build
loudly. *Decision:* remove exactly these 22:

`STR_ADD_SERVER`, `STR_CALIBRE_URL_HINT`, `STR_CHECKING_WIFI`,
`STR_DELETE_SERVER`, `STR_DISPLAY_QR`, `STR_ERROR_MSG`, `STR_FETCH_FEED_FAILED`,
`STR_FMT_AUTHOR_TITLE`, `STR_FMT_TITLE`, `STR_FMT_TITLE_AUTHOR`, `STR_LOADING`,
`STR_NEXT_PAGE`, `STR_NO_ENTRIES`, `STR_NO_SERVERS`, `STR_NO_SERVER_URL`,
`STR_PARSE_FEED_FAILED`, `STR_PASSWORD`, `STR_PREV_PAGE`, `STR_SERVER_NAME`,
`STR_TAP_TO_RETRY`, `STR_USERNAME`, `STR_WIFI_CONN_FAILED`.

*Attack surface:* the set is a snapshot. If another branch merges to `main`
before this one and adds a `tr()` for one of these, the implementer deletes a key
that has just become live — and §"Error handling" case 1 catches it at build
time, loudly. The plan must re-run the generator immediately before editing, not
trust this list.

**A2 — `STR_HIGHLIGHTS_TOO_LARGE` stays, and acceptance criterion 5 is restated
rather than met.** Criterion 2 makes keeping it conditional on "the bookmark task
[leaving] a note asking for it". Two documents answer and they disagree:
`docs/superpowers/specs/2026-09-16-issue-28-design.md:424-428` *rejected* the key
for the bookmark refusal because it says "highlights" and "would tell a user who
pressed *Toggle Bookmark* about highlights"; but issue **#39** — open, split out
of #27 — says in its own words that "`STR_HIGHLIGHTS_TOO_LARGE` already exists in
`english.yaml` and is currently unreferenced… **#31 owns the translation files
and should not strip it before this is decided.**" #39's owner comment measures
the gap as reachable by ordinary use: bookmarks hit the 45,000-byte budget at 219
records, which on a single-EPUB Bible is one bookmark per 140 chapters.

*Decision:* keep it. The later, more specific instruction wins over the earlier
one, and #28's rejection was of *reusing* the string for bookmarks, not of the
string existing. Consequence, measured on a scratch copy of the edit:

```
  Total: 421  |  Used in code: 420  |  Never used: 1
```

**Criterion 5 therefore reads, for this task: exactly one unused key remains, it
is `STR_HIGHLIGHTS_TOO_LARGE`, and the generator names it.** *Attack surface:* an
orchestrator may prefer to fail the issue rather than restate a criterion, or may
read #28's rejection as the decisive note and strip the key — which would delete
the string #39 exists to wire up, in all 1 of the files that hold it.

**A3 — The 17 keys that exist only outside `english.yaml` are left alone.**
`load_translations` builds the key list from English only, so a key defined in
another language with no English counterpart is invisible to the unused report
*and* to the generated output. Seventeen already exist — eight in `hebrew.yaml`,
six in `polish.yaml`, two shared with `norwegian.yaml`, one in `kazakh.yaml` —
19 lines, 844 bytes (research §6). They are exactly the thing the issue objects
to, and removing the 22 does not make the statement "the YAMLs carry only keys
the firmware uses" true. *Decision:* out of scope. They are a different defect
with a different detector (the issue's own acceptance criteria are all phrased
against the generator's report, which will never mention them), and folding them
in doubles the review surface of a task labelled *good first issue*. The research
note records them; a follow-up issue is the right home. *Attack surface:* a
reviewer may reasonably say that leaving 17 known-dead keys behind while
deleting 22 is an arbitrary line, and that the goal as the issue words it is not
met until both go.

**A4 — `STR_LOADING` goes, and `AGENTS.md` is knowingly left stale.**
`AGENTS.md:336` and `:709` use `tr(STR_LOADING)` as the project's worked example
of the i18n rule (`CLAUDE.md` is a symlink to `AGENTS.md`). Deleting the key
leaves the coding standard demonstrating `tr()` with a key that no longer exists.
On the merits the key should still go: `STR_LOADING_POPUP` ("Loading",
`english.yaml:35`) is live at six sites — `src/main.cpp:184,197`,
`src/activities/home/HomeActivity.cpp:70`,
`src/activities/util/BmpViewerActivity.cpp:99,195`,
`src/activities/catalog/PublicationsActivity.cpp:73` — so nothing that wants a
loading message is short of one, and the issue's "cheap to re-add if so" holds.
*Decision:* delete `STR_LOADING`; do **not** edit `AGENTS.md`, because the brief
reserves everything outside `lib/I18n/translations/` and a two-line doc edit in
this PR is exactly the scope creep the constraint exists to stop. The hand-back
names it as a follow-up. *Attack surface:* shipping a standards document that
cites a deleted symbol is a small, real defect; a reviewer may want the two lines
changed to `STR_LOADING_POPUP` here, or the key kept until they are.

**A5 — `scripts/i18n_orphans.sh` is used as a gate, with its ordering stated,
and not fixed.** `i18n_orphans.sh:10` greps `src lib --include=*.cpp --include=*.h`,
which includes the build-generated, gitignored `lib/I18n/I18nKeys.h`. Whether
that file lists a key depends only on whether the run that produced it stripped.
Reproduced on unchanged sources:

```
# after `gen_i18n.py … --strip-unused` (what pio run leaves: 420 StrIds)
$ ./scripts/i18n_orphans.sh | wc -l
      23
# after `gen_i18n.py …` with no flag    (443 StrIds)
$ ./scripts/i18n_orphans.sh | wc -l
       0
```

Same YAMLs, same sources, same commit. The #38 plan hit this from the other side
(`2026-09-16-issue-38-design.md:418`). *Decision:* keep the script as-is and
require the gate to run **after** `pio run`, expecting **1** line
(`orphan: STR_HIGHLIGHTS_TOO_LARGE`). **Assert on its stdout, never on its exit
status:** the script ends in a `grep | while read … echo` loop
(`i18n_orphans.sh:9-13`) and so exits 0 whether it prints 0 orphans or 23,
despite the `set -euo pipefail` on line 7. Wiring it into a `set -e` runner
without reading its output is a gate that passes unconditionally. Fixing the
script means editing `scripts/`, outside the allowed scope, and the fix belongs
with whoever owns the gate. *Attack surface:*
a gate that reads 0 or 23 by accident is a bad gate, and a reviewer may want it
excluded from the verification set rather than used with a caveat.

**A6 — The edit is a colon-anchored line delete, applied file by file; the YAML
is not otherwise reformatted.** Checked across all 32 files: every non-blank line
matches `^KEY: value`. There are no comments, no blank-line section headers, no
multi-line or folded values, and no duplicate keys within a file (research §9).
The removable keys are interleaved with live ones — `STR_ADD_SERVER` at
`english.yaml:334` sits between `STR_STEP_HINT_SIDE` and `STR_SERVER_NAME`, not
in an OPDS block — so a line delete leaves no orphaned header. The precedent is
`docs/superpowers/plans/2026-09-13-phase-0-fork-and-strip.md:844`, and its
trailing colon is load-bearing:

```sh
sed -i '' '/^STR_LOADING:/d' "$f"     # deletes one key
sed -i '' '/^STR_LOADING/d'  "$f"     # also deletes STR_LOADING_POPUP and
                                      # STR_LOADING_FONT_LIST — both live
```

*Decision:* every pattern carries the trailing colon; no sorting, no
re-indenting, no trailing-whitespace cleanup, no touching a file's final newline.
That last clause has one named beneficiary: **`arabic.yaml` is the only file with
no terminating newline** (`tail -c 1` is non-empty). Its last line,
`STR_RECOVERY_MODE_HINT`, is not on the removal list, so a line delete preserves
the anomaly — but any implementation that reads and rewrites a whole file
(`splitlines()` + `'\n'.join()` + a trailing newline, the obvious portable form)
would silently append one, turning gate 5's "0 insertions" into 1 and adding a
`\ No newline at end of file` hunk that reads as scope creep. A pure line delete
makes `git diff --stat` a checkable artefact (see the expected per-file counts in
Architecture). *Attack surface:* `sed -i ''` is the BSD
spelling and is wrong on Linux; a plan that hardcodes it is host-specific. A
reviewer may prefer a Python edit for portability.

**A7 — Verification is a regenerate-and-diff, not a build measurement.** Because
the build already strips (Problem), the generated C++ after this change must be
*identical* to the generated C++ before it. Proven, not asserted — generating from
today's YAMLs and from a scratch copy with the 22 removed, both through the
build's own `--strip-unused` path:

```
$ diff -r /tmp/genA /tmp/genB
$ echo $?
0
```

`I18nKeys.h`, `I18nStrings.h` and `I18nStrings.cpp` are byte-for-byte equal.
*Decision:* make that diff the primary gate. A green `pio run` proves only that
the tree still compiles; the diff proves the deletion hit exactly the intended
keys. *Attack surface:* the diff requires generating into two scratch directories
and is more ceremony than a first-issue task usually carries; a reviewer may
argue the build plus the generator's own key count is enough.

**A8 — No deleted key name may be written into a `.cpp`, `.h` or `.c` comment.**
`find_used_string_keys` (`gen_i18n.py:252-286`) is a regex sweep —
`\bSTR_[A-Za-z0-9_]+\b` (`:267`) over every `.cpp`/`.h`/`.c` (`:275`) under `src`
and `lib` (`:831`) — and does not parse C++, so a name in a comment counts as a
reference. A key deleted from `english.yaml` but named in a comment lands in the
*other* detector: `missing_keys = used_keys - string_keys`, which prints
`CRITICAL: … used in source but missing from english.yaml` and calls
`sys.exit(1)` (`gen_i18n.py:874-881`). *Decision:* the deletion is documented in
the commit message and this spec, never in a code comment. *Attack surface:*
none known; this is a trap to avoid, not a trade-off.

**A9 — The three generated files are never staged.** `lib/I18n/I18nKeys.h`,
`I18nStrings.h` and `I18nStrings.cpp` are gitignored (`.gitignore:8-10`) and are
rewritten by every `pio run` and by every verification step in this spec.
*Decision:* the final `git status --short` must show changes only under
`lib/I18n/translations/`, and `git status --short --ignored lib/I18n/` must show
the three files as `!!`. *Attack surface:* none; this is the repo's standing
rule (`CLAUDE.md`, "Generated files and build artifacts").

---

## Architecture

### The pipeline, and why it has a fixed point

```
lib/I18n/translations/*.yaml        32 files, the committed source
        |
        |  load_translations()      key list taken from english.yaml ALONE;
        |                           other languages fill in, missing -> English
        v
  string_keys (443) + translations
        |
        |  find_used_string_keys()  regex over src/ lib/ *.cpp *.h *.c
        |  report_unused_keys()     -> unused_set (23)
        |
        |  if strip_unused:         gen_i18n.py:916-923  <-- ALWAYS true in a build
        v
  string_keys (420)
        |
        v
  I18nKeys.h / I18nStrings.h / I18nStrings.cpp     gitignored, rebuilt every time
        |
        v
  I18n::get(StrId)                 lib/I18n/I18n.cpp:15-28
```

The load-bearing property: **the 420-key output is a fixed point.** Deleting the
22 from the YAML moves work from the `strip_unused` filter to the source files
and changes the output not at all (A7). That is what makes this a documentation
change wearing a code change's clothes, and it is why the testing strategy is
built on a diff rather than on a size or behaviour measurement.

### The removal set, and what each file loses

All 32 files change; none is left untouched, because the seven 32/32 keys reach
every file.

| File | lines deleted | keys before → after |
|---|---|---|
| `english.yaml` | 22 | 446 → 424 |
| `arabic`, `belarusian`, `bosnian`, `czech`, `french`, `german`, `hungarian`, `indonesia`, `lithuanian`, `portuguese-BR`, `portuguese-PT`, `slovak`, `slovenian`, `swedish`, `turkish`, `ukrainian`, `vietnamese` | 22 each | 366 → 344 |
| `catalan`, `kazakh`, `valencian` | 22 each | 367 → 345 |
| `italian` | 22 | 365 → 343 |
| `russian` | 22 | 363 → 341 |
| `norwegian` | 22 | 368 → 346 |
| `hebrew`, `polish` | 22 each | 374 → 352 |
| `spanish` | 22 | 413 → 391 |
| `danish`, `dutch`, `romanian` | 15 each | 274 → 259 |
| `finnish` | 14 | 244 → 230 |
| `orangutan` | 13 | 285 → 272 |
| **total** | **666** | **11,377 → 10,711** |

`22 × 32 = 704` is the wrong number and a plan that asserts it fails on its own
arithmetic: `danish`, `dutch` and `romanian` never had the five OPDS server keys,
nor `STR_NO_SERVERS` or `STR_TAP_TO_RETRY` — seven each; `finnish` lacks those
seven plus `STR_DISPLAY_QR`; `orangutan` lacks nine. **Derive every per-file
count from the table above, never by multiplication.**

### What cannot move

Three things a reviewer might expect to break, verified not to:

- **`StrId` renumbering is safe.** `I18n::get` (`lib/I18n/I18n.cpp:15-28`) takes
  `static_cast<size_t>(id)`, bounds-checks against `StrId::_COUNT`, and indexes
  `lang.offsets[index]`. Both the enum and every offset table are regenerated
  from one key list in one run, so they cannot disagree. No `StrId` is persisted,
  serialised, or stored as an integer anywhere in `src/` — the `static_cast`s
  near a `StrId` cast container sizes and font enums, not ids.
- **The persisted UI language is unaffected.** `SETTINGS.language` is a `uint8_t`
  (`src/CrossPointSettings.h:338`) cast to `Language` at `src/main.cpp:414`, and
  that enum's order is English-first then `_order` (`gen_i18n.py:174-176`).
  Removing `STR_*` lines cannot move it — provided no `_order` line is touched,
  which the non-goals forbid.
- **`CHARACTER_SETS` does not change, and nothing reads it anyway.**
  `compute_character_set` (`gen_i18n.py:424`) runs inside `generate_strings_cpp`,
  i.e. *after* the strip, so it is already computed from 420 keys. Separately,
  `I18n::getCharacterSet` (`lib/I18n/I18n.cpp:53-60`) has zero callers in `src/`
  or `lib/` — it is dead API.

### The English-fallback encoding, and the one thing it hides

`I18nStrings.cpp` deduplicates: a non-English string identical to English is not
stored, and its offset carries bit 15 to redirect into the English blob
(`lib/I18n/I18n.cpp:24-27`). This matters for Error handling case 3: deleting a
translation that happens to equal its English text changes no generated byte,
because it was never stored separately.

## Data and control flow

**Today**, at build time:

```
pio run
  -> pre:scripts/gen_i18n.py          platformio.ini:131
  -> main(strip_unused=True)          gen_i18n.py:1001
  -> load_translations()              443 keys from english.yaml
  -> find_used_string_keys(src, lib)  420 referenced
  -> unused_set = 23                  "Stripping 23 unused string(s) from output."
  -> generate_*()                     420 keys emitted
```

**After**, the same build:

```
  -> load_translations()              421 keys from english.yaml
  -> find_used_string_keys(src, lib)  420 referenced
  -> unused_set = 1                   "Stripping 1 unused string(s) from output."
  -> generate_*()                     420 keys emitted   <-- byte-identical (A7)
```

**At runtime**, nothing changes and no path is added or removed. A screen calls
`tr(STR_X)` → `I18n::getInstance().get(StrId::STR_X)` (`lib/I18n/I18n.h:39`) →
bounds check against `_COUNT` → `getLanguageStrings(_language)` → offset lookup,
with bit 15 redirecting to English (`lib/I18n/I18n.cpp:15-28`). The 22 deleted
keys have no call site by construction — that is what "unreferenced" means — so
there is no path to change, no fallback to design, and no "string removed"
state a user can reach.

## Error handling

No runtime error path is added or removed; this change cannot fail on the device.
The failure modes are all *edit-time*, and they differ sharply in how loudly they
fail. Each is listed with the gate that catches it.

| # | Mistake | Detected by | Loudness |
|---|---|---|---|
| 1 | Delete a **live** key from `english.yaml` | `gen_i18n.py:874-881` → `CRITICAL: N string(s) used in source but missing from english.yaml` + `sys.exit(1)`; `pio run` fails | Loud, immediate |
| 2 | Delete a **live** key from a non-English file, where that translation **differs** from English | **Only** the A7 diff | **Silent** in the generator |
| 3 | Delete a **live** key from a non-English file, where that translation **equals** English | Nothing — and nothing needs to | Harmless |
| 4 | Unanchored pattern (`/^STR_LOADING/d`) | The A7 diff, and case 1 if the collateral key is in English | Silent without the diff |
| 5 | Write a deleted key's name into a `.cpp`/`.h` comment | `gen_i18n.py:874-881` → `CRITICAL` + `sys.exit(1)` | Loud (A8) |
| 6 | Stage a generated file | `git status --short` (A9) | Silent; `.gitignore:8-10` normally prevents it |

**Case 2 is the one that justifies the whole testing strategy.** Verified by
injecting it: deleting `STR_TAGS_AND_SETTINGS` ("Etiquetas y ajustes") from
`spanish.yaml` alone leaves the generator at exit 0 with no `CRITICAL` and the
same 420-key count, and English silently substitutes for Spanish on that screen.
The diff catches it:

```
Files /tmp/genB/I18nStrings.cpp and /tmp/sim2/out/I18nStrings.cpp differ
```

**Case 3 is genuinely a non-event**, and the gate is exactly as sensitive as it
should be: deleting `STR_BEREAN` ("bereanOS", identical in both) from
`spanish.yaml` produces an identical diff, because the dedup never stored it
(Architecture, "The English-fallback encoding"). The diff fires if and only if
behaviour would change.

The repo's standing error convention (`CLAUDE.md`, "Error handling":
`LOG_ERR` + return false) does not apply — there is no new runtime branch that
can fail, and no `LOG_ERR` is added or removed.

## Testing strategy

**Host suite — nothing to add, and nothing to change.** `test/` is entirely
i18n-free: `grep -rnE "\bI18n\b|\bStrId\b|\bSTR_[A-Z]|\btr\("` over `test/`
returns zero hits, and no suite's `CMakeLists.txt` mentions `I18n`. The 51 suites
registered in `test/CMakeLists.txt:51-113` must still pass unchanged. Writing a
test for this change would be writing a test for the *generator*, which is a
different subject and a different issue.

**Gates**, in this order, after the last edit. Ordering matters twice: for A5 and
for A7.

```sh
# 0. Re-derive the list immediately before editing (A1). It must still be 23.
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --verbose \
  | sed -n '/Unused keys/,/^$/p'

# 1. Capture the CURRENT generated output, before touching any YAML (A7).
mkdir -p /tmp/i18n-before
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-before --strip-unused

#    ... make the 22 deletions ...

# 2. The primary gate: the generated C++ must be byte-identical.
mkdir -p /tmp/i18n-after
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-after --strip-unused
diff -r /tmp/i18n-before /tmp/i18n-after && echo "IDENTICAL"   # expect IDENTICAL

# 3. The generator's own report. Expect exactly one unused key, named (A2).
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --verbose \
  | sed -n '/Unused keys/,/^$/p'
#    expect:  Unused keys (1):  - STR_HIGHLIGHTS_TOO_LARGE
#    and NOT: CRITICAL ... missing from english.yaml

# 4. The metadata keys survive (criterion 4): expect 32 on each line.
for k in _language_name _language_code _order; do
  printf '%s: ' "$k"
  grep -l "^${k}:" lib/I18n/translations/*.yaml | wc -l
done

# 5. Shape of the diff (A6): a pure line delete, 666 lines, no insertions.
git diff --stat -- lib/I18n/translations/   # expect 32 files, 666 deletions, 0 insertions
#    `--stat`'s own "0 insertions(+)" is the signal. Do NOT assert with a bare
#    `git diff … | grep -c '^+[^+]'`: grep -c exits 1 when it matches nothing,
#    so under `set -e` that gate kills the run exactly when the change is clean.
[ "$(git diff -- lib/I18n/translations/ | grep -c '^+[^+]' || true)" -eq 0 ]

# 6. Build. It regenerates lib/I18n/I18nKeys.h, which gate 7 greps (A5).
#    If this reports `bad interpreter: .../penv/bin/python`, that is a host
#    problem, not this change: the venv's `python` is a symlink into Homebrew's
#    python@3.14, and a Homebrew point upgrade orphans it if it names a Cellar
#    path. It currently points at the upgrade-stable
#    /opt/homebrew/opt/python@3.14/bin/python3.14 and `pio --version` reports
#    PlatformIO Core 6.1.19. Repair by re-pointing that symlink, or run
#    `PYTHONPATH="$HOME/.platformio/penv/lib/python3.14/site-packages" \
#       python3 -m platformio run`.
~/.platformio/penv/bin/pio run
#    expect "Stripping 1 unused string(s) from output." and "String keys: 420",
#    SUCCESS, and a Flash figure unchanged from the 5,314,782 B baseline

# 7. Orphan gate — AFTER the build, never before (A5). Expect one line.
./scripts/i18n_orphans.sh
#    expect exactly: orphan: STR_HIGHLIGHTS_TOO_LARGE

# 8. Host suite, untouched.
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release   # NOT -G Ninja: ninja is
cmake --build build/test                                 # not installed here
ctest --test-dir build/test --output-on-failure -j

# 9. Nothing generated is staged (A9).
git status --short                       # expect only lib/I18n/translations/*.yaml
git status --short --ignored lib/I18n/   # expect the three generated files as !!
```

**`./bin/clang-format-fix` is not required by this change** — it formats C/C++,
and no `.cpp`, `.h` or `.c` file is touched. Running it costs nothing and the
working agreement asks for it, so run
`PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` last and expect a no-op; the
PATH prefix is needed because the bare wrapper exits 1 when `clang-format` is not
found.

**`pio check` is not a meaningful gate here.** It analyses C/C++ and this change
edits no C/C++; combined with gate 2 proving the generated C++ is identical, a
`pio check` result cannot differ from the baseline by construction.

**The baseline for gate 6**, measured on `c48e3ec7` with the YAMLs untouched:

```
RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)
Flash: [========  ]  81.1% (used 5314782 bytes from 6553600 bytes)
========================= [SUCCESS] Took 212.56 seconds =========================
```

**A moving flash figure is a failure signal, not a win.** Gate 2 asserts the
generated sources are identical, so the only ways the binary can change are a
different toolchain state or an edit outside the intended scope.

**Nothing here needs hardware.** Every key on the removal list is unreachable
from any screen — that is what "unreferenced" means — so there is no device check
this task can pass or fail. Anyone offering a hardware sign-off for it has tested
nothing. The one human-only item is a judgement, not a measurement: whether A2's
restatement of criterion 5 is accepted.

## Risks

- **A live key is deleted from a non-English file and English silently covers
  it.** The highest-severity risk, because the generator does not notice
  (Error handling case 2). Mitigated entirely by gate 2, and only by gate 2.
- **The list drifts between measurement and edit.** Another branch merges a
  `tr()` for one of the 22. Mitigated by gate 0 and caught loudly by case 1.
- **The orphan gate is run before the build and reads 0.** Reads as "no orphans,
  job done" when the truth is "the header is stale". Mitigated by A5's ordering.
- **Scope creep into `AGENTS.md`, `docs/i18n.md`, or the 17 keys of A3.** Each is
  defensible in isolation and none is in the brief; A3 and A4 record them as
  follow-ups so the PR stays reviewable.

## Open questions

1. **Does the orchestrator accept A2's restatement of acceptance criterion 5?**
   It cannot be met as written while #39's instruction is honoured. This is the
   one decision the spec cannot make on its own evidence.
2. **A3 — should the 17 non-English-only keys be folded in?** The spec says no;
   the issue's stated goal arguably says yes.
3. **A4 — should `AGENTS.md:336,709` be corrected in this PR?** Two lines, out of
   scope, and stale the moment this merges.

---

## Review pass 0 — what changed and why

`docs/superpowers/reviews/issue-31-spec-review-0.md` returned **CLEAR** with 2
MAJOR and 4 MINOR findings, all fixed inline above. Each was re-derived before
being applied; one was applied in a corrected form, and that difference is
recorded here rather than buried.

| # | Finding | Applied as |
|---|---|---|
| MAJOR 1 | A1 claimed `git grep` across the local branches finds none of the 22 in any `src/**` or `lib/**` source. **False.** | A1's third check rewritten. The original sweep used `git grep -E "…\b"`, and `\b` is undefined in POSIX ERE, so the pattern matched nothing on every branch and the emptiness was an artefact of the regex, not a property of the tree. Re-derived with `git grep -w`: eight branches reference `STR_DISPLAY_QR`, and `docs/berean-os-design` carries all 22. The removal set does not move — `main` is clean and all eight simply predate `4a107d5c` — but the sentence now says that instead of overclaiming. |
| MAJOR 2 | Gate 6's `~/.platformio/penv/bin/pio run` was unexecutable (`bad interpreter`) — the venv `python` symlink pointed at a Homebrew Cellar path (`python@3.14/3.14.5`) that a point upgrade had removed. | Applied **as a troubleshooting note, not as a repair step**, because the condition no longer holds: the symlink was repaired during the review window and now targets the upgrade-stable `/opt/homebrew/opt/python@3.14/bin/python3.14`; `~/.platformio/penv/bin/pio --version` reports PlatformIO Core 6.1.19 and exits 0. Prescribing a repair for a healthy tool would be its own defect. The failure mode is real and recurrent, so gate 6 now names it and says it is a host problem. The review's independently-routed build reproduced the `5,314,782 B` / `64,052 B` baseline exactly, which is the number that mattered. |
| MINOR 3 | A5's "never discard its exit status" caveat protects nothing: `i18n_orphans.sh` exits 0 with 23 orphans. | A5 now says to assert on stdout and names why (`i18n_orphans.sh:9-13` ends in an echoing `while` loop, so `set -euo pipefail` on line 7 cannot help). Confirmed: `./scripts/i18n_orphans.sh >/dev/null; echo $?` → `0` with 23 orphans present. |
| MINOR 4 | Gate 5's `git diff … \| grep -c '^+[^+]'` exits 1 exactly when it passes, killing a `set -e` runner on the green path. | Gate 5 rewritten to a `[ … -eq 0 ]` test with `\|\| true`, and `--stat`'s own "0 insertions(+)" named as the real signal. Confirmed: `bash -c 'set -e; … \| grep -c "^+[^+]"; echo SURVIVED'` prints `0`, never `SURVIVED`, and exits 1. |
| MINOR 5 | Architecture prose said `danish`/`dutch`/`romanian` "never had the five OPDS server keys" — which yields 17 and contradicts the table's 15 two lines above. | Corrected to seven (the five server keys plus `STR_NO_SERVERS` and `STR_TAP_TO_RETRY`), with an explicit instruction to derive per-file counts from the table rather than by multiplication. Confirmed: `danish` lacks 7 → 15, `finnish` 8 → 14, `orangutan` 9 → 13, matching every table row. |
| MINOR 6 | `arabic.yaml` is the only file with no trailing newline; A6's "don't touch the final newline" held only by luck, and A6's own suggested Python alternative would break gate 5. | A6 now names the file, why the line delete is safe there (its last line, `STR_RECOVERY_MODE_HINT`, is not on the removal list), and what a whole-file rewriter must preserve. Confirmed: `arabic.yaml` is the sole `tail -c 1` non-empty file of the 32. |

Not changed, and why: the reviewer explicitly declined to raise A2's restatement
of acceptance criterion 5, A3's 17 out-of-scope keys, and A4's knowingly-stale
`AGENTS.md` example, recording each as a stated trade-off rather than a defect.
Open Question 1 therefore stands as written and is still the human's call.
