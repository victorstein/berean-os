Tier: heavy

# PR #161 — intent review, pass 0

Reviewed: `gh pr diff 161` (head `90665cce`, base `2b22d536`) against issue #156,
the spec `docs/superpowers/specs/2026-09-27-issue-156-design.md` and the plan
`docs/superpowers/plans/2026-09-27-issue-156-plan.md`.

## Summary

The PR does what the issue asks and nothing more. It covers the three
recognition steps, register-on-open, the recents fallback, and the download
offer with its confirm, Wi-Fi, progress, failure and escape-hatch paths. Each
spec assumption (A1–A4, B1–B8, C1–C3, D1–D7, E1) has a matching code site. The
implementation follows the plan almost exactly: the plan's Task 6 header and
`.cpp` blocks differ from the committed files by one whitespace column from
clang-format (`BibleDownloadActivity.cpp:24`), and Tasks 5 and 7 match
verbatim. No divergence needs explaining. I found no BLOCKER or MAJOR, only two
MINOR test-quality notes.

## Issue acceptance, item by item

| Issue requirement | Where it is met |
|---|---|
| Register on open, reusing an existing Bible signal | `EpubReaderActivity.cpp:265-278`. Uses `getBibleBookNavSpineIndex() >= 0`, the same signal `StudyStore.cpp:34` uses. It is already cached by then, because `STUDY.openPublication` runs first at `:249`, so no new heuristic is added. |
| Once per book; skip the write if an entry exists; not on the render path | `LauncherBible.h:226-232` (`registerBibleIfUnknown`: lookup before record). Called from `loadBook`, which only `ReaderActivity.cpp:53` calls (`onEnter`), so it is not the render path. The write goes through `PubKeyRegistry::record`, which already follows the storage discipline (spec B8). |
| Order registry → CDN scan → recents; recents only last | `LauncherBible.h:186-210`; `LauncherActivity.cpp:113-125`. |
| Recents fallback is the old match | `LauncherBible.h:215-218` is character-for-character the lambda removed in `08d98a9f` (`path` contains "nwt", or `title` contains "Nuevo Mundo" or "New World"). The added `Storage.exists` check (`LauncherActivity.cpp:204-212`) is spec A2 and is explained in the PR. |
| The launcher never opens an EPUB | The recents step reads `RecentBook` fields and calls `exists` only. |
| Tile text invites a download, via `tr()`, in english and spanish | `english.yaml:9` and `spanish.yaml:9` (value change on `STR_BIBLE_SUBTITLE_NONE`, spec C1). |
| Confirm first, naming edition, language and approximate size | `BibleDownloadActivity.cpp:301-303` and `STR_BIBLE_DOWNLOAD_PROMPT`. The language comes from `SETTINGS.publicationLanguage`, which is also the catalog's language (`CatalogIndexStore.cpp:49`). |
| `publication::download` with `nwt`, empty issue, the catalog language | `BibleDownloadActivity.cpp:457-478`. |
| Wi-Fi through `WifiSession`, no hand-rolled teardown | `BibleDownloadActivity.h:718` (optional member declared last) and `.cpp:290` (`emplace` in `onEnter`), as `WifiSession.h:6-8` prescribes. The connection itself uses `WifiSelectionActivity`, as the model does. |
| Refresh on success; say whether it opens the Bible or returns | Returns to the launcher (spec C3); `LauncherActivity.cpp:584-587` re-resolves. |
| Every `Result` maps to a `tr()` message; a failure leaves the card untouched and the tile still offering the download | `BibleDownloadActivity.cpp:480-506`. `Cancelled` posts a toast (D7). No cleanup code is needed: `HttpDownloader.cpp:300-302` removes the partial file, and `PublicationDownloader.cpp:218-221` removes one that fails the checksum. |
| Escape hatch to the file browser | The Confirm dialog's secondary button (`BibleDownloadActivity.cpp:356-362`, `:514-515`). |
| Wi-Fi unavailable gives a clear failure and a retry, not a hang | A cancelled or failed picker leads to Failed with Retry (`:390-396`). A connection with no internet shows no dead Cancel during the resolve (`:348`, `:580-582`). |
| Host tests: order, laziness, idempotency, and the offer only after all three miss | `LauncherBibleTest.cpp:793-819` and `:889-895`. |

No acceptance item was dropped or narrowed.

## Scope

- No expansion. The code touches only the files listed in the plan's `FILES:` lines. `src/study/`, `test/CMakeLists.txt` and `src/main.cpp` are unchanged. The YAML edits are the ones E1 authorises, and the PR lists them.
- Spec B2 accepts a sideloaded NWT-family Bible without an entry being registered as `nwt`. That follows from the issue's "reuse the existing signal" instruction and is not a scope choice the PR introduces.

## Findings

### MINOR 1 — The launcher's step-to-lookup wiring is untested; only the generic walk is

`LauncherBibleTest.cpp:793-819` shows that `resolveBible` walks
`BIBLE_LOOKUP_ORDER` lazily. The issue's resolution-order criterion is really
about the `switch` in `LauncherActivity.cpp:115-124`: Registry maps to
`findBySymbol`, CardScan to `findBibleOnCard`, Recents to `findBibleInRecents`.
No test covers that mapping, because it is firmware-bound. The same holds for
"offer only after all three miss": the test shows the helper returns `nullopt`,
while the offer itself is the `biblePath.empty()` branch in `openBible`
(`LauncherActivity.cpp:571-575`). Both are short and correct on reading, and
the spec's test 4 accepts this split. The PR could name the gap alongside the
`exists` gap it already states. No code change is needed.

### MINOR 2 — `NamesEachLookupForTheLog` restates the implementation

`LauncherBibleTest.cpp:821-825` asserts the three string literals returned by
`bibleLookupName` (`LauncherBible.h:189-199`). It protects no behaviour beyond
the serial-log text that device check 2 greps for. The plan asked for it
(plan `:91`), so this is not a divergence. It is harmless, and can be kept or
dropped.

VERDICT: CLEAR
