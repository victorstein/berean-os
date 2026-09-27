Tier: standard

# Issue #105 — plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-27-issue-105-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-105-design.md`
Baseline: `e41880e3` (worktree `FilesPage.html` sha256 `c07fbd87…229e`, which matches the plan's Step 0).

## What was verified by running it

The two scripts were extracted verbatim from the plan (lines 77-252 and 297-596). Their sha256
values match the plan's pinned `e3ccfa3b…ab9f7` and `f84ec9f9…3e1`. They were run against a
**copy** of the baseline page in the review scratchpad, never against the repo page.

- Red run on the baseline: `removed-name list: 116 names, 29 ids`, then checks 1 and 2 fail and
  3 check(s) failed, exit 1. This matches plan lines 263-268.
- Stage `js`: `217924 -> 81356`, sha `071f9f9e…bcec`, then only check 1 fails. This matches plan
  lines 627-633.
- Stage `css`: `81356 -> 62221`, sha `c2ffbd21…6a41`, then only check 1 fails. This matches plan
  lines 680-686.
- Stage `jszip-tag`: `62221 -> 62178`, sha `0c2a5592…71fd`, then `PASS`. This matches plan lines
  710-715.

The plan's expected outputs are real and reproducible.

The following were also checked independently of the plan's checker:

- **Every edit boundary against the baseline text.** 1564/1752, 1757/1764, 1936/1940, 2111/2131,
  2155/2158, 2160/2206, 2206/2969, 3024, 3122/3142, 3219/3269, 3282/3943, 3974/5312 and 5470/5755
  all land on the intended first and last lines, with one exception (MAJOR 1).
- **`uploadFile` replacement against baseline 5471-5754**, diffed line by line. Only the
  conversion, batch-log and metadata-rename lines and the 50% split are gone. The one other change
  folds the always-true `if (failedFiles.length > 0)` in the `else` of
  `failedFiles.length === 0` into its body, which does not change behaviour. The WebSocket-to-HTTP
  fallback, the network-error cascade, the `operationCancelled` guards, collision renaming, the
  colours `#27ae60` / `#4caf50` / `#e74c3c` and the text formats are unchanged (spec coupling
  point 1, A7).
- **CSS.** The stripped `<style>` has balanced braces. Every selector in the six removed CSS ranges
  that names a still-used class (`.active`, `.error`, `.visible`, `.upload-btn`, `.modal` and so
  on) is compounded with a removed class, for example `.rotation-btn.active`,
  `.log-entry.error` or `.upload-btn.optimize`. No kept element loses styling. Classes styled but
  used nowhere drop from 13 in the baseline to 1 (`delete-item-name`, pre-existing, and the plan
  keeps it deliberately at line 670). So spec A9 holds in both directions.
- **Server, header, doc and build-script `old` strings** (plan 4c-4f) match
  `CrossPointWebServer.cpp:29-31`, `:141-142`, `:367-373`, `CrossPointWebServer.h:94-96`,
  `docs/webserver-endpoints.md:24` and `scripts/build_html.py:56` exactly.
- **Orphan search.** `git grep -il jszip -- ':!docs/superpowers'` lists exactly the six files the
  plan edits or deletes. No `.github`, simulator or root file references jszip.
- **Other checks.** `build_html.py` prints `Generated:` and `Compressed:` (`:90-93`), so Step 4h's
  filter sees them. The branch has an upstream (`origin/refactor/105-…`), so the bare `git push`
  works. The host-test commands match CI (`ci.yml:188,191`) apart from the generator, and
  `build/` is gitignored.

Spec coverage: Goals 1-4, A1-A9, coupling points 1-8, the orphan grep, the stale-`js/` deletion,
the build and warning expectation, clang-format, host tests, the device checklist and the
superseded-clause note each map to a step (Steps 2, 3, 4a-4j, 5 and 6). The `FILES:` lines (plan
8-10) sit at column 0, outside any fence. They cover every path any step touches, including the
`src/network/html/js/` prefix for `git rm` / `rm -rf`.

## Findings

### MAJOR 1 — the drag-and-drop comment edit is off by one and ships a duplicated comment line

- **Claim.** Stage `js` edit `(3147, 3159, FILE_INPUT_CLICK_AND_DROP_COMMENT)` (plan line 561, and
  the table row at 612) replaces the file-input click handler and rewrites the four-line
  drag-and-drop comment.
- **Problem.** The baseline comment spans **3157-3160**, not 3156-3159. 3156 is the blank line
  after `});` at 3155, and 3161 is `const dropZone = …`. The spec's citation `:3156-3159`
  (spec line 156) was already off by one, and the plan inherited it. The replacement's four comment
  lines are therefore followed by the surviving baseline line 3160. Because the three `*_SHA256`
  constants pin this output, an implementer who follows the plan literally cannot fix it without
  tripping the stage guard.
- **Evidence.** Running the plan's own stage `js` on a baseline copy gives stripped-page lines
  1297-1301:

  ```
      // Drag-and-drop: route dropped files through the existing file input so the
      // normal validateFile() pipeline runs unchanged. Assigning input.files via
      // DataTransfer is supported in all current Chromium/Firefox/Safari, which is
      // what the device serves the page to.
      // current Chromium/Firefox/Safari, which is what the device serves the page to.
  ```

  The last line is baseline 3160, which is left behind. It does not break anything at runtime, but
  it ships a garbled comment into the product. That violates `CLAUDE.md` "Comments" and the plan's
  own "keep them verbatim" promise (plan 27-29). The checker cannot see it, because checks 2 and 7
  strip comments.
- **Fix.** Make four changes:
  1. Change the edit to `(3147, 3160, FILE_INPUT_CLICK_AND_DROP_COMMENT)` and the table row to
     `3147-3160`.
  2. Re-pin the constants: `AFTER_JS_SHA256 = "e0c0640a7fc47b2b39edd91663583b6f31af70d56b0c24dc64544832826c3ea2"`
     and `AFTER_CSS_SHA256 = "d46c3dc7209eae3248eeb9a69d3dc2b45d014ca1ba1c63e480614883a92fa4c7"`.
  3. Update the expected outputs to `stage js: 217924 -> 81271`, `stage css: 81271 -> 62136`
     and `stage jszip-tag: 62136 -> 62093 bytes, sha256 d4cbf0de3900891bae63fd964bcc795c4843e217a4b3aaddb257e43f1d53449f`.
     The checker still prints `PASS`.
  4. Re-pin the strip script's sha. With only this change it becomes
     `c28d2737caed2b31adebac64dfd1a886f5cbbda16bfd06e96f763265fc336a94`.

  All of this was verified in the review scratchpad. If MINOR 1 is applied as well, use the
  combined values given there instead.

  This is mechanical and changes no decision, so the fix belongs inline.

### MINOR 1 — two kept globals still describe the removed conversion

- **Claim.** Spec coupling point 2 keeps the upload globals at `:3270-3281`, and the spec says
  comments describe the merged state (spec 158-159).
- **Problem.** Baseline 3272-3273 survive with `// Prevent modal close during upload/conversion`
  and `// Set by Cancel to stop conversion loops and upload async handlers`. After the strip there
  is no conversion and no conversion loop.
- **Evidence.** In the stripped page, line 1371 still reads "stop conversion loops". The remaining
  uses of `operationCancelled` are in the cancel handler, in `restoreAfterCancel` and in the
  `uploadFile` catch.
- **Fix.** Add `(3272, 3273, …)` to stage `js`, replacing the two lines with
  `let isUploadInProgress = false; // Prevent modal close during upload` and
  `let operationCancelled = false; // Set by Cancel to stop upload async handlers`.

  Combined with MAJOR 1 (verified, and the checker still `PASS`es):
  - `AFTER_JS_SHA256 = "54f9a03da9280b60f148278eaccb6ce7a69d5d78916e2ef8ccdc493a8dfc8c9f"`
    (`217924 -> 81239`)
  - `AFTER_CSS_SHA256 = "e3acf85e5b7e1d65432c53269e4992bdf99faba6aed95b3a13eb1375849bf652"`
    (`81239 -> 62104`)
  - final `11ec187c85fb5c7ae8bf47d888c9e72686fdb2d1ed2eda1fe96c6ea2aec8d352` (`62104 -> 62061`)

  Re-pin the strip script's own sha after editing it.

### MINOR 2 — Step 3 has no red, and the checker never asserts that the optimiser CSS is gone

- **Claim.** Every step should start with a failing test. Spec Goal 1 requires that the page
  contain "no optimiser code, markup or CSS".
- **Problem.** Step 3 runs the checker only as a regression guard (plan 688). None of checks 1-7
  fails before the CSS strip and passes after it: check 5 catches over-deletion only. What
  guarantees under-deletion today is the pinned sha. That is sound as far as it goes, but no step
  tests Goal 1's CSS clause.
- **Evidence.** After stage `js` the checker prints only `FAIL 1`, and the same after stage `css`
  (plan 627-633 and 680-686). An independent count of classes that are styled but used nowhere
  gives 13 on the baseline, 12 after `js` (the optimiser rules, plus the pre-existing
  `delete-item-name`) and 1 after `css`.
- **Fix.** Add a check 8 to `check_filespage.py`: "no class in `<style>` that is used nowhere else
  in the page, beyond the baseline's own unused set minus the optimiser's". The simplest form is an
  allow-list of `{'delete-item-name'}`. Step 3 then has a real red (after Step 2) and a real green.
  Re-pin the checker's sha.

### MINOR 3 — small textual slips

- Plan line 65-66: "match **Step 5**'s `old` strings". The server and doc `old` strings are in
  Step 4 (4c-4f). Step 5 is host tests.
- Plan line 889-890: Step 6 says "open the PR" but gives no command. Per `CLAUDE.md` "Repository
  context", `gh` needs `--repo victorstein/berean-os`. Give the literal
  `gh pr create --repo victorstein/berean-os --base main --title "refactor: strip the EPUB optimiser and jszip from the file page" --body-file …`,
  as sibling plans do (for example `2026-09-26-issue-133-plan.md:318`).

## Not findings (checked and sound)

- The checker subtracts baseline sets in checks 3, 4, 5 and 7 instead of hand-kept allow-lists.
  That is equivalent to the spec's allow-list wording and more robust. The mutation proofs
  (plan 283-287) are plausible, and checks 2 and 7 do share the removed-name derivation.
- Step 2's commit leaves the jszip tag and route in place. The page still loads, and the tree
  builds and works, so it is committable.
- Step 4 deletes the header declaration, the handler and the route in one commit, so the build
  never sees a dangling declaration.
- `git push` and opening the PR follow the pipeline's established pattern
  (`2026-09-26-issue-98-plan.md:1274`, `issue-99-plan.md:1754`).

VERDICT: CLEAR
