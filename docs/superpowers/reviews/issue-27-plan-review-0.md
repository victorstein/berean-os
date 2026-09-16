# Adversarial review — issue #27 implementation plan, pass 0

**Target**: `docs/superpowers/plans/2026-09-16-issue-27-plan.md`
**Against**: `docs/superpowers/specs/2026-09-16-issue-27-design.md`, and
`docs/superpowers/reviews/issue-27-spec-review-0.md` (CLEAR, 5 MAJORs, 6 MINORs,
all folded into the spec)
**Worktree**: `/Volumes/stein/.herdr/worktrees/berean-os/fix-27-atomic-store-saves` @ `3ac49316` (clean)
**Date**: 2026-09-16

## What holds up

I ran every shell command the plan specifies against the current tree and
compared every before/after anchor byte-for-byte. The results below are what
survived that.

**Every count reproduces.**

```
$ grep -rn 'saveToFile()' --include='*.cpp' src | wc -l        → 55   (step 1: 55)
$ grep -rn 'saveToFileAtomic' --include='*.cpp' src | wc -l    → 0    (step 1: 0)
step 3  WifiCredentialStore 5, RecentBooksStore 4, CrossPointSettings 1   → 5, 4, 1
step 4  main 4, SdCardFontSystem 1, PublicationDownloader 1,
        CrossPointWebServer 1, CardBooks 1                               → 4,1,1,1,1
step 5  Settings 8, Text 7, StatusBar 5, ButtonRemap 2,
        LanguageSelect 1, ClockSync 1, ClockOffset 1                     → 8,7,5,2,1,1,1
step 6  Reader 2, EpubReader 2, EpubReaderMenu 2, Sleep 1, Launcher 1,
        RecentBooks 1, WifiSelection 1, BmpViewer 1, Frontlight 1        → 2,2,2,1,1,1,1,1,1
step 7  PersistableStore.h → :39,:42,:96,:126,:172,:173                   → 6 lines
step 8  grep -c 'LOG_ERR("RBS"' src/RecentBooksStore.cpp                  → 1
step 9  grep -n 'limit reached' CrossPointWebServer.cpp                   → 1 hit
```

The arithmetic closes: 10 + 7 + 25 + 12 = 54 converted, 1 deleted
(`CardBooks.cpp:59`), plus `PersistableStore.h:173` — exactly the spec's
"54 converted, 1 deleted, plus the base class" (spec:124). No `.cpp` call site in
`src` falls outside steps 3–6; I cross-checked the full 24-file census against
the four step lists and every file is claimed exactly once.

**Every anchor matches verbatim, including indentation.** I diffed all eight
before-blocks against the file: `PersistableStore.h:39-42`, `:96`, `:124-125`,
`:172-173`; `CrossPointSettings.h:354`; `WifiCredentialStore.h:32`;
`CardBooks.cpp:59` (2-space indent, matches); `CrossPointWebServer.cpp:1419`
(6-space indent, matches). Step 8's three `RecentBooksStore` anchors match after
step 3's rename, and the variable names the new `LOG_ERR`s use (`path` in
`addBook` and `updateBook`, `oldPath`/`newPath` in `updatePath`) all exist with
those spellings; `Logging.h` is already included (`RecentBooksStore.cpp:6`).

**Every insertion point is in a public section.** `CrossPointState.h:32`
(`public:` at `:13`), `CrossPointSettings.h:386` (`public:` at `:15`, no later
access specifier), `WifiCredentialStore.h:44` (`public:` at `:43`),
`RecentBooksStore.h:29` (`public:` at `:28`). `persist::DEFAULT_SAVE_BUDGET` is
reachable in all four via `PersistableStore.h:11` → `SaveBudget.h:23` = 45000, so
step 2a's three asserts are genuinely red and RecentBooksStore's is genuinely
green, exactly as the plan claims.

**Step 2's ordering is sound.** Appending at the end of each `.cpp` puts the
`static_assert` where the class is complete — all four files end with a closing
function brace, none is inside a namespace — and all four `.cpp` files include
their own header first. `saveBudget()` is public and `constexpr`
(`PersistableStore.h:111`), inheritance is public in all four stores.

**Environment facts check out.** `/Volumes/stein/.platformio/penv/bin/pio` exists,
`.venv/` exists, `bin/clang-format-fix` exists, `ColumnLimit: 120` and
`ReflowComments: Always` mean none of the inserted comments or `LOG_ERR` lines
will be re-wrapped or joined.

**Spec coverage is complete.** Every spec section maps to a step: §1 → steps 3–7,
§3 → step 2, §Error handling A6a → step 8, §Error handling HTTP 400 → step 9, §Data
and control flow's four stale comments plus A8's `:124-125` → step 10, §Testing 1–5
→ steps 1/2/11, §Testing 6–9 → step 12. The one exception is MAJOR 2 below.

**The unbuilt commits are a declared trade, not an oversight.** Steps 3–10 commit
on a grep, not a build. The plan states this and justifies it from CLAUDE.md's
build-once rule (plan:33-37), and the edits are mechanically signature-preserving
(`saveToFile()` and `saveToFileAtomic()` are both `bool …() const`, both public).
I am not raising it as a finding, but it is worth naming: eight of the ten commits
are asserted-buildable rather than verified, and a typo in step 7 or 8 would not
surface until step 11, mid-history.

Two MAJORs and six MINORs follow. Neither MAJOR reverses a decision, changes
scope, or needs a judgment only the human can make.

---

## MAJOR 1 — the `sed` command in steps 3, 4, 5 and 6 is a silent no-op on this machine

**Claim.** Steps 3, 4, 5 and 6 each implement their conversion with exactly one
command (plan:183-184, 213-215, 256-263, 294-303):

```sh
sed -i '' 's/\bsaveToFile()/saveToFileAtomic()/g' <files…>
```

**Problem.** `sed -i ''` is BSD syntax, so this is macOS's `/usr/bin/sed`, and BSD
sed's basic regular expressions have **no `\b` word-boundary escape**. `\b` is
neither an error nor a boundary — the pattern simply never matches. sed exits 0,
touches nothing, and says nothing. Four of the plan's ten implementation steps
therefore do nothing at all when executed literally, and 54 of the 55 call sites
are never converted.

The plan's own Green greps do catch it (step 3 would print `5, 4, 1` instead of
`0, 0, 0`), so this cannot reach a merge — but it costs the implementer four
broken steps and a debugging detour into a command the plan presents as
verified.

**Evidence.** Run on this host (`uname -s` → `Darwin`, `which sed` →
`/usr/bin/sed`):

```
$ printf 'a SETTINGS.saveToFile();\nb mysaveToFile();\n' > t.txt
$ sed -i '' 's/\bsaveToFile()/saveToFileAtomic()/g' t.txt ; echo "exit $?"
exit 0
$ cat t.txt
a SETTINGS.saveToFile();      ← unchanged
b mysaveToFile();             ← unchanged
```

The plan's own environment header pins the platform (plan:47, "Run everything
from /Volumes/…"), and the spec's environment note (spec:587-593) confirms the
macOS worktree.

**Concrete fix.** Drop `\b`. It buys nothing here: the only strings in these files
containing `saveToFile()` are the call sites themselves, and the receiver (`SETTINGS.`,
`APP_STATE.`, `RECENT_BOOKS.`, or an implicit `this`) is preserved by the
substitution either way. Verified on the same host:

```sh
sed -i '' 's/saveToFile()/saveToFileAtomic()/g' <files…>
```

If a boundary is wanted anyway, BSD sed spells it `[[:<:]]`, also verified:

```sh
sed -i '' 's/[[:<:]]saveToFile()/saveToFileAtomic()/g' <files…>
```

Apply to all four steps. The `\b` form must not survive into the plan, because it
fails without an error message.

---

## MAJOR 2 — the A5 follow-up issue is a condition of the spec review's ruling, and the plan gives it no step

**Claim.** Plan step 12 (plan:586-590), under "Hand back honestly":

> **One follow-up to file** (spec, **A5**): truncate `title`/`author` at
> `RecentBooksStore::addBook` … It is deliberately **not** part of this change.

**Problem.** This is not an optional nicety — it is one of two conditions the spec
review attached to *accepting* A5, and the spec records it as such:

- spec:432-434: "*Accepted at review pass 0, with two conditions:* the comment on
  `RecentBooksStore`'s budget must state the concrete worst case it accepts …, and
  the truncation follow-up must be **filed as an issue**, not left as a sentence
  here."
- spec:574-580 (Open question 1, closed): "Conditions attached: the comment states
  the worst case it accepts, and the truncation follow-up is filed as an issue."
- spec review 0, ruling on Open question 1: "A5's 'that is a separate issue' must
  become a filed issue rather than a sentence."

The first condition is met — step 2b's `RecentBooksStore.h` comment does state
"the accepted worst case is 10 x 4 unbounded strings" (plan:149-155). The second
is not. The plan converts the condition back into the sentence the review
explicitly rejected: no step number, no command, no title, no body, no place in
the commit table (plan:594-605), and no acceptance check. An implementer executing
the plan literally produces a branch on which A5's acceptance condition is unmet,
and nothing in the verification ladder notices.

**Evidence.** The plan's ladder (step 11, plan:547-561) has five checks, none about
the follow-up. The commit table (plan:594-605) has ten rows, none about it. The
only mention is the prose above, inside a section whose stated purpose is listing
what the *human tester* must do on hardware.

**Concrete fix.** Promote it to a numbered step with an executable form. Something
like:

```
## Step 12 — File the A5 follow-up (spec condition, not optional)

gh issue create --repo victorstein/berean-os \
  --title "fix: truncate RecentBooksStore title/author so recent.json can take a tight budget" \
  --body "Blocks tightening RecentBooksStore's SAVE_BUDGET below persist::DEFAULT_SAVE_BUDGET.
RecentBook::title and ::author are unbounded std::strings copied straight from EPUB
metadata (BookMetadataCache.h:15-16 -> ReaderActivity.cpp:60) with no truncation on the
path, so the accepted worst case is 10 entries x 4 unbounded strings. Truncate at
RecentBooksStore::addBook (src/RecentBooksStore.cpp:42); the repo's precedent for the
shape is lib/StudyStore/StudyStore/PassageDoc.h:26-28 (MAX_SNIPPET_BYTES = 120,
MAX_REFERENCE_BYTES = 48). Deferred out of #27 per spec A5."

Record the issue number in the hand-back.
```

Note the ordering constraint: CLAUDE.md forbids pushing or opening a PR without
approval. An issue is not a PR, but it is still a remote write — so if the
implementer is not cleared for it, the step must instead hand back the exact
command above for the human to run, and say so. Either shape is fine; a sentence
in the hand-back narrative is what the review already ruled out.

---

## MINOR 1 — the positional `EXPECT:` counts do not line up with what `grep` actually prints

`grep` on this host is **ugrep 7.8.4** (`grep --version` → `ugrep 7.8.4
aarch64-apple-macosx`), and it emits multi-file `-c` results in nondeterministic
order. Two identical runs of step 4's Red check:

```
$ grep -c 'saveToFile()' src/main.cpp src/SdCardFontSystem.cpp \
    src/network/PublicationDownloader.cpp src/network/CrossPointWebServer.cpp src/util/CardBooks.cpp
src/SdCardFontSystem.cpp:1
src/main.cpp:4
src/network/CrossPointWebServer.cpp:1
src/network/PublicationDownloader.cpp:1
src/util/CardBooks.cpp:1

$ (same command again)
src/SdCardFontSystem.cpp:1
src/main.cpp:4
src/network/PublicationDownloader.cpp:1
src/util/CardBooks.cpp:1
src/network/CrossPointWebServer.cpp:1
```

The plan writes these as ordered lists — `# EXPECT: 4, 1, 1, 1, 1` (plan:208),
`# EXPECT: 5, 4, 1` (plan:178), `# EXPECT: 8, 7, 5, 2, 1, 1, 1` (plan:251),
`# EXPECT: 2, 2, 2, 1, 1, 1, 1, 1, 1` (plan:289). Step 6's is the worst case: nine
files, six of them with the same count, in an order that will not match the list.
Every count is correct; only the presentation is order-dependent. **Fix:** write
them as `file: count` pairs, or pipe through `| sort`.

## MINOR 2 — the expected line for the surviving `saveToFile()` definition is off by one after step 10

Step 10's Green (plan:526-527) and step 11's ladder item 4 (plan:553-554) both
say:

```
grep -rn 'saveToFile()' src lib
# EXPECT: exactly one line -- PersistableStore.h:126, the definition itself
```

The line count is right; the number is not. Step 10's own third replacement
substitutes the two-line comment at `PersistableStore.h:124-125` with a **three**-line
comment (plan:500-503), which pushes `bool saveToFile() const {` from `:126` to
`:127`. Nothing else in the plan shifts that file (step 7 is a 2-for-2 replacement,
step 10's other two are 4-for-4 and 1-for-1), and clang-format will not rejoin the
new comment — `ReflowComments: Always` breaks over-long lines, it does not fill
short ones, and the existing comments in the file are wrapped at ~78 columns under
a `ColumnLimit: 120` and survive untouched today. **Fix:** say `:127` in both
places, or drop the line number and expect only "one line, the definition".

## MINOR 3 — step 9's Red expects `:1420`; the line is `:1419`

Plan:431-432:

```sh
grep -n 'limit reached' src/network/CrossPointWebServer.cpp
# EXPECT: one hit, around :1420
```

Actual: `1419:      server->send(400, "text/plain", "Cannot add network (limit reached)");`.
Nothing in steps 2–8 shifts that file (step 4's sed is line-preserving), so it is
`:1419` when step 9 runs. The plan inherited this from the spec, which says "widen
`:1420`" (spec:358) while citing the correct range `:1419-1421` two lines earlier.
"around" makes it survivable. **Fix:** `:1419` in both the plan and spec:358.

## MINOR 4 — the `BUILD`/`FORMAT` variables are never used, and `BUILD` is broken if anyone uses them

Plan:42-45 defines:

```sh
BUILD='~/.platformio/penv/bin/pio run'
FORMAT='PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix'
```

Neither is referenced anywhere in the plan — every step spells the command out in
full. Worse, both would fail if used: `~` inside single quotes is not expanded, so
`$BUILD` resolves to a literal `~/.platformio/…` path that does not exist, and
`$FORMAT` would need `eval` for the inline `PATH=` prefix to take effect. The
paragraph above them ("both prefixes are load-bearing; without them you get a
failure, not a false pass") is correct and worth keeping. **Fix:** delete the two
assignments and keep the prose, or make them `BUILD="$HOME/.platformio/penv/bin/pio run"`
and actually use them.

## MINOR 5 — "exactly three builds" enumerates four

Plan:35-37: "this plan … spends exactly **three** builds: a baseline in step 1, the
red/green pair in step 2, and the final one in step 11." That is 1 + 2 + 1 = 4.
The plan's actual build calls are at plan:59, plan:111, plan:163 and plan:544.
**Fix:** "four builds: a baseline, the red/green pair, and the final one."

## MINOR 6 — step 2b's stated reason for public placement is false for all four of these stores

Plan:117-119: "Public placement is deliberate: `requires { T::SAVE_BUDGET; }`
(`PersistableStore.h:112`) is a soft test, and **a private constant is silently
ignored rather than rejected**."

A private constant is silently ignored only in a store that is *not* a friend of
`PersistableStore<T>`. All four of these stores are: `CrossPointState.h:11`,
`CrossPointSettings.h:13`, `WifiCredentialStore.h:41`, `RecentBooksStore.h:26`
each declare `friend class PersistableStore<…>`, so a private `SAVE_BUDGET` would
be picked up correctly. The spec states this accurately (spec:154-163: "works
today only because of `friend class PersistableStore<…>` …, a line nothing
connects to the budget"), and the spec review measured both cases on the xtensa
toolchain (`privateWithFriend=8192`, `privateNoFriend=45000`). The plan compressed
that into a claim that is wrong for the code it is about.

The **decision is right and should stand** — public placement makes the budget
independent of a `friend` line that nothing connects to it, which is precisely the
spec's argument. Only the justification needs correcting, and it matters because
this sentence is the kind that gets copied into a code comment. **Fix:** "a private
constant works here only because each store friends `PersistableStore<T>`
(`CrossPointState.h:11`, `CrossPointSettings.h:13`, `WifiCredentialStore.h:41`,
`RecentBooksStore.h:26`) — a link nothing connects to the budget and a future
cleanup would sever without a diagnostic. Public placement removes that
dependency."

Two smaller prose slips in the same family, both harmless but both citable:

- Step 4 (plan:211) calls its sed "the four conversions". It is seven call sites
  across four files (main 4, SdCardFontSystem 1, PublicationDownloader 1,
  CrossPointWebServer 1), plus one deletion. Steps 3, 5 and 6 all carry a site
  count in their headings; step 4 is the only one that does not, which is why the
  55 does not visibly close when reading the headings alone.
- Step 8 (plan:353-354) says the model pattern "is four lines below them, at
  `RecentBooksStore.cpp:85-87`". `:85` is 23 lines below `:62`, 11 below `:74` and
  17 *above* `:102`.

---

## Summary

No BLOCKERs. The plan is unusually literal and it mostly earns that: every grep
count, every file census, every anchor block and every insertion point reproduces
exactly against the tree, the spec-review fixes are correctly carried forward, and
the red/green in step 2 is a real compile-time failure rather than a stand-in. The
two MAJORs are a `sed` idiom that silently does nothing on this platform — which
guts four of ten implementation steps until the one-token fix is applied — and one
spec-review acceptance condition (file the A5 truncation issue) that the plan
demoted from a step back to the sentence the review rejected. Both are fixable in
place without a decision from the human. The six MINORs are an off-by-one line
number, an off-by-one line number, a nondeterministic grep ordering, two dead shell
variables, a miscount of builds, and one wrong-but-inconsequential justification.

BLOCKERS: 0
MAJORS: 2
MINORS: 6

VERDICT: CLEAR
