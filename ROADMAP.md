# Roadmap

Four phases, in order. Each one has its own implementation plan under
`docs/superpowers/plans/`, because they share no execution shape: Phase 0 is a deletion, Phase 1 a
data migration, Phase 2 a UI rewrite, Phase 3 a network subsystem with a CI half.

The product these phases build towards is described in
[docs/superpowers/specs/2026-09-13-berean-os-design.md](./docs/superpowers/specs/2026-09-13-berean-os-design.md).
[SCOPE.md](./SCOPE.md) is the gate everything passes through first.

---

## Phase 0 — Fork and strip — **IN PROGRESS**

**Goal:** reduce the inherited CrossPoint fork to a single-board bereanOS skeleton that builds,
flashes and updates itself from its own releases, with everything that remains behaving exactly as it
does today.

- Rename to bereanOS, and point OTA at this repository. A device that can reach CrossPoint's
  releases will flash them over itself, and an upstream image would remove the features this fork
  exists for.
- Hand release state to release-please; build one board.
- Delete the TXT and XTC readers, the dictionary, KOReader sync, and the OPDS browser.
- Land the storage-discipline helper the later phases depend on.
- Rewrite the project documents.

**The input layer is not touched in this phase.** `MappedInputManager` sits in the `Activity`
base-class constructor and *implements* this device's Back gesture. Removing it here would leave the
device with no Back for the whole of Phase 1. It goes in Phase 2, in the same change that lands its
replacement.

**Acceptance:** a release build queried against its own repository finds no CrossPoint release; the
binary shrinks by 200-230 KB; `pio run`, `pio check` and the host suite are green; and every
remaining screen is entered and exited once on hardware.

---

## Phase 1 — The study data model

**Goal:** stop addressing a marked passage by a position inside one file, and start addressing it by
what it actually is.

- A unified `Unit` address: verse, numbered paragraph, or a document offset where a publication has
  neither. The existing highlight model is the degenerate third case, not a separate system.
- A `data-pid` scanner for numbered paragraphs, beside the verse scanner that already exists. They
  are two scanners, not one: the grammars differ.
- A per-publication unit index, built lazily per document on first use, with a content hash in the
  header so a re-downloaded publication invalidates itself.
- A global tag store, a reverse index for "everything tagged X", and passages keyed on publication
  identity rather than file path — so a re-download or a rename keeps your marks.
- A migration from the per-book highlight files, resumable and idempotent, that never deletes the old
  store.

**Acceptance:** `/.berean/migration-report.json`, served over the existing web server, shows
per source file what went in, what came out, and why anything was dropped. The old store is still
there.

---

## Phase 2 — The product shape

**Goal:** the device stops looking like a general e-reader.

- A launcher home screen: four tiles — Biblia, Reuniones, Buscar, Ajustes — and a resume
  strip. The common case, waking the device to carry on reading, is one tap and never touches the
  tiles.
- The new input model: long-press Right to confirm, Left+Right as a hard-bound Back that routes
  through GPIO and survives the touch controller failing, Home short for back and long for the
  launcher.
- Two-tap passage selection: long-press a word to anchor, tap another to finish, then choose tags.
- `MappedInputManager` and the button-remap screen are deleted here, with their replacement.

Two decisions this phase has to make first: whether an evicted return still needs a visible
affordance now that the capacity is 16 (#73 raised it from 3, but eviction is still silent) before
Back becomes primary; and portrait-only, which follows from a fixed Left/Right mapping.

**Acceptance:** the UX, diffable against the layer it replaces.

---

## Phase 3 — Buscar

**Goal:** any publication in the jw.org catalog, on the device, without a computer.

- A CI job that builds a slim per-language index from the published catalog and hosts it as a static
  file, fetched with `ETag`/`If-Modified-Since`. It is not a firmware release asset: an 18 KB data
  change must not cost a 5.6 MB OTA.
- The index inflated once into PSRAM while search is open, with a debounce sized to the e-ink
  refresh rather than to the scan.
- Visible staleness. A user told "not found" must be able to tell "not published yet" from "your
  index is six months old" from "the fetch failed".
- Typing a symbol directly stays available, and works with no index at all.

**Acceptance:** any publication, downloaded on device.

---

## After Phase 3

A BLE keyboard, once there is a flash budget for the NimBLE stack. See [SCOPE.md](./SCOPE.md)
section 4.

---

## Open questions

Carried from the design, to be closed in the phase that needs them:

- Whether a document filename inside a publication survives a corrected reissue. The spine-index
  fallback in the passage record stands on this being uncertain. Phase 1.
- Whether Back and Return need to be visibly different. (`ReturnStack` capacity itself was settled
  in #73: 3 -> 16.) Phase 2.
- Which languages CI builds catalog indexes for. Spanish is required; English is nearly free.
- Whether Buscar defaults to non-periodicals, with periodicals behind a filter. Reuniones already
  covers periodicals, which argues for the filter.
- Retention policy for downloaded publications on a 16 GB card.
- Whether a passage can exist with zero tags — mark it now, label it later — or whether untagging to
  zero deletes the record, as it does today.
