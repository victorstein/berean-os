---
name: scope-discipline
description: Feature-scope discipline for a dedicated JW study device (not a general e-reader, not a tablet). Use when adding a feature, a new activity, a new lib, a setting, or a dependency, or when a request would grow the firmware's surface. Covers the SCOPE.md test, the cost gate in SRAM, flash and panel refreshes, preferring no-code or existing-mechanism solutions, awareness of the existing activity surface, and how to push back on out-of-scope asks.
---

# Scope Discipline

The mission: make the Bible and the JW study publications fast to reach, fast to read, and
trustworthy to mark up, on one board. `SCOPE.md` is the source of truth for what is in and out, and
it gives the *reasoning* for each exclusion, not just the verdict. Read it before adding surface.
This is the gate to run before writing a new feature.

## The gate

Before adding a feature, activity, lib, setting, or dependency, answer in order:

1. **Is it in `SCOPE.md` section 2?** Explicitly out: notes, JW Library interop, on-device
   full-text search, the general e-reader features the fork removed (OPDS, KOReader sync, the
   dictionary, the TXT and XTC readers), interactive apps, active connectivity, PDF, and support for
   any board but the X4 Pro. If it is out, say so and stop.
2. **Is it this phase's job?** `ROADMAP.md` sequences the work, and building Phase 2's UI on Phase
   1's unfinished address model is how a phase's acceptance criterion becomes unmeetable. "Later
   phase" is a real answer.
3. **Does it materially improve reading or study on this device?** If the benefit is "nice to have",
   or serves a different use case, it is out.
4. **What does it cost?** Three budgets, not one:
   - **Internal SRAM**, which is still the ~380 KB-class resource everything competes for.
   - **Flash**, which is the entire budget a future BLE stack has to fit into.
   - **Panel refreshes**, which the user feels directly. A flow that adds a screen to a common path
     is more expensive than it looks.

   PSRAM is plentiful but is not free: an external SPI bus, roughly an order of magnitude slower,
   unusable from an ISR or while the flash cache is suspended, DMA-constrained. Putting something in
   PSRAM is a decision to justify, not an escape from the question.

   Quantify with `scripts/firmware_size_history.py` and `scripts/script_profile_mem.sh` rather than
   guessing.
5. **Can it be done with no new code?** Prefer an existing activity, an existing setting, or a doc
   over a new code path. The cheapest feature is the one already built.

If a request fails the gate, push back with the specific reason and the `SCOPE.md` basis, and offer
the in-scope alternative. Make the call and say why; do not just hand over a menu.

## Study data is not like the rest

Reading positions, highlights and tags are the only things on this device that cannot be
re-downloaded. Anything that writes them inherits a hard contract from `SCOPE.md` section 5: atomic
writes, an explicit serialised-byte budget checked *before* writing rather than truncating, a format
version a future build refuses rather than reinterprets, and streaming for anything that can exceed
~40 KB. A feature that cannot meet that bar does not ship — and "it will not get that big" is the
assumption that costs a user a year of tagging.

## Surface awareness

The firmware still carries dozens of inherited activities, and Phase 2 replaces the shell around
them. Each new screen is permanent RAM, permanent maintenance, and another thing every future
refactor must not break. Default to extending an existing activity or setting. New top-level surface
needs a real justification, not "it would be convenient" — and if it belongs to the shape the design
is heading towards, it belongs in that phase's plan, not bolted onto the current shell.

## Settings are not free

A new setting is a field to persist, migrate, validate, translate and render, plus combinatorial
test burden, plus a row in the web settings API. Add one only when readers genuinely differ;
otherwise pick a sensible fixed default. Deleting one later is worse than never adding it: a
persisted enum cannot be renumbered without silently reassigning every user who had it.

## Self-review

- [ ] Checked against `SCOPE.md`; not on the out-of-scope list.
- [ ] Checked against `ROADMAP.md`; it belongs in this phase.
- [ ] Stated the concrete reading or study benefit, not a generic "useful".
- [ ] Named the SRAM, flash and refresh cost (measured, not guessed) and why the benefit wins.
- [ ] If it writes study data: atomic, byte-budgeted, version-gated, streamed where large.
- [ ] Checked whether an existing activity/setting/doc already covers it.
- [ ] New setting (if any) is justified by a real need, not added "just in case".
