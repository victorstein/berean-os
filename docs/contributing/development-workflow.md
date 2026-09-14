# How work gets done here

Ported from the `nicaraguan-laws` orchestration model and adapted to firmware.
Every rule below exists because skipping it cost something.

## The loop

```
brainstorm -> spec -> adversarial review -> revise -> plan -> issues -> dispatch -> review -> CI-gate -> merge -> teardown
```

## The orchestrator delegates; it does not implement

The session agent is an **orchestrator**. It keeps brainstorming, specs, plans,
the adversarial reviews, task breakdown, review, the CI gate, git and merges. It
delegates **every code change** to a scoped agent running in its own worktree.

**Inline is fine for trivial, cross-cutting, non-code work** — editing this file,
read-only verification, git. Don't spawn an agent to run a one-line grep.

**Destructive or outward-facing operations are the orchestrator's gate, not a
delegated task.** Flashing, publishing a release, anything touching the device's
SD card, anything hard to reverse — get an explicit go-ahead first.

## Spec before code

Designs live in `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md`. A spec that
cites a file path must cite a line number, and that line must have been read.

### Adversarial review, by someone who did not write it

Dispatch **two independent reviewers** with non-overlapping briefs so neither can
cover for the other:

1. **Claims verification** — every file:line citation, every measured number,
   every "this already exists". Verdicts CONFIRMED / WRONG / UNVERIFIABLE, with
   evidence attached.
2. **Design attack** — unhandled cases, failure scenarios with concrete inputs,
   cost arithmetic, ordering mistakes. Explicitly licensed to say the design is
   wrong.

Tell them not to fix anything. A reviewer that edits is a reviewer that stopped
looking.

This is not theatre. On the founding bereanOS spec the two reviewers
independently found the same three load-bearing errors, one of which would have
silently destroyed a year of tagging.

### Plans are per phase

A deletion, a data migration, a UI rework and a network subsystem share no
execution shape. One plan each.

## Dispatch

**Run every agent through herdr — always.** Never off-herdr, never backgrounded,
so every agent's pane and state stay visible.

1. **One GitHub issue per task** — `gh issue create`.

2. **Create the worktree**, capturing *both* ids:

   ```sh
   herdr worktree create --branch <name> --base main
   # capture .result.workspace_id AND .result.root_pane.pane_id
   ```

3. **Start the agent into that workspace**, capturing its pane id:

   ```sh
   herdr agent start <name> --workspace <ws> --cwd <worktree> \
     -- claude --dangerously-skip-permissions "<full task text>"
   # capture .result.agent.pane_id
   ```

   **Two placement footguns, both mandatory to avoid:**

   - **Pass `--workspace`.** With only `--cwd`, the pane splits into the *current*
     session's workspace instead of the worktree's.
   - **`agent start` always opens a NEW pane**, so it lands the agent in `p2` and
     orphans the worktree's root `p1` as a dead empty shell. Close it immediately:
     `herdr pane close <root_pane_id>` — safe, the agent in `p2` is untouched.

   Net result is **one pane per worktree**. Verify with `herdr pane list`.

4. **The prompt carries the full task text.** Agents do not read the plan file.
   It also points at the scoped agent definition and requires the PR body to end
   with a real GitHub closing keyword — `Closes #<issue>`. "Implements #…" does
   **not** auto-close.

## Monitor event-driven, never by polling

Confirm the integration is live first: `herdr integration status` → `claude: current`.
Without it, status is screen-scraped and lies.

Background a wait for **each** of `idle`, `done` and `blocked` per pane:

```sh
herdr wait agent-status <pane> --status idle
herdr wait agent-status <pane> --status done
herdr wait agent-status <pane> --status blocked
```

An agent finishes as **either** `idle` or `done`. `blocked` means it needs you —
`herdr agent read` / `herdr agent send`. React to whichever fires, then confirm
the PR: `gh pr list --head <branch>`.

Watch the `agent_status` field, not `status`. Watching the wrong one means the
watchers never fire.

## Review, then the CI gate

**Two-stage review per task:** spec compliance first, then code quality. They are
different questions and collapsing them lets a well-written wrong thing through.

**Verifying CI needs two sources.** `gh pr checks` collapses check runs that share
a name — a stale FAILURE beside a later SUCCESS shows only green. This has already
shown a failed PR as passing on this codebase, and the human caught it, not the
tooling.

```sh
gh pr view <n> --json statusCheckRollup
gh run list --commit "$(gh pr view <n> --json headRefOid -q .headRefOid)"
```

"No checks reported" means *not started*, not *passed*. Wait and look again.
Judging from a partial check set is the same mistake in different clothes.

## Merge, close, tear down

After merge, **verify the issue actually closed** (`gh issue view <n>`) — close it
by hand if the agent used a non-keyword phrasing.

```sh
herdr worktree remove --workspace <ws> --force
```

Never raw `git worktree remove` — it orphans the herdr workspace.

## Parallelism and collisions

Default to parallel across non-colliding surfaces. **Never two agents editing the
same files.** When two changes must touch one file, serialize them or rebase the
second onto the first before merge.

Surfaces in this repo (scoped agent definitions under `.claude/agents/` are yet to
be written — Phase 0 work):

| Surface | Owns |
|---|---|
| `epub-dev` | `lib/Epub` — parsers, unit addressing, caches |
| `ui-dev` | `src/activities`, FreeInkUI, `GfxRenderer` |
| `net-dev` | `src/network` — downloads, catalog, OTA, web server |
| `hal-dev` | `lib/hal` and the `freeink-sdk` boundary |

Route by surface, and land the lower layer first: a change spanning `lib/Epub` and
an activity dispatches `epub-dev`, lets it land, *then* dispatches `ui-dev`.

## Prime directive — follow the pattern, escalate before inventing

1. **Mirror the nearest existing example.** Find the closest thing this repo
   already does and copy its structure, naming, error handling and test. State
   which file you modelled on.
2. **If this repo has no way to do it, stop and ask.** A new dependency, a new
   architectural seam, or a new convention is a user decision, not an autonomous
   one. Surface the gap, propose options, wait.

This binds subagents too. Do not let one introduce a new pattern silently.

## Firmware-specific gates

**Formatting.**

```sh
./bin/clang-format-fix -g   # while working: Git-modified files only
./bin/clang-format-fix      # before committing: the whole tree, as CI does
```

Run the unsuffixed form before committing. `-g` only reaches files Git currently
reports as modified, so a file you create and commit is silently skipped and CI
fails on work that looked clean locally. Never invoke `clang-format` directly.

**Fresh worktrees need bootstrapping** before `pio run` or the format wrapper
works: `git submodule update --init --recursive`, plus a clang-format 21 venv.

**Releases are automated. Never push a tag by hand.** Conventional commits on
`main` drive release-please, which opens and merges a release PR, cuts the tag and
creates the release, which fires the build-and-attach workflow. A hand-pushed tag
is filtered out and does nothing. **The PR title is the release input** — a
non-conventional title contributes nothing to the changelog and may skip the bump.

**Testing.** Anything that can be pure, is. Parsers, address arithmetic,
migrations and index formats belong in the host suite with no Arduino and no
`HalStorage`, the way `WolWeekScan`, `PubMediaJson` and `VerseAnchors` already
are. A unit that needs hardware to test is a unit that will not be tested.

Device verification is the human's: on-device behaviour, heap after each screen,
and a cache-clear re-parse whenever a format version moves.

## Two habits worth keeping

**Cite or don't claim.** "This already exists" and "this is already tested" are
the two claims most likely to be wrong and most expensive to discover late.

**Report what happened.** If a check was skipped, say so. A green summary over an
unverified run is worse than no summary.
