Tier: standard

# PR #144 review, pass 0: strip the EPUB optimiser and jszip from the file page

Reviewed: `gh pr diff 144` (HEAD `b237bf86`, base `e41880e3`) against issue #105, the design at
`docs/superpowers/specs/2026-09-27-issue-105-design.md` and the plan at
`docs/superpowers/plans/2026-09-27-issue-105-plan.md`.

What I ran myself:

- `git diff --diff-algorithm=histogram e41880e3 HEAD -- src/network/html/FilesPage.html`: 21 insertions
  and 4,043 deletions. Every inserted line is a kept line with conversion removed, or a comment
  reworded for the merged state (listed under Intent).
- The scratch checker (`check_filespage.py`) on the PR head reports `PASS` (116 names, 29 ids). On the
  primary checkout, which is at `e41880e3`, it fails checks 1, 2 and 8, as the PR says. Check 2 fails
  on both removed identifiers and removed ids.
- The orphan grep `git grep -n "jszip\|handleJszip\|JSZip" -- src lib scripts test docs ':!docs/superpowers'`
  returns nothing. A wider grep for "Optimize EPUB", "Rename from Book Metadata" and "Remember Settings"
  outside `docs/superpowers` also returns nothing.
- `wc -c src/network/html/FilesPage.html` gives 62,061 B, which matches the PR.
- Worker logs: `verify-build.log` shows `SUCCESS` and `Flash: 83.8% (used 5493970 bytes)`, and it has no
  `jszip_minJs.generated.h` line. `baseline-build.log` shows `84.8% (used 5557906 bytes)` and generates
  `jszip_minJs`. Both logs contain the same two warnings (`WebSocketsClient.cpp:573`,
  `CrossPointWebServerActivity.cpp:204`). So the PR's flash table and its "no new warnings" claim match
  the evidence.
- Removed CSS selectors: every removed rule that names a class the page still uses (`.modal`,
  `.upload-btn`, `.active`, `.success`/`.error`/`.warning`/`.info`) is a compound selector with an
  optimiser-only qualifier: `.modal.picker-mode`, `.upload-btn.optimize`, `.quality-preset.active`,
  `.log-entry.*`, `.log-tag.*`. That is the per-selector removal spec A9 asks for, with no
  over-deletion.

## Intent

**Issue acceptance criteria.**

- *Strip the optimiser and jszip from the page.* Met. The `<script src>` tag, the markup, the JS and the
  optimiser-only CSS are gone. `src/network/html/js/jszip.min.js` is deleted, along with its include,
  route and handler (`CrossPointWebServer.cpp`, formerly `:30`, `:142`, `:367-371`), the header
  declaration (`CrossPointWebServer.h`, formerly `:95`) and the endpoint row
  (`docs/webserver-endpoints.md`, formerly `:24`).
- *Check that uploads, including the dev firmware side-load, still work.* Statically this is as far as
  the PR can take it. `uploadFile` (`FilesPage.html:1580-1728`) matches the baseline line for line once
  conversion is removed. The WebSocket-then-HTTP fallback, the network-error cascade, the
  `operationCancelled`/`uploadGeneration` guards, collision suffixing and `originalFile` for retry are
  all unchanged. The side-load goes through `POST /upload`, and the diff leaves that route untouched.
  On-device confirmation is on the human checklist, as the spec's A8 intends, because no harness in
  this repo runs page JS. This is not a finding. It is the correct split between agent and human.
- *Put the flash difference in the PR.* Met: −63,936 B in `Flash: used` and in `firmware.bin`, with a
  breakdown per asset. The RAM delta is 0, and the PR gives the mechanism (`PROGMEM` arrays).

**Spec requirements.**

- Coupling points 1-8 are all implemented as specified:
  - `validateFile` is reduced to `has-files` plus the button state (`:1363-1369`).
  - `openUploadModal` keeps the path display and the `open` class (`:1123-1126`).
  - `closeUploadModal` keeps the guard and the input, button and progress resets (`:1146-1164`).
  - `DOMContentLoaded` keeps only the delegated `.image-preview-link` handler (`:1166-1174`).
  - The file-input click timer no longer calls `clearImagePicker()` (`:1291-1297`).
  - The drag-and-drop and `restoreAfterCancel` comments no longer mention the optimiser
    (`:1299-1302`, `:1143`).
  - The `hydrate` → `fetchVersion` call is gone. The checker's removed-name check covers
    `fetchVersion`.
- A7: the 50/50 progress split and the purple and orange colours are gone. The green bar, the red
  failure colour and the text formats stay (`:1655-1662`).
- A1 and A3, the user-visible removals, are called out in the PR body. So is the superseded clause of
  the 2026-09-14 spec.
- Non-goals are respected. The server upload path, `/api/status` and the "CrossPoint Reader" strings
  are not touched, and neither are the other pages.

**Scope.** Nothing is quietly dropped and nothing is added. The one structural change beyond
deletion: the redundant `if (failedFiles.length > 0)` wrapper, which sat inside the
`failedFiles.length !== 0` branch, is flattened. That is behaviour-preserving, and it is the plan's
`UPLOAD_FILE` text verbatim.

**Tests.** Spec A8 decides how this is verified: a scratch checker that is not committed, plus a device
checklist. That decision was made at spec review. The checker tests behaviour-relevant invariants: no
dangling id lookups, no unresolved calls, no unresolved inline handlers, no orphaned or missing CSS. It
derives its removed-name list from the baseline rather than restating the implementation. Its red and
green results reproduce.

**Plan divergence.** None found. The comment rewrites at `:1143`, `:1147`, `:1299-1302` and
`:1374-1375` are the plan's own text (plan `:336`, `:341`, `:385`, `:389`). The PR body follows
Step 6 item by item.

## Quality

- The server-side removal is modelled on `0d9285eb` (OPDS removal): route, handler, include and docs
  row go in one change, and the result is verified by orphan grep. That is the existing pattern.
- The kept code is untouched apart from what the strip required. It adds no second mechanism and no
  new helper.
- The PR adds no dead code and no commented-out code. The reworded comments describe the merged state,
  as `CLAUDE.md` ("Comments") asks. `build_html.py:56` no longer names a deleted file, and the script
  behaves the same.
- Error handling is unchanged: an alert when the existing-names fetch fails, then the fallback, the
  cascade and the banner.
- The stale gitignored `src/network/html/js/` directory is gone from the worktree. The committed tree
  carries nothing inert.

## Findings

None. I found no BLOCKER, MAJOR or MINOR finding that the evidence supports. The upload behaviour on
real hardware is still unverified, and the PR's checklist already hands that to the human.

VERDICT: CLEAR
