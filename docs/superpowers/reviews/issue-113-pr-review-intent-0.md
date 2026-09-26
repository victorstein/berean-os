# PR #117 intent review 0 (issue #113)

Reviewed: `gh pr diff 117` / `git diff main...HEAD` on `fix/113-i18n-font-download-sd-error` (HEAD `c1842433`, merge base `2f303f6f`).
Against: issue #113, the spec at `docs/superpowers/specs/2026-09-26-issue-113-design.md`, and the plan at `docs/superpowers/plans/2026-09-26-issue-113-plan.md`.

## Issue acceptance criteria

| Issue asks | Evidence | Met |
|---|---|---|
| `FontDownloadActivity.cpp:92,102,113,120,148,295,432` stop drawing literals | `FontDownloadActivity.cpp:92, 102, 113, 120, 148, 295, 440` now assign `tr(STR_FONT_*)`. `grep 'errorMessage_ = "'` finds nothing. | Yes |
| `main.cpp:405` `"SD card error"` goes through `tr()` | `src/main.cpp:405` now `tr(STR_SD_CARD_ERROR)` | Yes |
| New `STR_*` keys in `english.yaml` | `english.yaml:225`, `:362-371` | Yes |
| ... and in `spanish.yaml` at least | `spanish.yaml:216`, `:345-354`. `gen_i18n.py -v` reports none of the 11 new keys as missing in ES (rerun during this review). | Yes |
| Regenerate with `gen_i18n.py` | Generator exits 0 with `String keys: 486` (rerun during this review). Generated headers are not committed. `git status` is clean. | Yes |
| Remember that comments are scanned | No `STR_` names were added in comments. Every new name is a real key. | Yes |

## Spec requirements

- **12 sites, 11 keys (spec "The keys").** All 12 are rewritten. `:113` and `:148` share `STR_FONT_MANIFEST_INVALID` (A2). The English and Spanish values in both YAML files match the spec table exactly, key by key.
- **English is byte-identical (Goal, A4).** Each English value equals its old literal. The four `%s` keys give the same text as the old `"literal" + file.name` (for example `"Download failed: %s"` in place of `"Download failed: " + file.name`).
- **`snprintf` into `char message[128]`, with `file.name` passed as an argument and never as the format (A3).** `FontDownloadActivity.cpp:358-360, 372-374, 384-386, 398-400`. Each sits inside the `RenderLock` scope the site already had. `state_ = ERROR` still comes first.
- **Locking unchanged (Non-goals).** The manifest sites at `:92-148` are still outside the lock, as the spec requires.
- **Spanish widths (A4, A7).** The short forms "Archivo no válido: %s" and "Suma no coincide: %s", plus "No se pudo verificar: %s", are the values committed. They were not expanded back.
- **SD error always shows in English (A5).** The PR body states this and does not claim a Spanish SD-error screen.
- **No new host test (A6).** This matches the spec. The spec's red/green generator run is reported in the PR body, with key counts 475 → 476 → 480 → 482 → 486.

## Plan conformance

The four fix commits match plan Tasks 1-4 one to one: grouping, commit messages, key order and anchor placement. The Task 5 checks are all reported in the PR body: literal grep, ES fallback, `pio run`, host suite and full-tree format. The Task 6 body covers every bullet the plan lists, including the A1 shared-file note and the `Closes #113` line. The PR body explains the one deviation from the plan's pre-flight: the linker crashed once (signal 11) and linked cleanly on an identical rerun. That does not change intent.

## Scope

- **Expansion:** 4 sites beyond the 7 the issue listed (`:358, 370, 380, 392`). They are the same defect, on the same `errorMessage_` field, on the same screen. The spec justifies them, and the PR body calls them out. This finishes the issue's evident intent ("`errorMessage_` literals ... drawn to the screen"). It does not creep into new scope.
- **Reduction:** none. The only thing the spec leaves out is the other 30 languages, and the issue permits that ("`spanish.yaml` at least").
- **Not touched, correctly:** `FontDownloadActivity.cpp:660-661` builds the DOWNLOADING status line from the translated `tr(STR_DOWNLOADING)` plus hardcoded punctuation (`" ("`, `"/"`, `")"`). The issue did not name it and it holds no English words, so leaving it alone is the right call for this PR.

## Findings

None. I found no BLOCKER, MAJOR or MINOR.

The code change contains no test beyond the generator. The spec accepts that deliberately (A6), and the spec review accepted it too. For missing keys the generator is a real behavioural gate: it fails the build when source references a key that has no English value. Only the device checks listed in the PR body can verify the on-screen Spanish rendering and fit.

VERDICT: CLEAR
