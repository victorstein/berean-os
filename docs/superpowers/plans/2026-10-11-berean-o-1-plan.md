# berean-o-1 (R20) — implementation plan: FAST, not HALF, entering Bible search and Publications

Spec: `docs/superpowers/specs/2026-10-11-berean-o-1-design.md`, which cleared review 0
(`docs/superpowers/reviews/berean-o-1-spec-review-0.md`). Read it first: this plan implements it and does not
re-argue it. Branch `perf/r20-fast-entry-refresh`, surface `ui`, owner decision d1 = (a).

FILES: src/activities/catalog/PublicationsActivity.cpp, src/activities/reader/BibleSearchActivity.cpp
FILES: src/activities/UiListActivity.h

No other file is touched. That rules out `test/CMakeLists.txt`, `src/main.cpp`, every
`lib/I18n/translations/*.yaml`, `BibleSearchActivity.h`, `UiListActivity.cpp`, `MeetingsActivity.cpp`,
`BibleNavigationActivity.cpp` and `freeink-sdk/` (spec A8, A12). The PR has no "For the orchestrator"
lines.

## Ground rules for the implementer

- **Re-read every file before editing it.** Sibling tasks in this run may have landed changes since
  this plan was written against `1ba1cd34`.
  - berean-o-6 (R11) edits `BibleSearchActivity.cpp` in `onExit`, `runBusyWork` and `openIndex`
    (spec, Risks).
  - If an `old` snippet below no longer matches, find the equivalent code and keep the sibling's
    work. Never overwrite it.
- **Code style.**
  - No comments that restate the code. A comment is only for a non-obvious *why*, and is written for
    the merged state, with no before/after narration (CLAUDE.md, "Comments").
  - The comment texts below are final. Use them as written.
- **Tools.**
  - `pio` is not on `PATH`: use `~/.platformio/penv/bin/pio`.
  - Never run two `pio run` at once in this checkout.
  - Never call `clang-format` directly. Use `./bin/clang-format-fix`, unsuffixed, over the whole tree.
- **Commits.**
  - Conventional messages, one commit per task that changes a file.
  - End every commit message with the line
    `Claude-Session: https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM`.
  - Never add a co-author trailer.
  - Stage paths explicitly (`git add <path>`), never `git add -A`.
- **There is no host-test step**, because no host suite compiles these files (spec A13). The "test"
  of this plan is the source invariant in Task 1, run red first and green last.

## Task 0 — bootstrap and baseline (3 min)

The worktree has no `.pio/` or `.venv/` yet (research, "Tool and package versions").

```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/perf-r20-fast-entry-refresh
git status --short                  # expect: empty
git branch --show-current           # expect: perf/r20-fast-entry-refresh
./bin/bootstrap                     # submodule + clang-format 21 into .venv/bin
git fetch -q origin
git log --oneline HEAD..origin/main -- src/activities/catalog/PublicationsActivity.cpp \
  src/activities/reader/BibleSearchActivity.cpp src/activities/UiListActivity.h
```

The last command prints nothing as of this plan. If it prints commits, a sibling has landed changes
to these files on `main`. Bring them in with `git merge --no-edit origin/main`, which does not rewrite
the pushed branch, and re-read the three files before Task 2.

Nothing to commit.

## Task 1 — the source invariant, run red (2 min)

This is the check the spec's testing strategy defines (step 1, as amended by review MINOR-2). It
lists every HALF request in `src/`, without line numbers, and diffs it against the merged state.

```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/perf-r20-fast-entry-refresh
diff <(grep -rn "halfRefreshPending.store(true)\|fullRefreshPending.store(true)\|fullRefresh=\*/true" src/ \
        | sed -E 's/^([^:]+):[0-9]+:[[:space:]]*/\1: /' | LC_ALL=C sort) - <<'EOF'
src/activities/catalog/PublicationsActivity.cpp: if (!mastheadCover_.empty() && mastheadCover_ != mastheadCover) halfRefreshPending.store(true);
src/activities/network/MeetingsActivity.cpp: halfRefreshPending.store(true);
src/activities/reader/BibleNavigationActivity.cpp: if (next == Level::Book && hasMasthead()) halfRefreshPending.store(true);
src/activities/reader/BibleSearchActivity.cpp: enterState(State::Ready, /*fullRefresh=*/true);
src/activities/reader/BibleSearchActivity.cpp: enterState(State::Starting, /*fullRefresh=*/true);
src/activities/reader/BibleSearchActivity.cpp: fullRefreshPending.store(true);
src/activities/reader/BibleSearchActivity.cpp: if (fullRefresh) fullRefreshPending.store(true);
EOF
echo "invariant exit=$?"
```

**Expected now, red.** Exit 1, with exactly these two extra lines:

```
1d0
< src/activities/catalog/PublicationsActivity.cpp: halfRefreshPending.store(true);
5d3
< src/activities/reader/BibleSearchActivity.cpp: enterState(State::Opening, /*fullRefresh=*/true);
```

The `1d0`/`5d3` hunk numbers may differ. The two `<` lines must not.

- If any **other** line differs, stop. A sibling changed a refresh choice, and this plan's expected
  list needs re-checking against spec A8 before going on.
- Keep this command at hand. Tasks 2, 3, 5 and 6 rerun it verbatim.

Nothing to commit.

## Task 2 — Publications enters with FAST (spec A1) (3 min)

Re-read `src/activities/catalog/PublicationsActivity.cpp` first. Then, in `PublicationsActivity::onEnter()`
(at `:52-61` as of `1ba1cd34`), make this edit.

old:

```cpp
void PublicationsActivity::onEnter() {
  UiListActivity::onEnter();
  refresh();
  // Entered from the launcher's cover tiles: one clean paint over them.
  halfRefreshPending.store(true);
  LOG_INF(MODULE, "Memory on entry: internal free %u (largest %u), PSRAM free %u",
```

new:

```cpp
void PublicationsActivity::onEnter() {
  UiListActivity::onEnter();
  refresh();
  LOG_INF(MODULE, "Memory on entry: internal free %u (largest %u), PSRAM free %u",
```

Do not touch the cover-change HALF in `refresh()`
(`if (!mastheadCover_.empty() && mastheadCover_ != mastheadCover) halfRefreshPending.store(true);`).

Rerun the Task 1 command. Expect exit 1 with only the
`enterState(State::Opening, /*fullRefresh=*/true);` line extra.

```bash
git add src/activities/catalog/PublicationsActivity.cpp
git commit -q -F - <<'EOF'
perf: enter Publications with a fast refresh

Home, its only origin, is 1-bit art with no gray pass, so a differential
refresh replaces it as it does for Continue, Go to, Tags and Settings.

Claude-Session: https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM
EOF
```

## Task 3 — Bible search's Opening frame is FAST (spec A2) (3 min)

Re-read `src/activities/reader/BibleSearchActivity.cpp` first. Then, at the end of
`BibleSearchActivity::onEnter()` (`:112-114` as of `1ba1cd34`), make this edit.

old:

```cpp
  // Loading the book names and open() are seconds of SD on the first search
  // after boot, so both wait for this frame.
  enterState(State::Opening, /*fullRefresh=*/true);
}
```

new:

```cpp
  // Loading the book names and open() are seconds of SD on the first search
  // after boot, so both wait for this frame.
  enterState(State::Opening);
}
```

`enterState` declares `bool fullRefresh = false` (`BibleSearchActivity.h:177`), so the header does not
change.

Rerun the Task 1 command. **Expect green: no output and `invariant exit=0`.** Also run:

```bash
grep -n "enterState(State::Opening" src/activities/reader/BibleSearchActivity.cpp
```

Expect exactly one line, ending in `enterState(State::Opening);`.

```bash
git add src/activities/reader/BibleSearchActivity.cpp
git commit -q -F - <<'EOF'
perf: paint Bible search's opening frame with a fast refresh

The index open waits for this frame, so the HALF it no longer pays
(~950 ms on UC8179) comes straight off the time to the keyboard.

Claude-Session: https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM
EOF
```

## Task 4 — a reason on every HALF left in Bible search (spec A4–A7) (5 min)

All four edits are in `src/activities/reader/BibleSearchActivity.cpp`. They are comments only.
Re-read each spot before editing it.

**4a. `showFailure` (spec A6).** As of `1ba1cd34` this is `:243-252`.

old:

```cpp
    failure = reason;
    state = State::Failed;
  }
  fullRefreshPending.store(true);
  requestUpdate();
}
```

new:

```cpp
    failure = reason;
    state = State::Failed;
  }
  // Closes the build's progress view with a clean paint, as Ready does.
  fullRefreshPending.store(true);
  requestUpdate();
}
```

**4b. Ready, at the end of `finishBuild()` (spec A5).** As of `1ba1cd34` this is `:372-373`.

old:

```cpp
  // Ready's work opens the new index, and its frame stays up while it does.
  enterState(State::Ready, /*fullRefresh=*/true);
}
```

new:

```cpp
  // Ready's work opens the new index, and its frame stays up while it does.
  // A clean paint over the progress view, which only FAST repaints have
  // touched since Starting.
  enterState(State::Ready, /*fullRefresh=*/true);
}
```

**4c. Starting, in `handleDialogInput()` (spec A4).** As of `1ba1cd34` this is `:442-445`.

old:

```cpp
    cancelRequested = false;
    goHomeAfterCancel = false;
    enterState(State::Starting, /*fullRefresh=*/true);
    return;
```

new:

```cpp
    cancelRequested = false;
    goHomeAfterCancel = false;
    // The build's first clean paint: its progress view stays up for the
    // whole build.
    enterState(State::Starting, /*fullRefresh=*/true);
    return;
```

**4d. The `render()` header comment (spec A7).** As of `1ba1cd34` this is `:1011-1015`.

old:

```cpp
void BibleSearchActivity::render(RenderLock&&) {
  // Mirrors UiListActivity::render(), with the theme's progress bar drawn over
  // the app and the refresh mode chosen per frame: a full refresh on entering
  // the screen, the progress view and completion; fast ones in between.
```

new:

```cpp
void BibleSearchActivity::render(RenderLock&&) {
  // Mirrors UiListActivity::render(), with the theme's progress bar drawn over
  // the app and the refresh mode chosen per frame: a HALF when a build starts,
  // completes or fails; FAST otherwise, entering the screen included.
```

Rerun the Task 1 command. Expect it still green (exit 0): comments do not change it.

```bash
git add src/activities/reader/BibleSearchActivity.cpp
git commit -q -F - <<'EOF'
docs: give each remaining Bible search HALF its reason

Starting, Ready and a failure keep their clean paint; the render
comment now states the per-frame rule as it is.

Claude-Session: https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM
EOF
```

## Task 5 — `UiListActivity.h`'s flag comment states the mechanism only (spec A8, review MAJOR-1) (2 min)

Re-read `src/activities/UiListActivity.h`. As of `1ba1cd34` this is `:85-89`.

old:

```cpp
  // The next paint is a HALF refresh instead of FAST, then it resets. Set it
  // where a paint replaces cover art with something else, which a
  // differential refresh would leave ghosted. Mirrors BibleSearchActivity's
  // fullRefreshPending.
  std::atomic<bool> halfRefreshPending{false};
```

new:

```cpp
  // The next paint is a HALF refresh instead of FAST, then it resets. Each
  // setter says why. Mirrors BibleSearchActivity's fullRefreshPending.
  std::atomic<bool> halfRefreshPending{false};
```

Each remaining setter already carries its own reason:

- `MeetingsActivity.cpp` (`// Entered from the launcher's cover tiles: one clean paint over them.`);
- `BibleNavigationActivity.cpp` (`// Back from the chapter grid replaces the cover art with paper, …`);
- `PublicationsActivity.cpp` (`// A delete or a download can swap the cover or remove it; …`).

Leave all three alone.

Rerun the Task 1 command. Expect it still green.

```bash
git add src/activities/UiListActivity.h
git commit -q -F - <<'EOF'
docs: state what halfRefreshPending does, not when to set it

Each setter carries its own reason; the base comment no longer
prescribes a HALF for every paint over cover art.

Claude-Session: https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM
EOF
```

## Task 6 — format, build and final checks (5 min, mostly build time)

```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/perf-r20-fast-entry-refresh
./bin/clang-format-fix              # the whole tree, as CI runs it
git status --short                  # expect: empty
```

- If `clang-format-fix` changed one of the three files, look at the diff. Commit it as
  `style: format R20 edits`, with the session trailer.
- If it changed any other file, the base was already unformatted. Do not commit those changes:
  restore them with `git checkout -- <path>` and mention them in the PR.

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee "${TMPDIR:-/tmp}/r20-build.log" | tail -5
grep -n "warning:" "${TMPDIR:-/tmp}/r20-build.log" | grep -E "PublicationsActivity|BibleSearchActivity|UiListActivity" || echo "no warnings in touched files"
```

- **Expected:** `[SUCCESS]`, and "no warnings in touched files".
- **A first build after bootstrap compiles the whole framework.** Allow several minutes, with a
  600000 ms tool timeout.
- **If the build fails,** fix it in the file the error names. Do not change the plan's behaviour.
  Commit with `fix: …`.

Rerun the Task 1 invariant one last time (expect exit 0), then confirm the scope:

```bash
git diff --stat 1ba1cd34..HEAD -- src/ lib/ freeink-sdk test/
```

Expect exactly three files:
- `src/activities/UiListActivity.h`;
- `src/activities/catalog/PublicationsActivity.cpp`;
- `src/activities/reader/BibleSearchActivity.cpp`.

If the Task 0 merge brought in sibling changes, compare against `origin/main` instead:
`git diff --stat origin/main...HEAD`.

```bash
git push
```

Host tests are not run: no file they compile changed (spec A13).

## Task 7 — the PR (implement phase, after the verification above) (5 min)

The worker opens the PR against `main` only when its implement-phase prompt says to.

- **Title:** `perf: enter Bible search and Publications with a fast refresh`
- **Before writing the body:** decide which end-to-end search figure to quote.

  ```bash
  git log --oneline 1ba1cd34..origin/main -- src/activities/reader/BibleSearchActivity.cpp src/study/
  ```

  - If that shows no berean-o-6 (R11) commit, quote the `1ba1cd34`-baseline estimate below.
  - Otherwise, replace the end-to-end search line with "measured on device against `main` at
    `<sha>`". R11 changes the index-open time, so the estimate no longer holds (spec Goal 5,
    review MINOR-4).

Body, filled in as written:

````markdown
Bible search and Publications now paint their first frame with a FAST refresh instead of a HALF.

## Why

On the owner's UC8179 a HALF is 1,506 ms and a FAST ~560 ms (device baseline, 2026-10-11, `1ba1cd34`).
Neither HALF was added for an observed ghost (#94 used "the codebase's clean refresh"; #204 said entry
"may" use one), and the owner chose FAST from every origin (decision d1).

- **Bible search:** the index open waits for the first frame (`BibleSearchActivity.cpp`, `runBusyWork` is
  gated on `shownState`), so the saving comes straight off the time to the keyboard.
- **Publications:** entered only from Home, whose art is 1-bit with no gray pass — the same differential
  update Continue, Go to, Tags and Settings already make over it.

## What changed

- `PublicationsActivity::onEnter` no longer sets `halfRefreshPending`.
- `BibleSearchActivity::onEnter` enters Opening without a full refresh.
- The HALFs that remain in Bible search — Starting, Ready, a failure — each carry a one-line reason, and
  the `render()` comment states the rule.
- `UiListActivity.h`'s `halfRefreshPending` comment states the mechanism; each setter keeps its own reason.
- No other screen's refresh choice changes: Meetings, the grid's Chapter → Book and Publications' cover
  change keep their HALF. No SDK or driver change. No new allocation (nothing allocated, so nothing to
  justify), no store, no format, no translation keys.

Design: `docs/superpowers/specs/2026-10-11-berean-o-1-design.md`. Plan:
`docs/superpowers/plans/2026-10-11-berean-o-1-plan.md`. Research:
`docs/superpowers/research/2026-10-11-berean-o-1-research.md`.

## Before and expected after (UC8179)

| | Before (baseline) | Expected after |
|---|---|---|
| Entry frame `Wait complete:  8179_DRF` | 1,506 ms (HALF) | ~560 ms (FAST) |
| Bible search → keyboard | ~2.7 s; 4.6 s first after boot | ~1.75 s; ~3.65 s |
| Publications entry, settled | 1,940 ms | ~990 ms |

The after column is an estimate (baseline minus ~950 ms). No device run has been made for this PR.

## Verified

- Source invariant (plan Task 1): every HALF request left in `src/` matches the expected seven lines.
- `pio run -e x4pro`: SUCCESS, no warnings in the touched files.
- `./bin/clang-format-fix` over the whole tree: clean.
- Host tests not run: no file they compile changed.

## Device checks (owner)

Flash this PR's `x4pro` dev build and capture serial with `cat /dev/cu.usbmodem*`. A FAST and a HALF
log the same `Wait complete:  8179_DRF (N ms)` line; only N tells them apart. On SSD1677 the line is
`Wait complete: refresh (N ms)`.

1. **Publications from Home.** The first `Wait complete` after `[PUBS] Memory on entry:` reads ~560 ms,
   not ~1,506. Look for any ghost of Home's covers or tiles.
2. **Search from Home's Search tile.** The first `Wait complete` after `[READER] Book loaded in` reads
   ~560 ms. Look for a ghost of Home.
3. **Search from the reader menu, AA off.** The first `Wait complete` after `[MENU] heap closed:` reads
   ~560 ms. Look for a ghost of the page or the sheet.
4. **Search from the reader menu, text anti-aliasing on.** Opening the menu logs `8179_gray_pre_DRF`
   (~674 ms); the search entry is then a plain ~560 ms `8179_DRF`. Look for gray fringes from the page.
5. **Unchanged:** Meetings still enters at ~1,506 ms; Chapter → Book on the grid still takes one HALF;
   preparing an index still shows a HALF on "Preparando…" and on "Listo".

If any ghost shows, do not merge as is: the fallback is a HALF only for the Home-origin entries
(Publications, and search opened with `CancelTo::Page`).

## For the orchestrator

None — no shared-file lines.

https://claude.ai/code/session_01BKhtSZjh1TanRUgr3K2qzM

Refs berean-o-1
````

The pipeline requires `Refs berean-o-1` to be the body's last line, so the session link goes above it.
