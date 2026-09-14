# Meetings as a library, not a pipeline

**Status**: scoped, not implemented
**Date**: 2026-09-14

## The problem

The Meetings tile opens `MeetingDownloadActivity`, which is a download pipeline
with no browsable state:

```
WIFI_SELECTION -> RESOLVING -> DOWNLOADING -> FINISHED("Done")
```

`runSequence()` reads the RTC, derives an ISO week, scrapes that week's
`wol.jw.org` meetings page for the two publication issue codes, downloads each,
and paints `tr(STR_DONE)`. A publication already on the card is skipped with a
log line (`"Skipping %s, already on the card"`).

So the common case — both publications already downloaded, the user wants to
read one — ends at a screen that says "Done" and offers nothing. There is no
list of the week's publications, no indication of which are present, and no way
to open either one.

## The constraint that shapes everything

**Which issues belong to this week is only knowable from the network.**
`WolWeekScanner` recovers the issue codes by scraping the week's meetings page;
the codes appear nowhere on the device. Two consequences:

1. A screen that resolves before it renders shows nothing until the network
   answers. `runSequence`'s own comment records that the resolve phases "block
   the loop task for up to a minute each" and that `fetchUrl` takes neither a
   progress nor a cancel hook.
2. Without caching, the device cannot name this week's publications offline even
   when it already holds both files.

`PubKeyRegistry` records `{symbol, issue, language}` per downloaded path, so the
device can always say *what it has*. It cannot say *whether that is this week's*
without the scan.

## Design

### 1. Cache the week to issue mapping

A small versioned store keyed by ISO week:

```
{ "v": 1, "w": { "2026-37": { "w": "202607", "mwb": "202609" } } }
```

Written after a successful scan. This is the only piece of "this week" knowledge
that requires the network, and it changes once a week.

Per the storage discipline in `CLAUDE.md`: `writeDocToFileAtomic`, an explicit
serialised-byte budget checked before writing, and a format version a future
build refuses rather than reinterprets. Entries are pruned to a small number of
recent weeks so the file cannot grow without bound.

This is a cache, not study data: losing it costs one rescan, never a user's
work. It still lives under `/.berean/` because it is keyed to publication
identity rather than to a file path.

### 2. Meetings becomes a library screen

On entry, paint immediately from local state — registry plus an existence check
— with no network:

```
La Atalaya (ed. estudio) · 2026-07      Abrir
Guía de actividades · 2026-09           Descargar
```

Row order is not significant; `PUBLICATION_ORDER` is retained for consistency
with the download sequence.

- **Abrir** goes straight to the reader.
- **Descargar** runs the existing download for that one publication.

### 3. Stale weeks resolve automatically

When the cached mapping does not cover the current ISO week, the screen resolves
on entry without being asked.

**This is a user decision, and it has a cost**: entering Meetings in a new week
requires WiFi and can block for up to a minute per the resolve comment above.

The mitigation is ordering, not avoidance. The screen paints whatever the cache
holds *first* — last week's rows, or an empty state — and only then resolves,
updating in place when the answer arrives. `runSequence` already establishes
this pattern with its leading `requestUpdateAndWait()`, and the reason given
there is exactly this one. A blank screen for a minute is not acceptable; a
stale screen that refreshes itself is.

### 4. The launcher tile follows

Once the mapping is cached, the Meetings tile can show *this week's*
publication and whether it is downloaded, instead of the best candidate a card
scan happens to find.

That retires the filename fallback added in `ff33ed7c`
(`findMeetingPublicationOnCard`) for anything downloaded from that point on. The
fallback still earns its place for publications downloaded before
`PubKeyRegistry` existed, which is why it was needed in the first place.

## Testing

The week-mapping cache is pure apart from its file I/O: key derivation, issue
lookup, staleness comparison against a supplied ISO week, and pruning are all
host-testable and should be covered in `test/`.

The screen, the resolve path, and the download path are device-dependent and
cannot be host-tested. They must be verified on hardware and reported as such.

## Sequencing risk

The Phase 3 catalog work was briefed to factor out and reuse the download path
inside `MeetingDownloadActivity` for its download-by-symbol feature. That is the
same file this design restructures.

To avoid a merge conflict across a file that owns irreplaceable-adjacent
behaviour, the pure cache and its tests land first — they touch no existing
file — and the screen work waits until the catalog change has landed and its
shape is known.
