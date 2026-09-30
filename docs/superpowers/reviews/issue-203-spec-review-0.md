Tier: standard

# Issue #203 spec review 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-203-design.md` (at `6327272f`), against issue #203
(`gh issue view 203 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-30-issue-203-research.md`.

Most of the spec checks out against the code. These claims were verified and hold:

- `pickRecent` skips by chapter (`PlacesDoc.cpp:78-87`), and places are unique per (book, chapter)
  (`PlacesDoc.h:78-80`).
- `ReaderEntryIntent::of`/`openAt`/`route` (`ReaderEntryIntent.h:21-50`) and their consumption
  (`EpubReaderActivity.cpp:934-968`).
- The Bible's passage file is `/.berean/passages/bible.json` (`PassageFile.cpp:16`, `PubKey.h`
  `BIBLE_PUB_KEY`), and `locatePlace` refuses any other pubkey (`StudyStore.cpp:102-104`), so A4's
  one-file scope is consistent.
- `thumbHeightFor(464, h) == 773` for h ≤ 773 (`CoverBandGeometry.h:46-48`), and
  `MastheadLayout::band` gives x = 8, width = 464 with insets 9/3/3/3 (`MastheadLayout.h:23-28`,
  `BoardConfig.h:624-629`) and Lyra `topPadding = 5`.
- The sampler, prefilter, scan and allocation citations (`StudySleepPick.h:29,88-148`,
  `StudySleepScreen.cpp:42,189-291,376-402`).
- The paint-then-work precedent (`MeetingsActivity.cpp:285-293`), and that nothing else asks for a
  render while the loop task is blocked. `main.cpp:796` requests one only from the loop.
- `GUI.drawHeader` takes a subtitle (`BaseTheme.h:224`, `BaseTheme.cpp:321-335`).
- The icon (`src/components/icons/bookmark.h:5`) and every cited `english.yaml` key and line.

The two findings that matter are one false premise under decision d1, and one false premise under
A12.

## MAJOR 1: d1 rests on a false premise. RTC memory survives deep sleep, so a per-wake re-scan isn't forced

**Claim.** A6: "Deep sleep is a full chip reset (`src/main.cpp:140-143`), so nothing in RAM survives
from one wake to the next… A cache that is only in RAM would re-pick on the first Home of every
wake". The d1 decision builds on this: "a RAM cache for the wake", and "Each wake pays one scan on
its first Home entry".

**Problem.** Heap and `.bss` do not survive deep sleep, but RTC slow memory does, and this firmware
already relies on that.

- The X4 Pro sleeps through a plain `esp_deep_sleep_start()`
  (`freeink-sdk/libs/hardware/PowerManager/src/PowerManager.cpp:91-96`). Nothing powers down the
  RTC domain: grepping the tree for `esp_sleep_pd_config` and `ESP_PD_DOMAIN` finds nothing.
- The C3 battery-cut branch is compiled out on the S3 (`HalPowerManager.cpp:78`,
  `#if !SOC_PM_SUPPORT_EXT1_WAKEUP`).
- The repo already keeps data in `RTC_NOINIT_ATTR` across resets: the log ring
  (`lib/Logging/Logging.cpp:12-19`), the panic capture (`lib/hal/HalSystem.cpp:17-21`) and the
  silent-restart flags (`src/main.cpp:137-139`).

As a result, the spec's design fails the issue's acceptance criterion as literally written ("Later
entries don't re-scan"). The spec itself says the device "sleeps many times a day" (A6), and every
wake re-parses up to about 204 KB of `bible.json` (`MAX_FILE_BYTES`, `StudySleepScreen.cpp:42`) on
the loop task, with input unpolled (A10). The issue asked for the pick "cached once per day in RAM",
and RTC slow memory is RAM that makes that literal.

**Evidence of the budget.** In the primary checkout's last `x4pro` build,
`xtensa-esp32s3-elf-size -A .pio/build/x4pro/firmware.elf` reports `.rtc_noinit 5524` at
`0x50000000` (RTC slow memory, 8 KB on the ESP32-S3) and `.rtc.force_slow 32`. That leaves roughly
2.6 KB. A cached pick needs about 600 bytes: a magic word, the date, a `study::Unit`, a 49-byte
reference, and text bounded by what fits the card (a few hundred bytes, per A8).

**Fix.** This changes a recorded decision, so the owner has to choose:

- **(a)** Keep d1. Correct A6's premise to "heap RAM does not survive; RTC memory would, and was
  declined because …". Also state plainly that the AC "Later entries don't re-scan" is met only
  within a wake.
- **(b)** Keep the date seed, which is still useful because it makes the pick reproducible and
  handles the no-clock case, but hold the day's pick in an `RTC_NOINIT_ATTR` struct guarded by a
  magic word, a format byte and the date, modelled on `Logging.cpp:19-30`. The cache then survives
  wakes, the scan runs once per day, and A8's internal-SRAM allocation disappears. The text must be
  a fixed `char[]` capped by the card fit, not a `std::string`.

Either way, the RAM-singleton rationale in A8 and the device test "Sleep and wake the same day"
must follow the choice.

## MAJOR 2: A12's `spineHint = 0` is based on a false claim, and it breaks card taps on non-verse Bible passages

**Claim.** A12: "`spineHint = 0` sends `STUDY.locatePlace` straight to its book search…, because a
passage row stores no spine."

**Problem.** Both halves are wrong.

- **A passage row does store a spine.** `PassageDoc::toJson` writes `row["s"] = p.documentSpine`
  (`lib/StudyStore/StudyStore/PassageDoc.cpp:279`) and reads it back at `:321`
  (`TaggedPassage.h:37`, "spine index, a weaker hint"). The reader's own passage lookup uses it
  (`StudyStore.cpp:159`, `locateUnit(passage.start, passage.documentSpine)`).
- **Hint 0 does not go straight to the book search.** `locateUnit` first calls
  `units_->unitsFor(spineHint)` on spine 0, which may build that index (`StudyStore.cpp:187`).
- **Only a Verse unit can be book-searched.** Every other unit returns `nullopt`
  (`StudyStore.cpp:194`).
- **The NWT has many non-verse documents.** It carries 153 Paragraph documents and 2,595
  DocumentOffset documents (`Unit.h:18-19`), and `offerRow` accepts any addressable start
  (`StudySleepScreen.cpp:191-192`, `unitFromCompact`).

The result is that a card pick tagged in study notes, an appendix or an introduction opens with
`STR_LINK_TARGET_NOT_FOUND` (`EpubReaderActivity.cpp:949-951`). That fails the acceptance criterion
"the verse card … land[s] on the right screen". Even a Verse pick pays a book-wide search, "up to
150 documents, each possibly a fresh index build" (`StudyStore.cpp:201`), where one lookup would do.

**Fix.**
- `Candidate` gains `uint16_t spineHint` beside `start`, read from `row["s"] | 0` in `offerRow`.
- The tap becomes `intent{OpenAt, pick.start, pick.spineHint}`.
- Rewrite A12's rationale accordingly.
- Add a `test/study_sleep_pick` case that the spine is carried through `Sampler::fill`.
- Optional: add the stored spine to the A8 cache contents.

## MINOR 1: A13 marks the UTC date as "today" when the clock's time is unreadable

**Claim.** A13: `buildWeekStrip(monday, &today, …)`, and "With no clock, the strip and range are
omitted".

**Problem.** `readLocalDate` can succeed with `shifted == false`, which returns the bare UTC date
(`src/util/LocalDate.h:5-8`). Meetings passes `haveDate && todayIsLocal ? &today : nullptr`
(`MeetingsActivity.cpp:156`) precisely because "a UTC date would mark the wrong day for part of
every day" (`:207-209`). The spec passes `&today` unconditionally and only covers the no-date case.

**Fix.** Pass `todayIsLocal ? &today : nullptr`, and split A13's no-clock sentence into "no date"
(omit the strip) and "date without time" (draw the strip with no day marked).

## MINOR 2: The date seed needs mixing, and the "independent pick" test is too weak

**Claim.** A6: "a small PRNG seeded from the local date's `yyyymmdd`". The test: "over a fixed set
of offers the picks for a run of dates are not all the same".

**Problem.** Adjacent dates are adjacent integers. A small linear PRNG (an LCG or xorshift) seeded
with n and n+1 gives correlated early draws, and `Sampler::offer` makes one draw per fitting row
(`StudySleepPick.h:112`). Consecutive days can therefore repeat or cycle picks. The proposed test
only catches a constant pick.

**Fix.** Hash the date before seeding: `fnv1a32` over the eight digits is already in
`StudySleepPick.h:34-40`, or use splitmix32. Strengthen the test so that over about 30 consecutive
dates and 10 offers, no single pick takes more than, say, half the days, and adjacent days differ
more often than not.

## MINOR 3: An empty scan result is neither cached nor told apart from "no tags"

**Claim.** A8's cache holds `{valid, dated, day, pick}`. A11 shows `STR_HOME_TAGS_EMPTY` ("Passages
you tag appear here") both when there is no `bible.json` and when "no row is whole and fits".

**Problem.**
- **Nothing says a completed scan with no pick is cached.** If it isn't, a user whose tagged
  passages are all too long for the card re-parses `bible.json` on every Home entry, which breaks
  "Later entries don't re-scan".
- **The hint misleads.** It tells a user who has tags to go and tag something. The scan already
  counts `rowsNotWhole` and `rowsUnfit` (`StudySleepScreen.cpp:95-102`), so the two cases can be
  told apart.

**Fix.**
- Add `bool hasPick` to the cache.
- Cache a completed empty result for the day. OOM stays uncached, as the error table says.
- Use a second hint when rows exist but none fits, for example "Your tagged passages are too long
  to show here". Hand that key off too.
- Optional: key the cache on `bible.json`'s size as well as the date. A `Storage` size check costs
  no parse, and it retires most of A9's staleness.

## MINOR 4: The verse card's height is left unspecified, but it decides which passages can ever appear

**Claim.** HomeLayout derives every section "from `ThemeMetrics`, viewable insets and line
heights". The hero is "the remainder after the fixed sections" (A17).

**Problem.** No section height is stated: not the Recent rows, the card, Meetings or the icon row.
The card's text box is A5's fit gate, so its height determines what fraction of the owner's
tagged verses is ever eligible. The mockup's 126 px card, minus its label, pill and reference line,
holds only two or three serif lines. The hero's minimum art height is also left unnamed ("a
minimum art height").

**Fix.** State each fixed section's height in metric and line-height terms, and the card's text
box in lines at each rung. Name the hero's minimum art height. Pin both in `test/home_layout`
against Lyra and the base theme.

## MINOR 5: A2's claim that it matches today's behaviour isn't true, and the refresh flag is chosen inconsistently

**Claim.** A2: `goToReader(biblePath, true)` "is today's Bible tile behaviour
(`LauncherActivity.cpp:431`)".

**Problem.** Today's tile calls `goToReader(biblePath)`, whose default is
`allowFastInitialRefresh = false` (`ActivityManager.h:86`). Passing `true` changes the reader's
first-page refresh budget (`ReaderActivity.cpp:19-22`). A1 also uses `true`, while A12 and A14 use
`false`, and no rationale is given for either.

**Fix.** Choose the flag per target with a one-line reason (only the old resume strip used `true`,
at `LauncherActivity.cpp:422`), and correct A2's wording.

## MINOR 6: The Meetings card adds a thumbnail size, against the principle A17 uses to reject 470 px

**Claim.** A13 draws the cover "at the card's height through `CoverThumb::pathFor`… generated once
and then cached". A17 rejects 470 px because "it would generate a second thumbnail".

**Problem.**
- **It is a new size.** `CoverThumb::pathFor` builds a thumbnail at exactly the requested height
  (`CoverThumb.h:10-12`). A card-height cover (about 76 px in the mockup) matches neither
  Meetings' `layout_.coverHeight` (`MeetingsActivity.cpp:119-120`), which Meetings deliberately
  keeps to one size (`:111-112`), nor the old tile's size.
- **It runs before the first paint.** The spec resolves it in `onEnter`, so the first Home entry
  after each week's download opens the EPUB and shows the generation popup ahead of the first
  paint.

**Fix.** State the extra size and its once-per-publication cost explicitly. Alternatively, follow
`Masthead`'s `cachedThumb` (`Masthead.cpp:33-38`, which never opens the EPUB), use the placeholder
when no thumbnail is cached, and generate the thumbnail after the first paint on the same pending
path as the verse.

## MINOR 7: The i18n hand-off leaves out Spanish, which recent features maintain

**Claim.** Hand-offs: three `english.yaml` keys, and "The other languages fall back to English."

**Problem.** `spanish.yaml` is kept current by feature PRs. #221 added `STR_RECENT: "RECIENTES"`
beside the English key (`git show --stat 75c65e93`: both YAMLs +1; `spanish.yaml:428`). The
owner's Bible is the Spanish NWT, so Home would mix English labels into a Spanish UI. The issue's
plate label is also "Go to…", but `STR_GO_TO` is "Go to" (`english.yaml:477`).

**Fix.** Hand off the Spanish lines for every new key, and settle whether the Go to button carries
an ellipsis.

VERDICT: BLOCKER
BLOCKERS: 0
MAJORS: 2
