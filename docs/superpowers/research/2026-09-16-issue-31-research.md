# Issue #31 — what is actually unused, and what must not be stripped

Measured on `c48e3ec7` (`main` at release 1.9.5), not on the commit the worktree
arrived with. Every count below comes from running `scripts/gen_i18n.py` or from
a script over `lib/I18n/translations/`; nothing is quoted from the issue body.
No device was involved. `pio run` was run to completion; §3 quotes its i18n
pre-step and §12 gives the result.

---

## 1. The worktree was based on a commit that predates both prerequisites

The branch arrived at `ded48d17` ("feat: publication language setting…", PR #25).
`origin/main` was eleven commits ahead, and the two commits the brief names as
prerequisites are both in that gap:

```
4a107d5c fix: remove Show page as QR, which never produced a scannable code (#41)
d043fd84 fix: give bookmarks a save budget, an atomic write and a recoverable load (#47)
```

The branch held no commits of its own (`git log --oneline origin/main..HEAD` →
0 lines), so it was fast-forwarded to `c48e3ec7` before anything was measured.
**Anything measured on `ded48d17` is wrong by at least one key**, and the
research, spec and plan for issues #27/#28/#30/#37/#38 — which the next phases
will want to read — do not exist in that tree at all.

## 2. The list is 23, not 22

```
$ python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --verbose
  Total: 443  |  Used in code: 420  |  Never used: 23
```

The extra key over the issue body's 22 is `STR_DISPLAY_QR`, released into
orphanhood by `4a107d5c`. On `ded48d17` the same command reports exactly the 22
the issue lists, which is the tell that the issue body was measured pre-merge.

| | |
|---|---|
| `STR_*` keys in `english.yaml` | 443 |
| Referenced from `src/` or `lib/` | 420 |
| Never referenced | **23** |
| Of those, to be removed (see §4) | **22** |

The 23: `STR_ADD_SERVER`, `STR_CALIBRE_URL_HINT`, `STR_CHECKING_WIFI`,
`STR_DELETE_SERVER`, `STR_DISPLAY_QR`, `STR_ERROR_MSG`, `STR_FETCH_FEED_FAILED`,
`STR_FMT_AUTHOR_TITLE`, `STR_FMT_TITLE`, `STR_FMT_TITLE_AUTHOR`,
`STR_HIGHLIGHTS_TOO_LARGE`, `STR_LOADING`, `STR_NEXT_PAGE`, `STR_NO_ENTRIES`,
`STR_NO_SERVERS`, `STR_NO_SERVER_URL`, `STR_PARSE_FEED_FAILED`, `STR_PASSWORD`,
`STR_PREV_PAGE`, `STR_SERVER_NAME`, `STR_TAP_TO_RETRY`, `STR_USERNAME`,
`STR_WIFI_CONN_FAILED`.

## 3. `--strip-unused` does not touch the YAML, and the keys already cost no flash

The issue asks this be checked before running it. It was.

```
$ find lib/I18n/translations -name '*.yaml' | sort | xargs shasum | shasum
75ca8091e7af3d17d92ede0d8c50f0bae5f8f1c8  -
$ python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --strip-unused
  Stripping 23 unused string(s) from output.
$ find lib/I18n/translations -name '*.yaml' | sort | xargs shasum | shasum
75ca8091e7af3d17d92ede0d8c50f0bae5f8f1c8  -
```

Byte-identical, and `git status --short lib/I18n/translations/` is empty. The
flag filters three in-memory lists immediately before the generators run
(`scripts/gen_i18n.py:916-923`) and writes nothing back. **The implementer must
strip the YAMLs by hand**; the flag cannot do it.

The same run shows what the flag *is* for. `platformio.ini:131` registers the
script as `pre:scripts/gen_i18n.py`, and imported by SCons it takes the `else`
branch and calls `main(strip_unused=True)` (`gen_i18n.py:1001`). Confirmed in a
real build, not inferred:

```
$ ~/.platformio/penv/bin/pio run
  Stripping 23 unused string(s) from output.
Code generation complete!
  Languages: 32
  String keys: 420
```

Generated `I18nStrings.cpp` is 1,213,365 B with all 443 keys and 1,143,006 B at
420; `I18nKeys.h` declares 443 `StrId` members versus 420. **Every firmware
build already takes the 420-key version, so the 23 orphans occupy zero flash
today.** This task buys translator attention and nothing else — no spec should
promise a binary-size number for it.

## 4. `STR_HIGHLIGHTS_TOO_LARGE` must stay, and that contradicts criterion 5

Acceptance criterion 2 says to keep it "if the bookmark task left a note asking
for it". Two documents answer, and they answer differently:

- **The bookmark task rejected it.** `docs/superpowers/specs/2026-09-16-issue-28-design.md:424-428`
  considered `STR_HIGHLIGHTS_TOO_LARGE` for the bookmark refusal and declined:
  it reads "This book already has too many highlights" and "would tell a user
  who pressed *Toggle Bookmark* about highlights." All three bookmark refusals
  ship pointing at `STR_ERROR_GENERAL_FAILURE` instead. Confirmed in the merged
  code: `grep -n "TOO_LARGE" lib/I18n/translations/english.yaml` returns only
  `STR_FIRMWARE_TOO_LARGE:379` and `STR_HIGHLIGHTS_TOO_LARGE:390` — the
  `STR_BOOKMARKS_TOO_LARGE` that spec described was never added.
- **Issue #39 asks for it by name, in writing.** "`STR_HIGHLIGHTS_TOO_LARGE`
  already exists in `english.yaml` and is currently unreferenced — it may
  generalise, or it may want to stay specific and a new key added beside it.
  **#31 owns the translation files and should not strip it before this is
  decided.**" #39 is open, and its owner comment measures the gap as reachable
  by ordinary use: bookmarks hit the 45,000-byte budget at 219 records, which on
  a single-EPUB Bible is one bookmark per 140 chapters.

So the note exists — it is on #39, not on #28 — and it says keep. **The removal
set is 22 keys.**

That makes acceptance criterion 5 unsatisfiable as written. Verified by
simulating the exact edit on a scratch copy of the 32 files:

```
$ python3 scripts/gen_i18n.py /tmp/.../translations /tmp/.../out
  Total: 421  |  Used in code: 420  |  Never used: 1
```

Not zero. One, deliberately. The spec has to either restate criterion 5 as
"exactly one unused key, `STR_HIGHLIGHTS_TOO_LARGE`, and it is the expected one"
or get the criterion amended. A plan that copies criterion 5 verbatim sets a
gate its own correct output fails.

The simulation also confirms the removal is otherwise clean: 443 → 421 keys, and
no `CRITICAL: … used in source but missing from english.yaml` (the hard
`sys.exit(1)` at `gen_i18n.py:874-881`).

## 5. "Carried in all 32 languages" is true of seven keys out of 23

| Key | Files defining it | Absent from |
|---|---|---|
| `STR_CHECKING_WIFI`, `STR_FMT_AUTHOR_TITLE`, `STR_FMT_TITLE`, `STR_FMT_TITLE_AUTHOR`, `STR_LOADING`, `STR_PASSWORD`, `STR_USERNAME` | 32/32 | — |
| `STR_CALIBRE_URL_HINT`, `STR_ERROR_MSG`, `STR_FETCH_FEED_FAILED`, `STR_NO_ENTRIES`, `STR_NO_SERVER_URL`, `STR_PARSE_FEED_FAILED`, `STR_WIFI_CONN_FAILED` | 31/32 | `orangutan.yaml` |
| `STR_DISPLAY_QR` | 31/32 | `finnish.yaml` |
| `STR_ADD_SERVER`, `STR_DELETE_SERVER`, `STR_NEXT_PAGE`, `STR_PREV_PAGE`, `STR_SERVER_NAME` | 28/32 | `danish`, `dutch`, `finnish`, `romanian` |
| `STR_NO_SERVERS`, `STR_TAP_TO_RETRY` | 27/32 | + `orangutan` |
| `STR_HIGHLIGHTS_TOO_LARGE` | **1/32** | every file but `english.yaml` |

The practical consequence: a plan that says "delete 22 lines from each
of 32 files" and asserts `22 × 32 = 704` will fail on its own arithmetic. The
real figure, counted over the files:

```
lines: 666   bytes: 28070
```

666 lines and 28,070 bytes out of `lib/I18n/translations/`'s 11,414 lines and
485,293 bytes — **5.8 %**. Any per-file line-count assertion has to be derived
per file, not multiplied.

## 6. A second class of dead weight the "unused" detector cannot see

`load_translations` builds the key list from `english.yaml` alone, so a key
defined only in another language is invisible to both the unused report and the
generated output. Seventeen already exist:

| Key | Where |
|---|---|
| `STR_ALL_FONTS_INSTALLED`, `STR_CALIBRE_WEB_URL`, `STR_CONFIRM_DOWNLOAD_PROMPT`, `STR_DELETE_CONFIRM`, `STR_FILES_LABEL`, `STR_REDOWNLOAD`, `STR_SD_CARD_FULL`, `STR_SIZE_LABEL` | `hebrew.yaml` |
| `STR_DOWNLOAD_FONTS`, `STR_FONT_DOWNLOAD`, `STR_LARGE`, `STR_MEDIUM`, `STR_SMALL`, `STR_X_LARGE` | `polish.yaml` |
| `STR_PERCENT_STEP_HINT`, `STR_SLEEP_TIMER_STEP_HINT` | `norwegian.yaml`, `polish.yaml` |
| `STR_LONG_PRESS_SKIP` | `kazakh.yaml` |

19 lines, 844 bytes. These are exactly the thing the issue objects to — strings
for subsystems that no longer exist, sitting in front of translators — and
`gen_i18n.py` will keep reporting "0 unused" with every one of them in place.
Whether they are in scope is the spec's call; that they exist, and that the
issue's stated goal ("the translation YAMLs carry only keys the firmware
actually uses") is not met by removing the 22 alone, is a fact.

## 7. The detector's contract, and why the 22 really are unreferenced

`find_used_string_keys` (`gen_i18n.py:252-286`) is a regex sweep:
`\bSTR_[A-Za-z0-9_]+\b` (`:267`) over every `.cpp`, `.h`, `.c` (`:275`) under
`src` and `lib` (`:831`), skipping only the three generated basenames
(`:249`). It does not parse C++, so a key named in a comment counts as used.

Three checks, all clean:

- **No key is kept alive by a comment alone.** A script collecting every
  `STR_*` occurrence with its line, then testing whether all of a key's lines
  start with `//`, `*` or `/*`, reports none. So 23 is not an undercount.
- **None of the 22 is referenced anywhere else in the repo.** `grep -rn "\b<key>\b"`
  over the whole tree, excluding `.git`, `.pio`, `translations/` and the three
  generated files, returns hits only in `docs/superpowers/**` prose and in
  `AGENTS.md` (§11). Zero in `src/`, `lib/`, `test/`, `scripts/`, `data/`,
  `freeink-sdk/`.
- **No in-flight branch reintroduces one.** `git grep` for the 22 across all
  thirteen local branches, restricted to `src/**` and `lib/**` sources, is empty
  on every branch — including `feature/buscar-catalog-search`, which has a live
  worktree and is the most plausible source of a new "No entries found".

The converse also holds and is worth stating because it bites the *next* phase:
because a comment counts as a reference, writing one of the deleted names into a
comment after deleting it from `english.yaml` makes `pio run` fail with
`CRITICAL: … used in source but missing from english.yaml` (`gen_i18n.py:874-881`).
A plan that documents the removal in a code comment breaks the build.

## 8. The orphan gate reports 23 or 0 depending on nothing but the last generator run

`scripts/i18n_orphans.sh:10` greps `src lib --include=*.cpp --include=*.h` —
which includes the generated, gitignored `lib/I18n/I18nKeys.h`. Whether that
file lists a key depends on whether the run that produced it stripped:

```
# after gen_i18n.py … --strip-unused   (what pio run leaves behind: 420 StrIds)
$ ./scripts/i18n_orphans.sh | wc -l
      23
# after gen_i18n.py … (no flag)        (443 StrIds)
$ ./scripts/i18n_orphans.sh | wc -l
       0
```

Same YAMLs, same sources, same commit. The gate is only meaningful immediately
after a `pio run`; run against a hand-generated header it silently passes. The
issue #38 plan hit the same trap from the other side. Any verification step in
the spec that uses this script has to state the ordering, and must not pipe it
through `wc -l` in a way that discards its exit status.

## 9. The edit itself is line-oriented and safe

Checked across all 32 files: every non-blank line matches `^KEY: value`. There
are **no comments, no blank-line section headers, no multi-line or folded
values, and no duplicate keys within a file.** Deleting a key is deleting one
line, and it leaves no orphaned header behind — the keys are interleaved with
live ones (`STR_ADD_SERVER` at `english.yaml:334` sits between
`STR_STEP_HINT_SIDE` and `STR_SERVER_NAME`), not grouped in an OPDS block.

The precedent idiom is `docs/superpowers/plans/2026-09-13-phase-0-fork-and-strip.md:844`,
and its trailing colon is load-bearing:

```sh
sed -i '' '/^STR_FMT_TITLE:/d' "$f"     # deletes one key
sed -i '' '/^STR_FMT_TITLE/d'  "$f"     # also deletes STR_FMT_TITLE_AUTHOR
```

`STR_FMT_TITLE` is a proper prefix of `STR_FMT_TITLE_AUTHOR`, and both are on the
removal list, so the mistake is invisible here — but `STR_LOADING` is a prefix of
`STR_LOADING_POPUP` (`english.yaml:34,35`) and `STR_LOADING_FONT_LIST`
(`:346`), both of which are **live**. An unanchored delete of `STR_LOADING`
takes out six call sites' worth of working string. `sed -i ''` is also the BSD
spelling; it is wrong on Linux.

## 10. Three things that do not break, so no one needs to defend them

- **Renumbering `StrId` is safe.** No `StrId` is persisted, serialised or cast to
  an integer anywhere: every `static_cast` near a `StrId` in `src/` casts a
  container size or a font enum, not the id.
- **The persisted UI language is unaffected.** `SETTINGS.language` is a `uint8_t`
  (`src/CrossPointSettings.h:338`) cast to `Language` at `src/main.cpp:414`, and
  that enum's ordering comes from English-first then `_order`
  (`gen_i18n.py:174-176`). Removing `STR_*` lines cannot move it — provided no
  `_order` line is touched.
- **The three metadata keys are all present.** `_language_name`,
  `_language_code` and `_order` are in 32/32 files today, so criterion 4 is
  already met and stays met under a line-delete of `STR_*` keys.

## 11. Two corrections to the issue body, both small and both load-bearing for citations

- **`docs/translators.md` does not ask translators to keep keys current.** It is
  66 lines of contributor credits. The instruction the issue means is
  `docs/i18n.md:254-260`, "For Translators".
- **`AGENTS.md` uses `STR_LOADING` as its worked example**, at `:336` and `:709`
  (`CLAUDE.md` is a symlink to it). Deleting the key leaves the project's own
  coding standard demonstrating `tr()` with a key that no longer exists. The
  brief says touch nothing outside `lib/I18n/translations/`, so the spec has to
  either carve out an exception for those two lines or accept the staleness
  knowingly.

  On the merits, `STR_LOADING` should go: `STR_LOADING_POPUP` ("Loading",
  `english.yaml:35`) is live at six sites — `src/main.cpp:184,197`,
  `src/activities/home/HomeActivity.cpp:70`,
  `src/activities/util/BmpViewerActivity.cpp:99,195`,
  `src/activities/catalog/PublicationsActivity.cpp:73` — so nothing that wants a
  loading message is short of one.

## 12. What I could not verify

- **The build is not a blank in this note — it passed.** `pio run` on `c48e3ec7`
  with the YAMLs untouched:

  ```
  RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)
  Flash: [========  ]  81.1% (used 5314782 bytes from 6553600 bytes)
  ========================= [SUCCESS] Took 212.56 seconds =========================
  ```

  That is the **baseline the implementer compares against**, and it is expected
  to be *unchanged* after the edit: the 22 keys are already stripped before the
  generators run (§3), so a flash figure that moves at all means something other
  than this task's deletion happened.
- **Nothing on hardware.** No key on the removal list is reachable from any
  screen — that is what "unreferenced" means — so there is no device check this
  task can fail. Anyone offering a hardware sign-off for it has tested nothing.
- **Whether #39 will actually want `STR_HIGHLIGHTS_TOO_LARGE`** as opposed to a
  differently-worded key beside it. #39 is explicit that the decision is open;
  it is only explicit that #31 must not pre-empt it.
- **Whether the 17 keys in §6 are safe to delete.** They are inert in this
  build; I did not trace whether any is a translator's work-in-progress for a
  key about to be added to English.
