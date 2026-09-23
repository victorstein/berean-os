#pragma once

#include "network/WolWeekScan.h"

// Resolves a meeting week while the radio happens to be up for something else,
// so the meetings screen can name its publications on a night with no network.
//
// Runs on the caller's task -- the loop task -- rather than a task of its own:
// its SD work is one read of the week cache and at most one atomic save, and a
// second task competing for storageMutex is exactly what this must not become.
// Metadata only; no EPUB is downloaded, so nothing accumulates on the card.
namespace MeetingWeekPrefetch {

// Reads the clock and the week cache without touching the network. True, with
// `week` set, when a resolve is worth a page fetch now.
bool due(bool enabled, IsoWeek& week);

// Polled while the transfer waits. Doubles as the caller's input pump; return
// true once the user has asked to skip.
using SkipCheck = bool (*)(void* ctx);

// Scrapes `week`'s meetings page and records what it names. Gives up after
// BUDGET_MS or when `skipRequested` says so. Every failure -- skipped, timed
// out, offline, nothing on the page -- leaves the cache as it was and is only
// logged: the user did not ask for this, so there is nothing to report to them.
bool resolve(const IsoWeek& week, SkipCheck skipRequested, void* ctx);

// Long enough for the meetings page on a working link, short enough that a
// connection made for something else never waits on this for long. TLS
// connection setup is outside it: HttpDownloader's per-operation timeout
// bounds that phase, and nothing is polled during the handshake.
inline constexpr unsigned long BUDGET_MS = 20000;

}  // namespace MeetingWeekPrefetch
