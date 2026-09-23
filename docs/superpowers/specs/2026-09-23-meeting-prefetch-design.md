# Meeting-week prefetch (issue #36)

The Meetings tile matters most on a meeting night, when the hall's WiFi is least
likely to be reachable. This change resolves a meeting week (the wol.jw.org
page scan that names the week's Watchtower and workbook issues) whenever WiFi
connects for another reason. The answer goes into `MeetingWeekCache`, so the
Meetings screen can name that week's publications offline.

## Decisions

**Metadata only. No EPUB.** Of the two options the issue lists, metadata-only
is the smaller one. It removes the retention question, and the week scan is the
part that needs the network at an awkward moment. One cache entry is three short
strings. `MeetingWeekTable::MAX_WEEKS` (13) already bounds the file
(`src/network/MeetingWeekTable.h`), and every write goes through
`writeDocToFileAtomic` after a budget check (`MeetingWeekCache.cpp`, `save`).

**Which week.** The current week if the cache cannot name it. Otherwise next
week if the cache cannot name that one. Otherwise nothing, with no network
traffic. Each opportunity resolves one week at most, so it costs one page fetch
at most. The current week comes first because it is the one the Meetings screen
shows (`MeetingsActivity.cpp:53-54`). If only next week were held, a missing
current week would fall back to `table.newest()` and show next week's issues as
though they were this week's.
A cache entry naming no issue counts as missing. `MeetingDownloadActivity`
records a week before it checks `scanner.count()`
(`MeetingDownloadActivity.cpp:154`), so an empty entry can exist, and it answers
nothing. The prefetch itself never records an empty entry, because a week
wol.jw.org has not published yet would then look settled. This logic is the pure
`meetingWeekToPrefetch` / `isoWeekAfter` in `src/network/MeetingPrefetchPlan.cpp`
and is host-tested, including 53-week years.

**One hook point: `WifiSelectionActivity`, on a successful connection.** Every
flow the issue names reaches the network through this activity: the OTA check
(`OtaUpdateActivity.cpp:84`), the catalog browse and download
(`CatalogSearchActivity.cpp:332`), and any connection made there. So one site
covers all of them. The prefetch runs in `checkConnectionStatus` after the
first-connection NTP sync. That sync is the existing example of opportunistic
work done at this moment, and it is also what gives a first connection a date
to ask about. `MeetingDownloadActivity` opts out
(`WifiSelectionActivity(..., meetingPrefetch=false)`). It scans the current week
itself, and one connection should not fetch the same page twice.

**No new task.** The prefetch runs on the loop task inside the existing
connection step, while the user is already waiting on a connection. Its SD work
is one read of the week cache and at most one atomic save, both through
`HalStorage`. Auto-sleep cannot interrupt the save: the inactivity check runs in
the main loop (`src/main.cpp:636-694`), and this whole pass is inside one
activity `loop()` call.

**Bounded and cancellable.** The cancel hook #22 asked for is on the streaming
`fetchUrl`, but it is a flag, and nothing sets that flag while the transfer waits
for response headers. `HttpDownloader` now has one extra overload that takes an
abort predicate (`AbortCheck`). It is polled wherever `cancelFlag` was: in the
esp_http_client read loop, and on the wolfSSL path (the one this build uses,
`platformio.ini:51`) in `SecureHttpClient`'s header and body loops. The
prefetch's predicate enforces a 20 s budget (`MeetingWeekPrefetch::BUDGET_MS`)
and pumps input, so a key release, tap, back or home gesture skips it. The
screen reads "Connected! / Looking up upcoming meeting publications..." with a
Skip hint.

Limit, stated rather than hidden: nothing is polled during the TCP connect and
TLS handshake, so that phase is bounded only by `HttpDownloader`'s existing
per-operation timeout (60 s, `HttpDownloader.cpp`, `HTTP_TIMEOUT_MS`). Bounding
the handshake would mean changing `SecureHttpClient` in the SDK.

**Setting: on by default.** "Look up meetings when WiFi connects" is under
Settings → System (`meetingPrefetch`, default 1). On by default because the
feature only helps on a night with no network, and a user who never found the
setting would lose that night. Metadata-only costs one small page fetch a week
and puts nothing on the card. A user who finds even that intrusive can turn it
off.

**Failure is silent.** Every failure (skipped, timed out, offline, the page
names nothing, the cache is not written) returns without touching the cache and
is logged under `MEETPF`. The user did not ask for the fetch, so there is
nothing to report to them.

## Known limitation

A prefetch that runs close to its full bound can use up most of a 1-minute
auto-sleep timeout, so the device may sleep soon after the connection finishes.
That affects convenience only: the cache write has already completed atomically
in the same pass.
