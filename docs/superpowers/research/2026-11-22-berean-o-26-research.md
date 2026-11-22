# Research: berean-o-26 — the Tarjetas tab in Predicación

Branch `feature/preaching-tarjetas-tab`, HEAD `5134f9be`, which contains the batch tip
(`git merge-base --is-ancestor origin/batch/20261122-tarjetas-preaching-cards-in-predicaci-n-65ka HEAD`
succeeds). The previous task's reader is present: `src/study/PreachingHandouts.{h,cpp}` and
`test/storage_io/PreachingHandoutsTest.cpp` exist on this branch.

## Who owns the behaviour

| Behaviour | Owner |
|---|---|
| The tab bar, rows, notices, opening a row, coming back from a topic | `src/activities/preaching/PreachingActivity.{h,cpp}` |
| Data shared while Predicación is open | `src/activities/preaching/PreachingSession.{h,cpp}` |
| Turning a source into layout cards | `src/activities/preaching/PreachingContent.{h,cpp}` (`TopicCards`, `loadQuestion`, `loadTag`) |
| Where a topic or the reader came from | `src/activities/preaching/PreachingOrigin.h` |
| The card screen: show mode, a reference tap, the result it returns | `src/activities/preaching/PreachingTopicActivity.cpp` |
| Reading `/preaching-cards.json` | `src/study/PreachingHandouts.{h,cpp}` (out of scope; read only) |
| Reader entry and the swipe back | `src/activities/reader/ReaderEntryIntent.h`, `EpubReaderActivity.cpp:705-715`, `ActivityManager.cpp:232-233` (read only) |
| Strings | `lib/I18n/translations/{english,spanish}.yaml` |

## Current control flow

**Tabs.** `enum class Tab : uint8_t { Questions, Topics, Count }` and `Tab tab = Tab::Questions`
(`PreachingActivity.h:24,60`). `tabLabel` is a two-way ternary (`PreachingActivity.cpp:72-74`).
`stepTab(int)` ignores its argument and toggles (`:141-148`); `UiTabListActivity.cpp:70-71` calls
it with `+1` and `-1`. `onTabAction` jumps straight to an index (`:132-139`). `showTab` loads the
Mis temas counts lazily (`:125-130`).

**Entry.** `onEnter` (`:39-63`): opens the session, sets `tab = Topics` only for a Tag origin, so a
Question origin relies on `Questions` being the default (`:42-47`). It builds rows under a
`RenderLock`, calls `seedSelection` (`:112-123`), then re-pushes the topic for a Question or Tag
origin (`:55-62`). Making Tarjetas the default breaks the Question case unless `onEnter` sets
`Tab::Questions` explicitly.

**Rows.** `rebuildRows` (`:78-110`) fills `rowItems` whose `label`/`value` are `c_str()` pointers into
session strings and `rowValues`; it reserves before both push loops. The caller holds the
`RenderLock` (`PreachingActivity.h:47`). Empty states: `buildScreen` draws a centred notice for
Questions with no section and a two-line top notice for an empty Mis temas (`:172-184`).

**Opening.** `activateIndex` (`:198-210`) → `openQuestion`/`openTag` (`:270-298`): allocate
`TopicCards` with `makeUniqueNoThrow`, call `PreachingContent::load*`, report a toast through
`failureMessage` on failure, else `pushTopic` (`:300-309`).

**Back from a topic.** `onTopicResult` (`:311-324`) re-selects the tab and row from the
`PreachingResult`'s origin, per kind.

**Card screen.** `originAt` (`PreachingTopicActivity.cpp:163-167`) maps Question and Tag only and
returns `{}` otherwise. `openBible` (`:202-232`): a Question verse goes through
`ReaderEntryIntent::openHref` plus `resolveHrefToSpineIndex`; everything else is assumed to be a
tag passage (`topic->passages[source]`, `OpenAt`). A handout reaching that `else` would index an
empty `passages` vector — so `openBible` must branch on `Handout` explicitly.

**Reader round trip.** `ReaderEntryIntent::openHref` refuses an href of `>= HREF_BYTES` (64)
(`ReaderEntryIntent.h:23,48-50`) and copies the origin into the intent (`:55`). The reader keeps it
(`EpubReaderActivity.h:233`) and on the swipe back, when `preachingOrigin.isSet()`, calls
`activityManager.goToPreaching(preachingOrigin)` (`EpubReaderActivity.cpp:706,711-713`), which
constructs a fresh `PreachingActivity` with that origin (`ActivityManager.cpp:232-233`). The reader
never inspects `kind`, so a new `Kind::Handout` needs no reader change.

## The handout reader this task consumes (`src/study/PreachingHandouts.h`)

- `FILE_PATH = "/preaching-cards.json"`, `FORMAT_VERSION = 1`, `MAX_FILE_BYTES = 128 KiB`,
  `MAX_HREF_BYTES = 63`, `MAX_HANDOUTS = 200`, `MAX_TITLE_BYTES = 96` (`:21-29`).
- `Status { Ok, Missing, Unreadable, TooNew, OutOfMemory }` (`:31`).
- `summaries(json, out)` → `Summary { title, uint16_t verses }` (`:33-36,53`);
  `load(index, allocators, Handout&)` → `Handout { title, headings, verses }`, each `Verse` carrying
  `section` (index into `headings`), `reference`, `href`, `PassageText text` (`:38-49,57`).
- The two agree on the count: both skip a verse whose href does not open (`PreachingHandouts.cpp:105`
  in `summaries`, `:146-150` in `load`), so the row's verse count equals the cards that open.
- Every `Verse::section` is an index already pushed into `headings` (`PreachingHandouts.cpp:142-144`),
  so `headings[verse.section]` in `loadHandout` is in bounds.
- An index past the end is `Unreadable` (`:129-131`); `summaries` stops at `MAX_HANDOUTS` (`:100-101`).
- The firmware passes `PsramJsonAllocator::json()` / `::passageDoc()` (`PreachingHandouts.h:18`;
  `PsramJsonAllocator.h:18,22`), the same pair `loadTag` and `ensureCounts` use
  (`PreachingContent.cpp:81`, `PreachingSession.cpp:152`).

## Nearest existing example

The Mis temas tab is the model: `Kind::Tag` + `PreachingOrigin::ofTag` (`PreachingOrigin.h:8,23-29`),
`PreachingContent::loadTag` (`PreachingContent.cpp:79-103`), `PreachingSession::ensureCounts`'s
lazy, load-once read with a held status (`PreachingSession.cpp:143-153`, `PreachingSession.h:51-54,67-69`),
and its tab/row/return branches in `PreachingActivity.cpp`. It landed squashed in `99a88230`
(#277, 2026-10-15). The plan's Tasks 7–8 mirror each of those lines one-for-one for a handout.

## Strings

- `STR_PREACHING_SAVE_FAILED` sits at `english.yaml:498` and `spanish.yaml:445`, as the plan says.
- File Transfer's labels: `STR_FILE_TRANSFER: "File Transfer"` (`english.yaml:12`) and
  `"Transferir archivos"` (`spanish.yaml:12`) — the plan's Spanish wording already matches.
- None of the three new keys exists yet (`grep STR_PREACHING_CARDS` over both files: no match).

## Host-test seams

`PreachingActivity`, `PreachingContent` and `PreachingTopicActivity` are not built by any host
suite. `PreachingOrigin` is: `test/ui_layout/ReaderEntryIntentTest.cpp:57-62,80-86` exercises
`ofQuestion`/`ofTag` and the intent's origin copy (`test/ui_layout/CMakeLists.txt:85-99`). That is
the place for the failing-first test of `ofHandout` and of an origin surviving `openHref`. The
three-way `stepTab` arithmetic lives inside the activity and has no host seam today.

## Installed versions

| Tool | Command | Output |
|---|---|---|
| PlatformIO | `/Volumes/stein/.platformio/penv/bin/pio --version` | `PlatformIO Core, version 6.1.19` |
| Python | `python3 --version` | `Python 3.14.7` |
| CMake | `cmake --version` | `cmake version 4.4.2` |
| ArduinoJson | `platformio.ini:155` | `bblanchon/ArduinoJson @ 7.4.2` |

At research time this worktree has no `.pio/` and no `build/test/`: the `./.claude/pipeline-bootstrap`
has to run before the first `pio run` or host-test build.

## Findings the spec must settle

1. **Default tab vs. Question origin.** Covered by plan Task 8 Step 3; without it a Question origin
   reopens on Tarjetas.
2. **`openBible` fall-through.** The `else` assumes a tag; a handout must be its own branch (plan
   Task 7 Step 4 does this).
3. **Translation YAMLs are a "report, do not edit" shared file** per `.claude/agents/ui-dev.md`
   ("Shared files"), but the brief explicitly assigns the three strings to this task and the plan's
   Task 6 edits them. The brief is the more specific instruction; the edit stays in this change.
4. **`USER_GUIDE.md` §11 says Predicación opens "with two tabs"** (`USER_GUIDE.md:335-343`). The plan
   does not touch it and will leave it wrong. Either a short §11 update joins this change or it is
   filed with `hpipe discover`.
5. **Tier.** The new enum value crosses into the reader only as an opaque `PreachingOrigin`
   (`EpubReaderActivity.cpp:706,713`); the origin is RAM-only (`PreachingOrigin.h:6`), so there is no
   on-disk migration and no other surface edits. `standard` holds.
