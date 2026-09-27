Tier: heavy

# Issue #172 — plan review 0

Plan: `docs/superpowers/plans/2026-09-27-issue-172-plan.md` (at `1c90485e`).
Spec: `docs/superpowers/specs/2026-09-27-issue-172-design.md`.

## What was checked, and how

I executed the plan in a throwaway `git worktree add --detach` of `1c90485e` in my scratch directory. I pulled every script out of the plan by line range and ran it without editing it. I did not run `pio`, as the brief asked.

- **Step 0 pre-flight.** `git diff --stat 7c080671 HEAD` printed nothing for the three source files, and so did `git diff --stat HEAD origin/main` after `git fetch`. The anchors still hold.
- **Step 1, `route_table.py`.** It prints 21 routes, from `HTTP_GET /` to `HTTP_POST /upload`, with `total 21` on stderr. The working tree matches: `SAME`.
- **Steps 3–6, `split_routes.py utils|settings|fonts|files`.** Every phase ran without an assertion firing and printed the `wrote` lines the plan predicts. The route table stayed `SAME` after each phase. Line counts: 736 / 754 / 79 (the plan predicts 737 / 754 / 79; see m5).
- **Move-only check (spec W7).** I normalised the original `CrossPointWebServer.cpp` (`webroutes::` prefix, `server->`→`server.`, `server.get()`→`&server`). I then compared its line multiset against the union of the five resulting `.cpp` files, in both directions. The only lines that are new or gone are:
  - includes;
  - the out-of-class signatures;
  - the `registerRoutes` bodies and the old `on()` lines they replace;
  - the new WebDAV-ordering comment;
  - the `scanFiles` capture `[this, &server, …]`;
  - `if (!serverRunning) {`.

  That is exactly the list in W7. No moved body references an owner member (`apMode|port|wsServer|running|udp…` grep over the three group files is empty). `lastLoggedSize` stays function-static, and the pre-existing `const_cast` moves unchanged.
- **Registration order (spec W5).** In the new `begin()`, the order is:
  1. `/`, `/api/status`, `/migration`;
  2. the three `registerRoutes` calls;
  3. Wi-Fi;
  4. `onNotFound`;
  5. `addHandler(webDavHandler)`.

  In the header, `FileRoutes fileRoutes{running};` is declared after `bool running` (spec "Class shapes").
- **Step 10 (red).** The `test/CMakeLists.txt` anchor edit applied cleanly. `cmake --build … --target BootDecisionsTest` fails with `fatal error: 'boot/BootDecisions.h' file not found`, as the plan predicts.
- **Step 11 (green).** No error or warning lines, and `[  PASSED  ] 19 tests.`
- **Step 12, `split_setup.py`.** It wrote `src/main.cpp`. I compared the new stages with the original `setup()` (`main.cpp:340-606` at HEAD) statement by statement:
  - The order is the same and the comments are kept.
  - `decodeSilentReboot` and `resolveBootResume` are algebraically identical to `:365-367` and `:467-470`.
  - The `switch` over `BootRoute` performs the same call with the same arguments in each branch as the old `if` chain `:559-586`.
  - The eager `isPressed(Back)` read is `const` down to `InputManager.cpp:472` (`MappedInputManager.cpp:57-63, 272`; `HalGPIO.cpp:178-183`).
  - Nothing outside `main.cpp` names `SILENT_REBOOT_*` or `BootResume`, and no new type name collides with an existing one (grep over `src` and `lib`).
  - The types line up: `lastSleepFromReader` is `bool` and `readerActivityLoadCount` is `uint8_t` (`CrossPointState.h:34-35`), so the braced `BootRouteInputs` initialiser has no narrowing.
- **Step 14, host tests.** Full `ctest`: `100% tests passed out of 1139`, which is the figure the plan predicts.
- **FILES lines.** Every tracked path the plan creates or modifies is covered by `plan:10-16`:
  - `CrossPointWebServer.{h,cpp}`;
  - the eight new `src/network/*` files;
  - `src/main.cpp`;
  - `src/boot/BootDecisions.h`;
  - `test/boot_decisions/`;
  - `test/CMakeLists.txt`.

  The lines sit at column 0, outside any code fence, and use repo-relative paths and directory prefixes, not globs.
- **Spec → plan mapping:**

  | Spec item | Plan step |
  |---|---|
  | W1–W4 | the `FILES_H`/`FONTS_H`/`SETTINGS_H` shapes |
  | W5 | `FILES_ROUTES_NEW` and step 7 |
  | W6 | phase `utils`, with the helpers kept in `FileRoutes.cpp`'s anonymous namespace |
  | W7 | as checked above |
  | W8 | `plan:720-729` |
  | W9 | `upload` private in `FileRoutes` |
  | S1–S8 | `NEW_SETUP` and step 13 |
  | Testing 1–5 | steps 10/11, 1–7, 8, 8/14 and 14 |

  Testing 6 (device checks) goes into the PR in step 16.

I did not build the firmware. The plan's `[SUCCESS]`, flash and `-Wall` claims come from its own trial and are unverified here.

## Findings

No BLOCKER and no MAJOR findings. The MINORs below can be fixed inline.

### MINOR m1 — The build cadence runs against the repo's build rule and costs several serialised full rebuilds

- **Claim:** `pio run` at steps 3, 4, 5, 6, 8 (twice), 12 and 14 (twice) is needed "after every phase".
- **Problem:** `CLAUDE.md` ("Testing checklist" §1) says:
  - build once after the last code edit;
  - do not repeat a target that already passed;
  - do not rebuild after formatting.

  The builds also go through a lock shared with sibling workers. Each `PLATFORMIO_BUILD_FLAGS="-Wall"` build changes the flags, which forces a full rebuild, and so does the next plain build after it (step 12 after step 8, and step 14's plain build after nothing). So about four builds are full rebuilds.
- **Evidence:** `plan:775, 786, 797, 808, 840, 844, 1539, 1565, 1568`; the plan's own note at `plan:856`.
- **Fix:**
  - Drop the builds in steps 3–5. The script's asserts and the route table catch drift, and step 6 still gives a bisect point for commit 1.
  - Drop the step 12 build, since step 14 builds the same tree.
  - Keep one plain and one `-Wall` build per commit. If `clang-format-fix` changes nothing in a `.cpp` or `.h` beyond whitespace, say that the step 6 build stands.

### MINOR m2 — "The script … writes nothing" is not true for three of the four phases

- **Claim:** "the script stops with an `AssertionError` and writes nothing" (`plan:25-27`).
- **Problem:** Three phases write their files before running the header assertions:

  | Phase | Writes before the header assertions | Header assertions |
  |---|---|---|
  | `settings` | `CrossPointWebServer.cpp` and the new `.h`/`.cpp` (`plan:334-336`) | `plan:338-351` |
  | `fonts` | the same files (`plan:435-437`) | `plan:439-469` |
  | `files` | the same files (`plan:668-680`) | `plan:682-747` |

  A header that has drifted would leave the tree half-moved.
- **Evidence:** as cited above.
- **Fix:** In each phase, compute all texts first and write at the end. Alternatively, change the plan text to say: "on an assertion, run `git checkout -- src/network && git clean -f src/network`, then stop and report."

### MINOR m3 — Spec Testing 2's "match `docs/webserver-endpoints.md`" has no step

- **Claim:** Spec `:257-259` requires that the sorted route set be equal (21 routes) and match `docs/webserver-endpoints.md`.
- **Problem:** No step compares against the doc. The doc has 17 endpoint headings (`docs/webserver-endpoints.md:27-376`), and the four page routes `/`, `/files`, `/settings` and `/fonts` are not among them. A literal executor cannot tell whether "match" is met.
- **Fix:** Add one line to step 1 comparing the `GET|POST /…` headings of the doc with `routes-before.txt`. The PR should say the 17 API routes are documented and the four HTML page routes are intentionally not documented. No doc change is needed.

### MINOR m4 — Spec Testing 3's PR statement is missing from the step 16 body list

- **Claim:** Spec `:262-263`: "the PR states that the only non-moved lines are declarations, registration, and the body edits W7 lists."
- **Problem:** Step 8 describes the `--color-moved` aid (`plan:859-861`), but step 16's list of PR-body contents (`plan:1612-1629`) leaves out the statement.
- **Fix:** Add a "Move-only" bullet to step 16 with the `git diff --color-moved=dimmed-zebra` command and that statement.

### MINOR m5 — Step 6's quoted pre-format line count is off by one

- **Claim:** "737 / 754 / 79 lines" (`plan:814`).
- **Problem:** The plan's own scripts on `1c90485e` produce `736` for `CrossPointWebServer.cpp`. A literal executor who treats the quoted figures as the pass condition could stop here.
- **Fix:** Change 737 to 736, or say the counts are approximate and informational.

### MINOR m6 — `FileRoutes::UploadState` is public, while the spec's class shape shows it private; the plan does not say why

- **Claim:** In spec "Class shapes" (`:212-213`), `UploadState` sits under `private:`.
- **Problem:** `FILES_H` declares the struct under `public:` (`plan:492-509`) and keeps only the `upload` member private. That is needed, because the file-scope `static bool flushUploadBuffer(FileRoutes::UploadState&)` (W7, moved "unchanged") has to name the type. But the plan never records it as a deviation, so a reviewer comparing the result with the spec will flag it.
- **Fix:** Add one sentence under the phase table saying the type stays public, as it was in `CrossPointWebServer.h:36`, because `flushUploadBuffer` names it, and that W9's "private state" refers to the `upload` member.

### MINOR m7 — The gitignored helper outputs are not on a `FILES:` line

- **Claim:** `plan:10-16` lists the files the plan touches.
- **Problem:** Several steps also create files under `build/`, which is gitignored (`.gitignore:13`), so none of them can reach a commit or collide with a sibling worktree:
  - `build/route_table.py`, `build/split_routes.py`, `build/split_setup.py`;
  - `build/routes-*.txt`;
  - `build/pio-*.log`.

  I do not rank this as a lock violation: the same reasoning would apply to `.pio/` and `build/test/`, which no plan lists. Still, the plan is ambiguous against a literal "every file a step touches."
- **Fix:** Add `FILES: build/` at column 0 so the lock is unambiguous. It costs nothing.

VERDICT: CLEAR
