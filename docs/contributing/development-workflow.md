# How work gets done here

This is the loop that built the Bible navigation, the meeting downloader and the
OTA release pipeline in crosspoint-x4pro, written down so it survives a new
session. It is not ceremony: every step exists because skipping it cost something.

## The loop

```
brainstorm -> spec -> adversarial review -> revise -> plan -> dispatch -> verify -> merge
```

### Spec before code

Designs live in `docs/superpowers/specs/YYYY-MM-DD-<topic>-design.md`. A spec that
cites a file path must cite a line number, and the claim at that line must be
checked, not remembered.

### Adversarial review, by someone who did not write it

Dispatch **two independent reviewers** with non-overlapping briefs so neither can
cover for the other:

1. **Claims verification** — every file:line citation, every measured number, every
   "this already exists" claim. Verdicts are CONFIRMED / WRONG / UNVERIFIABLE with
   evidence attached.
2. **Design attack** — unhandled cases, failure scenarios with concrete inputs,
   cost arithmetic, ordering mistakes. Explicitly licensed to say the design is
   wrong.

Tell them not to fix anything. A reviewer that edits is a reviewer that stops
looking.

This is not optional theatre. On the bereanOS spec the two reviewers independently
found the same three load-bearing errors, including one that would have silently
destroyed a year of the user's tagging.

### Plans are per phase

One plan per phase, not one plan per project. A deletion, a data migration, a UI
rework and a network subsystem share no execution shape, and a single plan
covering all four is a plan for none of them.

### Dispatch through herdr

Parallel work runs in herdr worktrees, one agent per workspace.

- A fresh worktree needs `git submodule update --init --recursive` **and** a
  clang-format 21 venv before `pio run` or `./bin/clang-format-fix` will work.
- `herdr agent send` / `pane send-text` do **not** press Enter. Follow with
  `herdr pane send-keys <id> Return` or nothing is submitted.
- Watch the `agent_status` field, not `status`. Watching the wrong one means your
  watchers never fire and you sit waiting on work that finished long ago.
- `herdr wait agent-status --status idle` does not fire while an agent is
  `blocked` on a permission prompt. Treat `blocked` and timeout as "retry later",
  never as "done".
- `worktree create --branch N --base REF` makes an **empty** branch. For an
  existing branch use `worktree open --branch N`.

### Verifying CI — use two sources

**`gh pr checks` collapses check runs that share a name.** A stale FAILURE sitting
beside a later SUCCESS shows only green, and a PR that failed reads as passing.
This has happened on this codebase and was caught by the human, not the tooling.

Check both:

```sh
gh pr view <n> --json statusCheckRollup
gh run list --commit "$(gh pr view <n> --json headRefOid -q .headRefOid)"
```

Also: "no checks reported" means *not started*, not *passed*. Wait, then look
again. Judging from a partial check set is the same mistake wearing a different
hat.

### Formatting

```sh
./bin/clang-format-fix -g   # while working: Git-modified files only
./bin/clang-format-fix      # before committing: the whole tree, as CI does
```

Run the unsuffixed form before committing. `-g` only reaches files Git currently
reports as modified, so a file you create and commit is silently skipped and CI
fails on work that looked clean locally. Never invoke `clang-format` directly.

## Releases

**Never push a tag by hand.** Releases are automated: conventional commits on
`main` drive release-please, which opens and merges a release PR, cuts the tag and
creates the release, which in turn fires the build-and-attach workflow. A manually
pushed tag is filtered out and does nothing.

The **PR title is the release input** — a non-conventional title contributes
nothing to the changelog and may skip the version bump.

## Testing

Anything that can be pure, is. Parsers, address arithmetic, migrations and index
formats belong in the host suite with no Arduino and no `HalStorage`, the way
`WolWeekScan`, `PubMediaJson` and `VerseAnchors` already are. A unit that needs
hardware to test is a unit that will not be tested.

Device verification is the human's: on-device behaviour, heap after each screen,
and a cache-clear re-parse whenever a format version moves.

## Two habits worth keeping

**Cite or don't claim.** "This already exists" and "this is already tested" are
the two claims most likely to be wrong and most expensive to discover late.

**Report what happened.** If a check was skipped, say it was skipped. A green
summary over an unverified run is worse than no summary.
