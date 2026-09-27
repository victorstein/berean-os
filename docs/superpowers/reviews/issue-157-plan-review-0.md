Tier: heavy

# Issue #157 — plan review 0

Plan: `docs/superpowers/plans/2026-09-27-issue-157-plan.md` (cited as **P**)
Spec: `docs/superpowers/specs/2026-09-27-issue-157-design.md` (cited as **S**)
Checked against the worktree at `2957aea0`.

## Summary

The plan is sound and can be executed as written. I checked every code
replacement against the file it replaces, and each one lines up with the current
text: the `nextEntry`/`split`/`lineAt` shapes (`CatalogIndex.cpp:9-35,82-98`),
the helpers moved out of `CatalogStamp.cpp:9-45`, `PubMediaJsonParser::push`
(`PubMediaJson.cpp:50-72`), `pubMediaUrlForSymbol` (`WolWeekScan.cpp:71-84`),
the downloader block (`PublicationDownloader.cpp:151-158`), `runSearch`
(`CatalogSearchActivity.cpp:176-201`), the header draw (`:546`), and the
workflow (`catalog-index.yml:34-97`). I traced each Python and gtest assertion
through the code the plan supplies, and they hold. Types and names stay
consistent across tasks: `EpubAvailability`, `OLDEST_READABLE_VERSION`,
`SearchVisitor`, `collectHit`, and `run_probes`/`plan_probes`/`build`
signatures. Every file the plan touches appears on a column-0 `FILES:` line
(P:12-26).

There is one MAJOR, in the workflow's English step. Its fix is mechanical and
needs no decision from the human. The MINORs are about wording, one
partial-download edge, and the rollout note.

## Findings

### MAJOR 1 — A Spanish throttle turns an English "seed" run into a flag wipe

- **Claim.** P:2156-2160 builds the English arguments as
  `if hold || spanish throttled → --no-probe; elif seed → --seed-from-v2`.
- **Problem.** Mode `seed` means the English v2 is listed and its sidecar is not
  (P:2119-2121, S A-13(b)). If Spanish was throttled in the same run, the English
  step takes the first branch. It gets `--no-probe` and never gets
  `--seed-from-v2`. No `previous-E.probes.tsv` exists, so `build()` starts from
  `cache = {}` (P:1912-1917). It renders a v2 in which every flag is empty
  (P:1723) and writes an empty sidecar. Because the mode is `seed`, not `hold`,
  Publish uploads both (P:2189, 2202). The result:
  - every English device sees previously hidden no-EPUB entries come back;
  - the next run finds a listed but empty sidecar and re-probes the whole
    backlog.

  This breaks A-13's intent that a missing sidecar "must not … wipe the
  accumulated cache". It also breaks the spec's test line "`--no-probe` makes no
  calls and carries the previous v2 flags over" (S:304).
- **Evidence.** P:2156-2160 (the branches are exclusive), P:1912-1917 (a cache
  comes only from `--probes-in` or `--seed-from-v2`), P:2185-2204 (the upload
  depends only on `mode != hold`).
- **Fix.** Make seeding independent of the throttle, as the Spanish step already
  is:

  ```bash
  case "${{ steps.previous.outputs.mode_E }}" in
    hold) args+=(--no-probe) ;;
    seed) args+=(--seed-from-v2 previous-E.v2.txt) ;;
  esac
  [ "${{ steps.spanish.outputs.throttled }}" = 1 ] && args+=(--no-probe)
  ```

  `build()` already handles `--seed-from-v2` together with `--no-probe`
  correctly. Add a `BuildTest` case that pins it:
  `self.build(self.args("--seed-from-v2", seed, "--no-probe"))` should make no
  requests and write `km\t198001\t1980\tperiodical\t0\t` into v2.

### MINOR 1 — A partially downloaded sidecar in `hold` mode fails the whole language, v1 included

- **Claim.** P:2115-2118 sets `mode=hold` when the sidecar download fails.
  P:2140 and P:2161 still pass `--probes-in` whenever the file exists.
- **Problem.** `curl -sfL -o` leaves a partial file when a transfer breaks
  mid-body. `read_probes` rejects a truncated last line (P:1741-1743), and the
  builder then exits non-zero. Publish skips that language entirely, so its v1
  is skipped too, and "Fail loudly" fails the job. S A-13(a) and S:238 say that
  in this case only the v2 and the sidecar are held back.
- **Fix.** Remove the partial file on failure:
  `… || { rm -f "previous-$lang.probes.tsv"; mode=hold; }`. The same applies to
  `previous-$lang.v2.txt`. Alternatively, pass `--probes-in` only when the mode
  is not `hold`.

### MINOR 2 — Step 5.2's expected result names one failing stamp test; two fail

- **Claim.** P:970-972 says `CatalogStampTest` "fails only
  `AFutureVersionIsRefusedRatherThanReinterpreted`".
- **Problem.** `FORMAT_VERSION` becomes 2 while `indexAcceptable` still tests
  `== FORMAT_VERSION` (`CatalogStamp.cpp:61-63`). So
  `TheMatchingVersionAndLanguageIsAccepted` also fails, because its `kIndex` is
  version 1 (`CatalogStampTest.cpp:14,62-64`). An implementer who reads the plan
  literally could stop to investigate.
- **Fix.** Say that both tests fail until Task 6.

### MINOR 3 — Task 14 pushes

- **Claim.** P:2318 says "Push: `git push`."
- **Problem.** `CLAUDE.md` Git rule 2 says: "Never push to any remote … without
  explicit user approval." Pushing belongs to the orchestrator, not the plan.
- **Fix.** Replace the step with "Stop; the orchestrator pushes."

### MINOR 4 — The rollout note understates the gap before the first v2 exists

- **Claim.** Task 7 points the device at `catalog-<lang>.v2.txt.gz` (P:1124-1125).
  The PR note says the owner "may" start the workflow early (P:2324-2328).
- **Problem.** release-please releases on merge, so a device can install the new
  firmware by OTA before any v2 asset exists. Until the next Monday run, "check
  for update" on that firmware fails with `STR_CATALOG_FETCH_FAILED`. A device
  with no held index cannot get one at all. A device holding v1 keeps working
  (A-3). This does not reverse A-24, which already allows a `workflow_dispatch`.
  But the note should say why an early dispatch matters.
- **Fix.** In the PR notes, state the window and recommend a `workflow_dispatch`
  of `catalog-index.yml` right after merge.

### MINOR 5 — Task 10's byte-identity check downloads from jw hosts on a dev machine

- **Claim.** P:1993-2004 runs the new builder locally.
- **Problem.** Even with `--no-probe`, the builder fetches the manifest and the
  57.6 MB catalog from `app.jw-cdn.org` (P:1962, 1972). That is an extra request
  against the service the posture note protects, and pipeline workers may be
  told to make no jw requests at all. `FormatTest.test_v1_is_the_original_layout_byte_for_byte`
  (P:1403-1410) already pins the v1 bytes, and the row code is unchanged apart
  from being split out (compare P:1685-1711 with
  `build_catalog_index.py:120-147`).
- **Fix.** Mark the check as optional and skip it when jw hosts are off-limits.
  Or run it with `--db` against a catalog that is already on disk.

## Checked and not raised

- **Spec coverage.** Every requirement maps to a task:
  - A-1 to A-6: Tasks 5-7.
  - A-7 and A-8: Tasks 1-3.
  - A-9 to A-14: Tasks 10 and 12.
  - A-15 to A-21: Tasks 4 and 8.
  - A-23: Task 11.
  - A-24: the PR notes.
  - Docs: Task 13.
  - Fail-loudly for English: P:2209-2216.

  The four documented departures (P:30-35) do not change scope:
  - The second test executable follows the `test/number_grid/CMakeLists.txt:1,16`
    precedent.
  - The YAML edits add new keys only.
  - `STR_SEARCH_HEADER` puts the separator through `tr()`.
  - `describeHit` is kept.
- **TDD exceptions.**
  - Task 3 (`publication::download`), Task 7 (`assetUrl`) and the Task 8 glue
    have no host test. This is justified because Arduino-bound code has none, and
    the logic under them is tested in Tasks 1, 2 and 4.
  - Tasks 11-13 are CI and docs work.
  - Steps 5.2, 5.3 and Task 6 are committed as one unit, and the plan says so
    (P:973-974).
- **Callers of `publication::Result`.** They compare only against `Cancelled`,
  `Ok` and `AlreadyOnCard` (`CatalogSearchActivity.cpp:403,417,423`,
  `MeetingDownloadActivity.cpp:293,301,305`), so `NoEpubEdition` needs no
  caller edits.
- **Stamps.** They are derived only from the parsed header
  (`CatalogIndexStore.cpp:169-176,235-241`), and nothing persists a stamp. So
  adding `version` cannot leave a stale version-0 stamp that causes endless
  "update available".
- **Firmware build of `CatalogLabel.cpp`.** `lib/Catalog` has no
  `library.json`, so PlatformIO compiles `lib/Catalog/Catalog/*.cpp` as it
  already does for `CatalogArchive.cpp`.
- **Local tools.** The `pio-locked.sh` path in P:46 exists. Local Python is
  3.14, and the plan's annotations and stdlib use are compatible with both 3.14
  and CI's 3.12.

VERDICT: CLEAR
