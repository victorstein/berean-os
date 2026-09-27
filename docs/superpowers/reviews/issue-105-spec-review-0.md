Tier: standard

# Issue #105 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-105-design.md`, checked against issue #105
(`gh issue view 105 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-27-issue-105-research.md`, and the tree at `640dfae5`.
`git diff --stat e41880e3 HEAD` touches only the two docs, so the spec's `e41880e3` line numbers
still apply to the code.

## What I verified and found sound

- Server side. The include is at `CrossPointWebServer.cpp:30`, the route at `:142`, `handleJszip` at
  `:367-371`, the declaration at `CrossPointWebServer.h:95`, the doc row at
  `docs/webserver-endpoints.md:24`, and the comment at `scripts/build_html.py:56`. All match the spec.
  `/api/status` has one other consumer, `HomePage.html:142`, so A4 holds.
- Page structure. jszip loads at `:7`, the markup block runs from `:1565` to `:1744`, `convertWarning`
  is at `:1746-1749`, `startConversionBtn` is at `:1751`, and the log section is at `:1758-1764`. The
  removed JS ranges begin and end where the spec says: `:2326/2327`, `:2968`, `:3124-3140`, `:3283`,
  `:3390`, `:3392-3399`, `:3401`, `:3810/3811`, `:3942`, `:3979` and `:5310`.
  `reserveAvailableUploadFilename`/`fetchExistingUploadNames` (`:3943-3977`) sit in a gap, as the
  spec states.
- A1 holds. `getMetadataFilenameForEpub` depends on `JSZip.loadAsync` (`:3897-3898`), and the default
  is `renameFromMetadata: false` (`:3325`). The issue's "rename" means `POST /rename`, and the upload
  keeps collision suffixing. This is a sound consequence of an explicit issue requirement, not a
  judgment left to the human.
- The side-load claim holds. `needsConversion = isEpub && convertEnabled` (`:5577-5578`), so `.bin`
  never reached the optimiser, and `SdFirmwareUpdateActivity.cpp:25-26` does not read the page.
- I ran a scratchpad script that collects every top-level `function`/`let`/`const` defined in the
  spec's removed JS ranges (118 names) and greps the kept lines for them. The hits at `:1938`,
  `:2113-2126`, `:2185-2204`, `:2993-3017`, `:3231-3265` and `:5513-5733` all fall inside coupling
  points 1-6. The hit at `:3151` is the one exception (see MAJOR 1). The retry banner, the WS/HTTP
  upload functions and the folder/delete/rename/move code have no references to removed names.
  Every inline handler in the kept markup (`:1519-1831`) names a kept function.

## Findings

### MAJOR 1: the coupling map misses `clearImagePicker()` in `setupFileInputListener`

- **Claim.** The spec removes `clearImagePicker` (`:3124-3140`, spec line 118). For the file-input
  IIFE, the only coupling point is "8. The drag-and-drop comment (`:3156-3159`)" (spec line 148).
- **Problem.** The same IIFE also calls the removed function. `FilesPage.html:3147-3154` is:
  ```
  fileInput.addEventListener('click', function() {
    setTimeout(() => {
      if (fileInput.files.length === 0) {
        clearImagePicker();
        document.getElementById('uploadBtn').disabled = true;
  ```
  If the spec is followed exactly, every click on the drop zone or file input with nothing selected
  throws `ReferenceError: clearImagePicker is not defined` from the timer. That is the first thing a
  user does in the upload modal, because the drop-zone click calls `fileInput.click()` at `:3179`. The
  throw also skips the `uploadBtn.disabled = true` line after it. The device checklist item
  "devtools console shows no errors" would fail. None of the six static checks catches it (see MAJOR 2).
- **Evidence.** My reference scan reports `3151 clearImagePicker | clearImagePicker();`, which is
  outside every coupling point the spec lists.
- **Fix.** Add coupling point 9: "`setupFileInputListener` click handler (`:3147-3155`). Drop the
  `clearImagePicker()` call and keep `uploadBtn.disabled = true`." Coupling point 8 can then become
  "the `setupFileInputListener` IIFE (`:3143-3218`)", covering the call and the comment.

### MAJOR 2: the static check cannot detect a call to a removed function from JS, which the spec says is its purpose

- **Claim.** Spec lines 189-192 say a call to an undefined function "throws on the first upload or
  on closing the modal. No build step catches either … The static check in the testing strategy
  exists for this."
- **Problem.** None of the six checks (spec lines 200-210) detects a JS-to-JS call to a deleted
  function:
  - Check 2 lists only ten names. It includes `convertEpubFile`, `imageStates` and `fetchVersion`,
    but not `clearImagePicker`, `logError`, `showLog`, `clearLog`, `startBatchLog`,
    `finalizeBatchLog`, `saveToFileBatchLog`, `exportLogToFile`, `updateBatchModeUI`,
    `toggleConvertOptions`, `showImagePicker`, `applyUploadSettings`,
    `restoreUploadSettingsFromStorage`, `updateQualitySettings`, `maybeRenameEbookFile`,
    `suppressUploadSettingsSave`, `exportLogCheckbox`, `epubImagesCache` or
    `autoCropProtectedPaths`. Every one of these is referenced from a kept line today.
  - Check 3 covers element ids only.
  - Check 4 covers inline `on*=` attributes only.
  - Check 6 (`node --check`) checks syntax only and does not resolve names.

  So a partial strip, such as the `:3151` case above or one missed `logError(` in the
  `uploadFile` catch at `:5733`, passes red/green. That is the failure mode the research note
  (section "Nothing validates this HTML") and `0d9285eb` both warn about.
- **Evidence.** In my scan, 30+ kept-line references to removed names are not covered by check 2's
  list.
- **Fix.** Replace check 2's hand-written list with a derived one. Take the names of every
  `function X` and top-level `let|const X` in the spec's removed ranges of the *baseline* page (a
  scripted extraction gives 118 names). Assert that none of them appears as an identifier in the
  stripped script, matching `log` as `(?<![.\w])log\(` so that it does not match `console.log`. As
  an alternative or addition, check that every bare `name(` call in the script resolves to a
  function declared in the script or to a short browser-global allow-list.

### MINOR 1: the `validateFile` range reaches into the upload state globals

- **Claim.** Coupling point 2 cites `validateFile` as `:3220-3280` and says it "keeps … nothing
  else" (spec lines 136-137). The research note repeats the range.
- **Problem.** The function ends at `:3268`. Lines `:3270-3281` declare `failedUploadsGlobal`,
  `isUploadInProgress`, `operationCancelled`, `uploadGeneration`, `currentUploadWs`,
  `currentUploadXhr`, `HTTP_PORT`, `WS_PORT` and `WS_CHUNK_SIZE`, and every one of them is kept
  upload state. An implementer who rewrites the cited range deletes all of them, and after MAJOR 2's
  fix only a name-resolution check would notice.
- **Fix.** Cite `validateFile` as `:3220-3268`.

### MINOR 2: the orphan grep cannot "return nothing" as written

- **Claim.** Spec line 222: `grep -rn "jszip\|handleJszip\|JSZip" src lib docs scripts test` returns
  nothing.
- **Problem.** Run today, the same grep also matches files that the change leaves in place:
  - `src/network/html/js/jszip_minJs.generated.h`. It is gitignored (`.gitignore:11`) and stays on
    disk in any tree that has built. A6 calls it inert, which is true, but grep still finds it.
  - `docs/superpowers/research/2026-09-27-issue-105-research.md` and this spec.
  - `docs/superpowers/specs/2026-09-14-publication-download-design.md:141`.
- **Fix.** Scope the check to tracked, live files: `git grep -n "jszip\|handleJszip\|JSZip" -- src
  lib scripts test docs ':!docs/superpowers'`. Also tell the implementer to delete the stale local
  header (or run `rm -rf src/network/html/js`) before the after-build.

### MINOR 3: the change silently supersedes a statement in a settled spec

- **Claim.** The spec treats the optimiser as having no users that matter.
- **Problem.** `docs/superpowers/specs/2026-09-14-publication-download-design.md:156-159` records
  "Decided: accept the raw archive … The web-UI optimizer stays available for when it is wanted".
  That decision stands on its own, because it was confirmed by reading `lff` raw on the device. The
  "stays available" clause becomes false, though, and the #105 spec does not mention it. Issue #105
  asks for the removal explicitly, so this is not a new scope question.
- **Fix.** Add one sentence to Problem or Non-goals: the 2026-09-14 download spec's raw-archive
  decision stands without the optimiser, and its "stays available" note is superseded. Mention the
  same in the PR description. Whether to annotate the old spec is optional.

### MINOR 4: small internal inconsistencies and citation drift

- Spec line 94 says the change "touches four files and deletes one", but the table below it lists
  five edited files plus one deletion.
- Check 3, applied to the stripped page, fails on `getElementById('notification')`. That element is
  created in JS (`:1882-1885`), not in the markup, so "all six must pass" (spec line 212) needs a
  dynamic-id allow-list for check 3 too, like check 5's.
- A9 cites `.picker-columns.picker-active` at `:191`. It is at `:193`.
- The non-goal about brand strings cites `:1504` and `:1849` and omits `<title>` at `:6`.

These MAJORs are mechanical corrections to the spec. They reverse no decision, change no scope and
need no human judgment, so they are to be fixed inline.

VERDICT: CLEAR
