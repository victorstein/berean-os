Tier: heavy

# PR #163 — intent review 0

Reviewed against issue #157, the spec
`docs/superpowers/specs/2026-09-27-issue-157-design.md`, and the plan
`docs/superpowers/plans/2026-09-27-issue-157-plan.md`, at `616a1d8d` (merge base
with `origin/main`: `3bcec700`).

Verification I ran myself: `python3 -m unittest discover -s scripts/tests` (26
tests pass), and the host suites `CatalogIndexTest` (24), `CatalogStampTest`
(21), `CatalogLabelTest` (10) and `PubMediaJsonTest` (15), all passing. I did not
rebuild the firmware or run the full ctest set; the PR reports both.

## Findings

No BLOCKER and no MAJOR findings.

### MINOR 1 — Devices with no index cannot get one until the workflow runs; the fix is only a recommendation

`src/network/CatalogIndexStore.cpp:57-62` now fetches only
`catalog-<lang>.v2.txt.gz`, and that asset does not exist until
`catalog-index.yml` has run once (`.github/workflows/catalog-index.yml:20-21`:
Mondays 05:00 UTC, or `workflow_dispatch`). release-please cuts a release as soon
as the PR merges. If that release reaches a device that holds no index before the
first run, Buscar reports a failed fetch for up to a week. A device that already
holds a v1 index keeps working (`CatalogStamp.cpp` `indexAcceptable` accepts 1..2).

The PR body states the gap and recommends a `workflow_dispatch` right after
merge, as the plan's "PR notes to carry" require, so the gap is disclosed, not
hidden. The spec chose this rollout (A-2, A-24), and this review does not
reverse that choice. **Action:** the orchestrator should pass the dispatch
recommendation to the owner at merge time, not leave it in the PR body. No code
change is needed.

### MINOR 2 — The three-way download fallback has no direct test

The decision in `src/network/PublicationDownloader.cpp:157-174` has three
outcomes: EPUB found on the retry, `NoEpubEdition`, or `NoMediaLink`. Only its
inputs are host-tested: `languagePresent()` in
`test/pub_media_json/PubMediaJsonTest.cpp` and the unfiltered URL in
`test/wol_week_scan/WolWeekScanTest.cpp`. The branch itself is covered only by
the owner's device check ("An unprobed no-EPUB entry … shows 'No hay edición
EPUB…'"). Neither the issue nor the spec asks for a host test of this branch, and
`PublicationDownloader` depends on `HttpDownloader`, which has no host seam. This
is acceptable, but the device check is the only thing that proves this path
works. **Action:** none required.

## Acceptance criteria — issue #157

| Criterion | Status | Evidence |
|---|---|---|
| Fix 1: an honest message; "not published in this language" only when the language is absent | Met | `PublicationDownloader.cpp:163-172` retries without the filter and returns `NoEpubEdition` when `files.<lang>` is present with no EPUB. `failureMessage` maps it to `STR_NO_EPUB_EDITION` (`:265`). The strings are in `english.yaml`/`spanish.yaml`. A transport failure still gives `NoMediaLink`, which the spec deliberately leaves out of scope (Non-goals, A-7). The issue only asks about *responses*, so this is not a silent reduction. |
| Fix 2: per-entry flag, sidecar cache, probe only uncached entries, rate limit, honest UA, rare negative re-probe, posture note | Met | `build_catalog_index.py:238-250` (plan: unprobed rows, then negatives older than 90 days; positives never), `:297-316` (1 s from start to start, cap, time budget, stop on 429/5xx), `:253-259` (probe URL mirrors the device's), `:262-271` (existing `USER_AGENT`), `:209-217` (sidecar, pruned). Posture note: `2026-09-13-berean-os-design.md:573-587`. |
| Fix 3: unconditional device filter; no toggle; a flagless index lists everything; version bump refused by old builds | Met | `CatalogIndex.cpp` `listable` / `search` filter before `matches` and before the cap. `FORMAT_VERSION = 2`, and v1 reads as `Unknown`. The bump is served under a new asset name, so shipped builds (which refuse anything but 1) never see it (decision d1). A v3 is refused (`indexAcceptable`, and `search` visits nothing). |
| Fix 4: the message covers unprobed and withdrawn entries | Met | An empty flag stays listed (`listable`). A tap then goes through the fallback in fix 1. |
| Rows: localised issue date, formatter is pure and host-tested, months through `tr()` | Met | `lib/Catalog/Catalog/CatalogLabel.cpp` `formatIssueDate` handles `YYYYMMDD` and `YYYYMM`, which are the only shapes `normalise_issue` (`build_catalog_index.py:111-127`) produces. Strings: `STR_MONTHS_LONG`, `STR_ISSUE_DATE_DAY`, `STR_ISSUE_DATE_MONTH`. |
| Rows: symbol and raw code dropped; a book keeps at most its code | Met | `CatalogSearchActivity.cpp:49-58`: a date when there is an issue, otherwise the year, otherwise the symbol. The title's trailing year is trimmed (`:206`). |
| Header names the language once | Met | `CatalogSearchActivity.cpp:61-64, 553` (`STR_SEARCH_HEADER`, from the publication-language setting). |
| Each row stays one tap, with no detail screen | Met | Row handling is unchanged. |
| Host: formatter covers each form, both languages, malformed input falls back to the raw code | Met | `test/catalog_stamp/CatalogLabelTest.cpp`. |
| Host: `PubMediaJson` tells "language present, no EPUB" from "language absent" | Met | `PubMediaJsonTest.cpp`, new tests including the root `languages` decoy. |
| Host: the filter drops flagged entries from every result list and keeps unflagged ones | Met | `CatalogIndexTest.cpp` `CatalogSearchScan.*`. `catalog::search` is the only index consumer in `src/` (`CatalogSearchActivity.cpp:197`). |
| Script: probes only uncached entries; respects the rate limit; writes the flag | Met | `scripts/tests/test_build_catalog_index.py` `PlanTest`, `ProbeTest.test_probes_are_at_least_the_interval_apart`, `FormatTest.test_v2_writes_the_flag_before_the_title`, and `BuildTest`. The tests inject a fake clock and fake API, so they check timing and requests rather than repeating the implementation. |
| Device checks (3 + 1) | Listed for the owner | PR body, "Device checks for the owner". |

## Spec coverage

Every assumption, A-1 to A-24, is implemented, including the parts that are easy
to skip:

- A-6: the version is part of `sameRelease`.
- A-13(a/b/c): the fetch step works out each case from the release asset list
  (`catalog-index.yml:43-82`), and the hold case keeps back v2 and the sidecar
  (`publish()`, `:146-167`).
- A-14: the Publish `if:` accepts status 3.
- A-9: throttling on Spanish also stops English, and it is added on top of the
  mode (`:116-121`).
- MAJOR 3: `search` re-reads the header of the buffer it scans on every call.
- "Fail loudly" now covers English.

The `script-tests` job gates `test-status` (`ci.yml`), as decision d2 requires.

## Scope

- **Nothing beyond the issue.** JWPUB download, a toggle and a detail screen are
  all absent. The Bible download screen gets no new mapping; `NoEpubEdition`
  falls through to that screen's generic hint
  (`BibleDownloadActivity.cpp:252-278`), and the PR says so.
- **The Meetings screen now shows the corrected message.** This follows from the
  shared download path (spec A-8), not from new work.

## Divergence from the plan

The plan's "Where this plan departs from the spec" section lists four
departures:

- the label tests run as a second executable in `test/catalog_stamp`;
- the YAML is edited directly;
- the header uses a `STR_SEARCH_HEADER` format string;
- `describeHit` is kept.

The code matches all four, and the PR body repeats the two shared-file ones. I
found no departure from the plan that is not explained.

VERDICT: CLEAR
