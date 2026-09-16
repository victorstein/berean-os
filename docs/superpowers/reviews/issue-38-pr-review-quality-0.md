# Stage 2 — code quality review, PR #41

Reviewed `gh pr diff 41 --repo victorstein/berean-os` against the codebase at
`/Volumes/stein/.herdr/worktrees/berean-os/refactor-38-remove-qr-display`
(branch `refactor/38-remove-qr-display`, working tree clean).

## Scope of the change

Pure deletion, six source files touched, no lines added to any `src`/`lib` file:

- `src/activities/reader/EpubReaderActivity.cpp:36,827-839(prev)` — include and
  `case DISPLAY_QR` block removed.
- `src/activities/reader/EpubReaderMenuActivity.cpp:77(prev)` — the
  `push_back({MenuAction::DISPLAY_QR, …})` row removed.
- `src/activities/reader/EpubReaderMenuActivity.h:26(prev)` — `DISPLAY_QR`
  enumerator removed.
- `src/activities/reader/QrDisplayActivity.{h,cpp}` — deleted outright.
- `USER_GUIDE.md:140(prev)` — the "Show page as QR" bullet removed.

## Verification performed independently

- `grep -rn "QrDisplayActivity\|DISPLAY_QR\|STR_DISPLAY_QR" src lib` — zero hits
  except `STR_DISPLAY_QR` in the i18n translation YAMLs, which the PR correctly
  attributes to a separate task (generated `I18nKeys.h`/`.cpp` are gitignored and
  regenerated from those YAMLs; leaving the key there does not reintroduce any
  reachable code path).
- `grep -rn "QrUtils\|qrcode.h" src lib` — only `src/util/QrUtils.{h,cpp}` and
  the three live callers in `CrossPointWebServerActivity.cpp:445,463,483`, which
  the PR correctly leaves alone (AC-3 in the PR body is right that
  `ricmoo/QRCode` can't come out of `platformio.ini:152` without touching that
  subsystem).
- Confirmed `MenuAction` in `EpubReaderMenuActivity.h:14-29` now has 15
  enumerators, and the `switch` in `EpubReaderActivity.cpp:722-865` handles 13 of
  them (`NIGHT_MODE`, `FRONTLIGHT`, `ROTATE_SCREEN` are handled in
  `EpubReaderMenuActivity.cpp:103,199` in-place) — matches the PR's own count and
  confirms the switch was never exhaustive, so the grep-based verification the
  PR relies on (13 references → 0) is in fact the only mechanical guard against
  a stray `case`/enumerator pair going out of sync. That argument holds up.
- `find test -iname "*qr*"` and `grep -rn QrDisplayActivity test/ CMakeLists.txt`
  — no hits; confirms the PR's claim that `test/` has no reader-menu surface to
  exercise, so "no test added" is a correct call, not an omission.
- `git status --short` — clean; nothing stranded, no leftover empty diff hunks,
  no dangling blank lines at the deletion sites in either `.cpp` file.

## Findings

### MINOR — pre-existing stale comment drifts further, left untouched (acknowledged in the PR body)

`src/activities/reader/EpubReaderMenuActivity.h:48-52` (the `MAX_MENU_ITEMS`
comment) says "13 unconditional items … reaches exactly 18 on an X4 Pro." It was
already wrong before this PR (true counts: 10 unconditional, 5 conditional); this
PR's deletion makes it wrong by one more. The PR body explains this was a
deliberate scope call: `EpubReaderMenuActivity.h` is a contested file shared with
other in-flight tasks, and the instruction was to touch only the lines this issue
owns rather than add unrelated churn to a shared header. That's a defensible
call for a file with declared merge contention, and the PR is transparent about
it rather than silently leaving debt. No action needed from this reviewer;
flagging only so whoever next touches this header knows the comment is now
stale by three, not two.

No BLOCKER or MAJOR findings. The diff mirrors the existing pattern for
removing a reader menu action exactly — enumerator, `push_back` row, switch
case, include, in that order — matches every sibling deletion's shape, contains
no dead code, no commented-out code, and no restated-the-next-line comments.
The "no test added" call is justified on the actual repository structure
(`test/` compiles nothing under `src/activities/`), not asserted without
checking.

---

VERDICT: CLEAR
