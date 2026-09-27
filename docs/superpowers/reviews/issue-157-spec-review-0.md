Tier: heavy

# Review 0: issue #157 spec (`docs/superpowers/specs/2026-09-27-issue-157-design.md`)

Reviewed against issue #157 (`gh issue view 157 --repo victorstein/berean-os`), the research note
(`docs/superpowers/research/2026-09-27-issue-157-research.md`), and the tree at `aefaad48`. I did not
send any requests to jw.org. The research's response shapes are the only live evidence, and nothing
below depends on a new request.

## What holds up

I checked these claims against the code and they are correct:

- **A-1.** `split` really does let the last field absorb any later tabs. The branch at
  `CatalogIndex.cpp:27` (`found + 1 == count`) takes `line.substr(start)`. The comment at `:20-21`
  says the opposite, and the comment is wrong. A 6-field v2 split therefore keeps a title whole.
- **A-6 is needed, not optional.** v1 and v2 come from one builder run with the same
  `date.today()` (`build_catalog_index.py:131`) and the same manifest id. `sameRelease` compares only
  language, manifest and date (`CatalogStamp.cpp:56-59`). Without the version field, a device that
  holds the v1 from that run would report the v2 as `UpToDate` (`CatalogIndexStore.cpp:241-245`).
- **A-7.** The claim that no `HttpDownloader` change is needed is correct: a non-200 body never
  reaches the sink (`HttpDownloader.cpp:78`, `:100-101`). `languagePresent` fits the existing
  `Node::Language` push (`PubMediaJson.cpp:57-58`), and `reset()` is at `:31-39`.
- **d1.** A downgrade to an older firmware that still holds a v2 `.idx` recovers by itself.
  `loadFrom` refuses the file (`CatalogIndexStore.cpp:170-174`), the stamp stays invalid, the
  catalog row offers "download" (`CatalogSearchActivity.cpp:235-237`), and `!hadIndex` installs the
  v1 (`:163-172`).
- **Call sites.** `nextEntry` has only one production caller (`CatalogSearchActivity.cpp:195`), and
  `publication::failureMessage` has only the two callers the spec names
  (`CatalogSearchActivity.cpp:424`, `MeetingDownloadActivity.cpp:314`).
- **Line references.** The spec's line citations that I sampled all match (`CatalogSearchActivity.h:45`,
  `ci.yml:125-151,155-161`, `WolWeekScan.h:32-34`, `catalog-index.yml:93-97`).

## Findings

### MAJOR 1: A-14 is only half-applied. The Publish step's gate still skips the sidecar in the steady state

**Claim.** A-14 says the sidecar is uploaded even when the builder exits 3, because re-probing a
negative row changes `probedOn` without changing any flag (spec :65). §2 says to "Upload
`catalog-<lang>.probes.tsv` on status 0 or 3" (spec :131-133). The same bullet keeps the index
uploads "exactly as today (`:85-91`)".

**Problem.** Today the whole Publish step is gated on at least one language returning 0:
`if: steps.spanish.outputs.status == '0' || steps.english.outputs.status == '0'`
(`catalog-index.yml:71`). Once the backlog has cleared, the normal week is: no new catalog rows,
expired negatives re-probed, still 404. Both languages then exit 3, the step does not run, and the
sidecar with the new `probedOn` dates is never uploaded. The next week plans the same expired
negatives again (A-10: "negative rows older than 90 days"), up to 1,000 per language, and this
repeats every week. That is the exact case A-14 was written for. Left like this, the design breaks
its own goal ("Only negative results are re-probed, and only rarely", spec :29-31) and the courtesy
constraint in the posture note.

**Evidence.** `catalog-index.yml:71`. Spec :131-133 does not mention the step's `if:`.

**Fix.** Say explicitly that the Publish step's `if:` becomes "either status is 0 or 3". Inside the
step, upload each language's v1/v2 pair only when that language's status is 0, and upload each
language's sidecar when its status is 0 or 3. The existing `[ -f "$f" ] || continue` guard happens
to work for the index files, because exit 3 writes none. Add a workflow-level acceptance line:
"both languages exit 3 → sidecars still uploaded".

### MAJOR 2: A-13 decides what state exists by whether a download worked, so transient failures can wipe the cache or stop probing for good

**Claim.** A-13 (spec :64) and §2 (spec :124-129) work like this. The fetch step records whether
each download succeeded. When the sidecar is missing and a v2 exists, the run uses `--no-probe` and
"records none". The cache starts empty "only when no v2 has ever been published".

**Problem.** There are two failure paths. Both come from treating "the curl failed" as "the asset
does not exist". Today's fetch is `curl -sfL … || true` under `continue-on-error`
(`catalog-index.yml:34-40`).

1. **The cache is wiped.** If fetching both the previous v2 and the sidecar fails for a transient
   reason (GitHub 5xx, a network blip), the run looks exactly like "no v2 ever published". The
   builder starts from an empty cache and probes 1,000 rows. `--previous` is missing, so it hits
   `FileNotFoundError` and exits 0 (`build_catalog_index.py:179-190`). Publish then clobbers the v2
   and the sidecar with a cache holding ≤1,000 rows. Every row that was hidden before is visible
   again on devices, and the backlog restarts. This is the reset A-13 says it prevents.
2. **Probing stops for good, silently.** If the v2 is published but the sidecar is not (a Publish
   step that fails between the two uploads, or the asset deleted by hand), every later run takes
   the A-13 branch. That branch "records none", so no sidecar is ever produced again, and each of
   those runs exits 0 or 3, which counts as success. Probing stops and nothing reports it.

**Evidence.** `catalog-index.yml:34-40` (`|| true`, `continue-on-error: true`),
`build_catalog_index.py:189-190` (a missing previous file means "changed"), spec :64 and :206.

**Fix.**
- Decide existence from the release listing, not from the download:
  `gh release view catalog --json assets -q '.assets[].name'`.
- If an asset is listed but cannot be downloaded, build with `--no-probe` and publish no v2 or
  sidecar that run, or fail loudly.
- If the v2 is listed and the sidecar is not, rebuild the sidecar from the previous v2's flags,
  with `probedOn = today` for the `0` rows, and upload it. The next run then resumes probing.
- State in the spec which of these the `--no-probe` path writes.

### MAJOR 3: the header version is read once in `onEnter`, but the index is swapped inside the same session

**Claim.** "`CatalogSearchActivity` holds the header version it got from
`catalog::parseHeader(store.view())` and passes it to `nextEntry`" (spec :156-157).

**Problem.** `runSearch` also runs right after `applyStaged()` reloads a *different* index, in two
places:

- the "install update" path (`CatalogSearchActivity.cpp:111-120`);
- the first-install path (`:163-171`).

On first install nothing is held at `onEnter` (`:63-68`), so the saved version is 0, a value
`nextEntry` has no mapping for. The in-session v1→v2 upgrade path is also affected. That is the path
every device updated by OTA takes: it holds v1, taps "Check for update", then "Install". With a
stale version of 1, the v2 buffer is parsed as 5 fields, so `title` becomes `"1\t¡Despertad! 1980"`
and nothing is filtered, until the user leaves Buscar and comes back.

**Evidence.** `CatalogSearchActivity.cpp:111-120`, `:163-171`, `:182-195`. `CatalogIndex.cpp:83-99`
also skips the header, so the version is never re-read.

**Fix.** Read the version at scan time rather than caching it. A-6 already adds `version` to
`Stamp`, so use `store.stamp().version` inside `runSearch`, or call `parseHeader(index)` at the top
of `runSearch`. Have `nextEntry` treat any version other than 1 or 2 as "no records". Add a host test
that swaps the buffer between two scans.

### MINOR 1: the rollout contradicts the probing rules

Rollout step 1 says the first weekly job after merge builds v2 with "all rows unprobed until the
first probing run", and step 2 puts that first probing run on "the scheduled Monday 05:00 UTC after
merge" (spec :280-283). Those are the same run. With no v2 published yet, A-13 starts from an empty
cache and probes 1,000 rows per language in that very job. A-24 (spec :75) says the same thing
correctly. **Fix:** merge steps 1 and 2. The first scheduled run, or a `workflow_dispatch`, builds
v1, builds v2 with up to 1,000 probed rows per language, and uploads the first sidecar.

### MINOR 2: comparing only v2 records republishes v1 with a new date every time a flag changes

"`--previous` compares v2 records" and "exit 0 when anything was written that differs" (spec :111,
:119) mean that a flag-only change rewrites v1 with a new header date. Older firmware compares
`builtOn` (`CatalogStamp.cpp:56-59`), so it would report "update available" for records that did
not change. That happens every week for the ~4 backlog weeks, and again whenever a re-probed
negative flips. Before this change, v1 moved only when its records moved (`build_catalog_index.py:179-188`).
**Fix:** keep a `--previous-v1` comparison against the existing `previous-<lang>.txt` fetch, and
write and upload v1 only when its records differ. d1's "v1 unchanged" holds either way. This
tightens it.

### MINOR 3: moving `wordAt` breaks the `CatalogStampTest` link, and two existing tests assert v1

`wordAt` is currently file-local in `CatalogStamp.cpp:21`. Moving it to `CatalogLabel.cpp`
(spec :168-170) means `test/catalog_stamp/CMakeLists.txt:2-5`, which lists only `CatalogStamp.cpp`
and `CatalogIndex.cpp`, also needs `CatalogLabel.cpp`, or the test fails to link. The spec's
shared-file hand-off (:288-295) mentions only `add_subdirectory(catalog_label)`. Two existing tests
also need editing, not just new tests beside them:

- `CatalogStampTest.cpp:66-69`, `AFutureVersionIsRefusedRatherThanReinterpreted`, uses version `2`
  as the "future" version and would start failing.
- `CatalogIndexTest.cpp:30` compares a v1 fixture against `FORMAT_VERSION`.

**Fix:** list all three edits in the plan.

### MINOR 4: the `%s` in the month formats receives a `string_view` that is not null-terminated

A-15 fixes the translated formats as `"%d de %s de %d"` and `"%s de %d"` (spec :66). The month name
comes from `wordAt`, a view into the middle of the twelve-word `STR_MONTHS_LONG`. Passing
`name.data()` to `%s` would print "abril mayo junio … diciembre de 1980". This is the `string_view`
pitfall described in `CLAUDE.md`. **Fix:** state that `formatIssueDate` copies the month word into
a small terminated local buffer (for example `char[16]`) before calling `snprintf`. The format
parameters should be `const char*`, since `tr()` returns that and `snprintf` needs a terminated
format.

### MINOR 5: d2's exact command cannot import the builder

`python -m unittest discover -s scripts/tests` (spec :74, :308-309) puts only `scripts/tests` on
`sys.path`, so `import build_catalog_index` raises `ModuleNotFoundError`. I reproduced this in a
scratch tree with the same layout: `FAILED (errors=1)`. **Fix:** the test module inserts
`Path(__file__).resolve().parent.parent` into `sys.path`, or the job passes `-t scripts` and adds a
`scripts/tests/__init__.py`. The d2 decision itself is unchanged.

### MINOR 6: no per-probe timeout or per-language time budget, so the 90-minute kill loses the week's work

The spec sizes the job at "two capped runs of about 20 minutes each" (spec :129-130), which assumes
sub-second responses. The existing `fetch` uses `timeout=600` (`build_catalog_index.py:43`), and
spacing is start-to-start (A-10). A slow CDN can therefore push 1,000 probes past 90 minutes. The
job is then cancelled before Publish, which throws away that run's probes and its v1 rebuild.
**Fix:**
- Specify a per-probe timeout, for example 15 s.
- Specify a per-language wall-clock budget for probing, for example `--probe-budget-seconds 1500`,
  after which probing stops and the builder goes on to write its output.

### MINOR 7: a 429 or 5xx stops only the language that saw it

A-9 and A-10 say a 429 or 5xx "stops probing for the rest of the run" (spec :60). But S and E run as
separate builder processes (`catalog-index.yml:42-64`), so E starts probing straight after S was
throttled. **Fix:** have the builder write a marker file, or print a step output, when it was
throttled, and pass `--no-probe` to the English step when that is set.

### MINOR 8: a failed English build still goes unreported

"The workflow's existing failure step (`:93-97`) reports it for Spanish" (spec :207). This change
adds a new way for English to fail on its own, a malformed `catalog-E.probes.tsv`, which would then
fail every week with nothing reported. **Fix:** extend "Fail loudly" to
`steps.english.outputs.status`.

### MINOR 9: the "hidden rows do not use the cap" test exercises a copy of the loop

The `test/catalog_index` plan tests "a search-shaped loop" (spec :240-242). The real loop and its
`MAX_RESULTS` cap live in `CatalogSearchActivity::runSearch` (`:182-207`), which the host tests do
not build. That leaves the issue's "device-side filter drops flagged entries" acceptance checked
against a re-implementation. **Fix:** move the scan into `lib/Catalog`, for example
`catalog::search(index, version, query, maxResults, visit)`, call it from `runSearch`, and test that
function directly. Doing this also solves MAJOR 3.

## Summary

d1 and d2 are applied correctly apart from MINOR 5. The format, version-gate and download-fallback
design is sound and matches the code. The three MAJORs are all integration seams that can be fixed
in the spec without reopening a decision:

- the Publish step's `if:` that A-14 needs;
- A-13's existence detection;
- reading the index version at scan time.

None of them changes scope or needs the owner's judgment, so fix them in the spec before planning.

VERDICT: CLEAR
