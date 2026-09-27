# Issue #157 — research

Buscar lists publications the device cannot open, reports the failure as a
language problem, and labels every result with a raw code. This note covers how
the repo behaves today, measured on `6f47178e` (main, release 1.17.0).

## Who owns what

| Concern | File |
|---|---|
| Index builder (CI, Python) | `scripts/build_catalog_index.py` |
| Index publisher (schedule, release upload) | `.github/workflows/catalog-index.yml` |
| Index record format and parser | `lib/Catalog/Catalog/CatalogIndex.{h,cpp}` |
| Version gate, stamp, ISO build-date formatter | `lib/Catalog/Catalog/CatalogStamp.{h,cpp}` |
| On-device cache, fetch, PSRAM hold | `src/network/CatalogIndexStore.{h,cpp}` |
| Buscar screen: search, rows, header, errors | `src/activities/catalog/CatalogSearchActivity.cpp` |
| Media-link resolve, download, failure text | `src/network/PublicationDownloader.{h,cpp}` |
| GETPUBMEDIALINKS URL | `src/network/WolWeekScan.cpp:71-84` |
| GETPUBMEDIALINKS parser | `src/network/PubMediaJson.{h,cpp}` |
| Strings | `lib/I18n/translations/{english,spanish}.yaml` (shared file: net-dev reports the lines and does not edit them; `.claude/agents/net-dev.md`, "Shared files") |
| Posture note | `docs/superpowers/specs/2026-09-13-berean-os-design.md:573-577` |

## Current control flow

### Building and publishing the index

1. `catalog-index.yml:14` runs weekly (`0 5 * * 1`) and on `workflow_dispatch`.
   It does not run on `pull_request` or `push`.
2. The workflow fetches the previously published `catalog-{S,E}.txt` from the
   fixed `catalog` release tag (`:34-40`). These files are used only to check
   whether anything changed.
3. `build_catalog_index.py` downloads the 57.6 MB catalog DB and selects
   `KeySymbol, IssueTagNumber, Year, Title` for one `MepsLanguageId` (`:124-128`).
   It canonicalises the issue with `normalise_issue` (`:89-105`): a trailing
   `00` day is stripped to `YYYYMM`, and `0` and `00000000` become empty. It
   writes one line per record, `symbol\tissue\tyear\tkind\ttitle` (`:144`),
   under the header `berean-catalog\t1\t<lang>\t<manifest>\t<date>` (`:131`).
4. If the records match the previous file byte for byte, the builder exits 3 and
   nothing is republished (`:179-188`).
5. The Publish step uploads `catalog-X.txt` and `catalog-X.txt.gz` to the
   `catalog` release with `--clobber` (`catalog-index.yml:85-91`). The asset URL
   never changes.
6. The script makes no network calls to `b.jw-cdn.org`. The docstring
   (`:13-22`) records that choice: EPUB availability is not derivable from the
   catalog, and probing ~3,768 entries on every build was ruled out as
   discourteous. The only User-Agent is
   `bereanOS-catalog-indexer/1 (+https://github.com/victorstein/berean-os)` (`:35`).

### Loading the index on the device

1. `CatalogSearchActivity::onEnter` calls `CatalogIndexStore::load()`
   (`CatalogSearchActivity.cpp:63-68`).
2. `loadFrom` reads the cached `/.berean/catalog-<lang>.idx`, inflates it into
   PSRAM, and runs `catalog::indexAcceptable` (`CatalogIndexStore.cpp:156-180`).
3. `indexAcceptable` requires `header.version == FORMAT_VERSION` exactly
   (`CatalogStamp.cpp:61-63`, `FORMAT_VERSION = 1` at `CatalogIndex.h:50`).
   A mismatch returns `BadFormat`.
4. `checkRemote` downloads the asset from the fixed URL
   (`CatalogIndexStore.cpp:57-60`, `.../releases/download/catalog/catalog-<lang>.txt.gz`)
   and validates it with the same `loadFrom`. A `BadFormat` result deletes the
   staged file and leaves the held index in place (`:235-239`). The UI reports
   this as `STR_CATALOG_FETCH_FAILED` (`CatalogSearchActivity.cpp:154-158`).

**Consequence for the version bump.** The asset URL is the same for every
firmware version. If a v2 file replaces v1 at that URL, every shipped build
refuses it. A device that already holds v1 keeps it but can never update it; its
"Check for update" row fails every time. A device with no index can never
install one. This is the refusal `CLAUDE.md` asks for, but it also strands
builds that users have not yet updated by OTA. No precedent in this repo
publishes two format versions of one data asset side by side (see "Nearest
examples").

### Record parsing and search

- `split(line, fields, 5)` puts everything after the fourth tab into `title`
  (`CatalogIndex.cpp:22-35`). A sixth field appended after the title would
  therefore appear inside the title on a v1 parser. The version bump prevents
  that, because v1 refuses the file.
- `nextEntry` skips any line with fewer than 5 fields (`:88-89`).
- `runSearch` walks every record, keeps those where `matches` holds, and stops
  at `MAX_RESULTS` (`CatalogSearchActivity.cpp:182-207`). No filtering happens
  apart from the query match. Any EPUB filter belongs in this loop, before the
  `MAX_RESULTS` cap, so hidden entries do not use up result slots.
- The `symbol` row ("Download \"<query>\"", `:240-243`) skips the index
  entirely, so an index flag never covers it.

### Row labels and header

- Label: `hits[i].title` (`:247`). Subtitle: `describeHit(symbol, year, issue)`
  gives `"g  1980  19800422"` (`:46-53`, `:248`).
- Header: `GUI.drawHeader(..., tr(STR_SEARCH))` (`:552`). The language is shown
  nowhere on the screen.
- The publication language is `CatalogIndexStore::language()`, which returns
  `CrossPointSettings::langWritten(SETTINGS.publicationLanguage)`, "S" or "E"
  (`CatalogIndexStore.cpp:49`, `CrossPointSettings.h:72`). This setting is
  separate from the UI language. Its display names already exist as
  `STR_LANG_SPANISH` and `STR_LANG_ENGLISH` (`SettingsList.h:245-247`). They are
  translated into the UI language: "Español"/"Inglés" in Spanish,
  "Spanish"/"English" in English.

### Download failure path

1. `publication::download` builds the URL with `pubMediaUrlForSymbol`. The URL
   **always** includes `fileformat=EPUB` (`WolWeekScan.cpp:75,79`).
2. `HttpDownloader::fetchUrl` returns false on any status other than 200 and
   passes no body to the parser (`HttpDownloader.cpp:100-101` for wolfSSL,
   `:176-177` otherwise).
3. `!fetched || !media->found()` → `Result::NoMediaLink`
   (`PublicationDownloader.cpp:156-159`) → `STR_PUBLICATION_UNAVAILABLE`
   (`:248-249`), "Not published in this language" / "No está publicada en este
   idioma" (`english.yaml:437`, `spanish.yaml:379`).
4. `MeetingDownloadActivity` also calls `publication::download`
   (`MeetingDownloadActivity.cpp:299`), so any change to `failureMessage` also
   reaches the Meetings screen.

## What jw.org actually returns (live, 2026-09-27)

These were a handful of individual GETs with the builder's own User-Agent, spaced
2 s apart. Command shape:
`curl -s -A "$UA" -w "%{http_code} %{size_download}" "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&<q>"`

| Query | Status | Bytes | `files` |
|---|---|---|---|
| `pub=km&langwritten=S&issue=198001` | 200 | 979 | `S: [JWPUB]` |
| `pub=km&langwritten=S&issue=198001&fileformat=EPUB` | **404** | 80 | — (`[{"title":"Not Found","status":404}]`) |
| `pub=km&langwritten=TG&issue=198001` (no Tagalog edition) | **404** | 80 | — |
| `pub=km&langwritten=XX&issue=198001` (invalid code) | 400 | 82 | — |
| `pub=w&langwritten=S&issue=202601` | 200 | **36,191** | `S: [PDF, EPUB, JWPUB, RTF, BRL, MP3, DAISY]` |
| `pub=w&langwritten=S&issue=202601&fileformat=EPUB` | 200 | 959 | `S: [EPUB]` |
| `pub=g&langwritten=S&issue=19800422&fileformat=EPUB` | **200** | 954 | EPUB present |

What follows from this:

1. **The request the device sends today cannot tell the two cases apart.** With
   `fileformat=EPUB`, "language present, no EPUB" and "language absent" are the
   same 404, and step 2 above discards the body anyway. Fix 1 of the issue
   ("`PubMediaJson` can report which case applies") depends on a response the
   device never gets. To tell them apart, the device has to make a second
   request **without** `fileformat`, and only after the first one fails. That
   request returns 200 with `files.<lang>` present but no `EPUB` key in the "no
   EPUB" case, and 404 when the language is absent. The unfiltered request is
   not viable as the primary one: it is 36 KB for a current Watchtower because
   of the per-article MP3 list, against 959 B filtered.
2. **For the builder's probe, `fileformat=EPUB` is enough.** 200 means the device
   can open the entry; 404 means it cannot, whatever the reason. The response
   is about 1 KB or less either way.
3. **"Roughly 1973–2007 has no EPUB" is not accurate enough to filter on.** The
   docstring says it (`build_catalog_index.py:16-18`), but *¡Despertad!* 22 Apr
   1980 has an EPUB while *Nuestro Ministerio del Reino* Jan 1980 does not.
   Availability depends on the individual entry, so only a probe can establish
   it.
4. **The API returns a localised date.** `formattedDate` came back as
   `"Enero de&nbsp;1980"` (km 198001) and `"22 de abril de&nbsp;1980"`
   (g 19800422). That confirms the target wording for the Spanish row labels.
   The device cannot use it for search rows, because it is only in the
   per-entry API response and the index does not carry it.
5. The `pubName` for `km` in 1980 is "Nuestro Ministerio del Reino", while the
   catalog title is "Nuestro Servicio del Reino 1980". Searches match the
   catalog title, which is why the reported query found these rows.

## The index's real shape (published assets, built 2026-09-21)

The published files were fetched from the `catalog` release:
`curl -sfL .../releases/download/catalog/catalog-{S,E}.txt`

- Size: S is 243,637 B with 3,769 records; E is 245,388 B with 3,819 records.
  `CatalogIndex.h:13-15` and `CatalogIndexStore.h:75` still say 216,708 B and
  3,768. The budget of `MAX_INFLATED_BYTES = 1 MiB` (`CatalogIndexStore.h:78`)
  leaves plenty of room for an added one-byte field: 3,769 × 2 B ≈ 7.5 KB.
- Issue shapes, counted with `awk -F'\t' 'NR>1{print length($2)}'`. These are
  the only three shapes present:

  | Length | S | E | Form |
  |---|---|---|---|
  | 0 | 310 | 323 | book, or a `kind=book` row (`ws` 1986, `CA-brpgm17`) |
  | 6 | 975 | 994 | `YYYYMM`: `km` (552), `g` (149), `w` (132), `mwb` (96), `wp` (29), `ws` (17) |
  | 8 | 2,484 | 2,502 | `YYYYMMDD`, days only 01/08/15/22: `w` (1,488), `g` (864), `wp` (96), `ws` (36) |

  E has the same forms (8-digit days: 792 on the 01st, 432 on the 08th, 846 on
  the 15th, 432 on the 22nd). Neither language has a month outside 1–12. The
  year in every issue code matches the `year` column.
- `mwb` is bimonthly in recent years (`202601`, `202603`, `202605` …) but was
  monthly in 2016 (`201601`, `201602` …). A `YYYYMM` label such as "Enero de
  2026" would therefore name only the first month of a two-month issue. The
  index does not record the span.
- **Titles already end in the year.** 3,406 of the 3,459 Spanish periodical
  titles end in `" YYYY"`, and that suffix equals the `year` column in every
  case. The remaining 53 are formatted like `mwb` "… Cristianos (2016)", or
  like `ws` "… 2013 (lenguaje sencillo)". So "¡Despertad! 1980" is the catalog
  title as it stands. The issue's device check wants the label "¡Despertad!"
  with "22 de abril de 1980" underneath, which means removing a trailing year
  from the label (or leaving the year off the subtitle) — and this has to be
  specified.
- In 2008, `w` (15th, study edition) and `wp` (1st, public edition) share the
  title "La Atalaya. Anunciando el Reino de Jehová 2008". Once the symbol is
  off the row, the date alone tells them apart.

## Nearest existing examples

- **Pure, host-tested date formatter:** `catalog::formatIndexDate`
  (`CatalogStamp.cpp:65-85`, tested in `test/catalog_stamp/CatalogStampTest.cpp`).
  It takes a translated month list as a `string_view`, uses `wordAt` to pick a
  month, writes into a caller's `char*`, and falls back to the raw input when
  it cannot parse. The issue-date formatter should follow the same shape. It
  lives in `lib/Catalog`, which makes it host-testable with no Arduino
  dependency, and its test target pattern is `test/catalog_stamp/CMakeLists.txt`.
- **Parser test with JSON fixtures:** `test/pub_media_json/PubMediaJsonTest.cpp`
  (11 `TEST`s, fixtures `pubmedia_{w_202607,mwb_202609}_S.json`). A "language
  present, no EPUB" test would add a fixture shaped like the km 198001 body
  above, trimmed per `public-repo-test-fixtures-no-full-publisher-text`.
- **Index format and its test:** `lib/Catalog/Catalog/CatalogIndex.cpp` with
  `test/catalog_index/CatalogIndexTest.cpp`. Introduced in `3299c67a`
  ("feat: add the publication catalog index format"), then `79d3e4fe` (CI
  publisher) and `b1412ef7` (#14, Buscar). No later commit has changed the
  format, so this would be its first version bump.
- **Version-refusal rule:** `indexAcceptable` (`CatalogStamp.cpp:61-63`), and
  `BOOK_CACHE_VERSION` / `SECTION_FILE_VERSION` per `CLAUDE.md`. Those caches are
  device-local, so a bump just regenerates them. The catalog index is the only
  versioned format published *remotely* and shared by every firmware version,
  and nothing in this repo publishes two versions of an asset side by side.
- **Script tests: no precedent.** The repo has no Python tests.
  `git ls-files | grep -iE 'pytest|conftest|tests?/.*\.py'` returns nothing.
  CI runs only CMake/ctest (`ci.yml:125-151`), and `pytest` is not installed
  locally (`ModuleNotFoundError`). The issue's "Script tests" acceptance
  therefore needs a new test harness (stdlib `unittest` is the dependency-free
  option) and a new CI step. Both are patterns this repo does not yet have.
- **No probe persistence yet.** The only state the workflow carries between runs
  is the previous `.txt`, fetched from the release (`catalog-index.yml:34-40`).
  A probe sidecar uploaded to the same `catalog` release with `--clobber` would
  follow that existing mechanism.

## Installed versions

| Tool | Version | Command |
|---|---|---|
| Python (local) | 3.14.7 | `python3 --version` |
| Python (catalog CI) | 3.12 | `catalog-index.yml:32` |
| Python (build CI) | 3.13 | `ci.yml:50` |
| pytest | not installed | `python3 -c "import pytest"` |
| CMake | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` |
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` |
| gh | 2.96.0 | `gh --version` |
| `scripts/requirements.txt` | pillow, cairosvg, matplotlib, pyserial, colorama (no test runner) | `cat` |

## Scope and tier

The change covers the CI script and its workflow, `lib/Catalog` (on-disk and
published format), `src/network`, `src/activities/catalog` (UI), and the i18n
YAML (shared). It crosses several surfaces, bumps a published format, and needs
a one-time backlog job against a third-party API. `heavy` is already the highest
tier (`standard|heavy`), so no tier change is needed.

## Open questions for the spec

1. **Keep older builds working.** Either publish v2 under a new asset name
   (e.g. `catalog-S.v2.txt.gz`) and keep building v1 at the old URL, or replace
   v1 in place and strand older builds until they update by OTA.
2. **Script tests.** Use stdlib `unittest` plus a new CI job, or accept a gap in
   this acceptance criterion.
3. **Backlog pacing.** At 1 probe/s, S and E together take about 7,600 s (~2 h)
   in one run. The alternative is a per-run probe cap that spreads the backlog
   over several weekly runs. Unprobed rows stay visible either way.
4. **Title trimming.** Decide whether to remove the trailing year from periodical
   labels so the row reads "¡Despertad!" / "22 de abril de 1980".
5. **Date language.** Month names through `tr()` follow the UI language, while
   the list's language follows `publicationLanguage`. A Spanish index can
   therefore be labelled with English dates. The issue asks for `tr()`, so this
   is expected, but the spec should say so.
