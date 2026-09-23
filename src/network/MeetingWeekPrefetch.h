#pragma once

#include <cstdint>
#include <string>

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
// `week` set, when a resolve is worth a page fetch now. Reads the SD card, so
// the caller holds whatever lock its other SD work does.
bool due(bool enabled, bool clockSynced, IsoWeek& week);

struct Hooks {
  void* ctx = nullptr;
  // Called once, at the first point a skip can be honoured. Before it the
  // transfer is connecting and nothing is polled, so a skip hint shown earlier
  // would be a promise the device cannot keep.
  void (*onSkippable)(void* ctx) = nullptr;
  // Polled from then on. Doubles as the caller's input pump; return true once
  // the user has asked to stop.
  bool (*skipRequested)(void* ctx) = nullptr;
};

struct ResolvedWeek {
  std::string watchtower;
  std::string workbook;
};

// Scrapes `week`'s meetings page. Gives up after BUDGET_MS or when the hooks
// say so. Touches no storage; the caller hands a success to record(). Every
// failure -- skipped, timed out, offline, nothing on the page -- is only
// logged: the user did not ask for this, so there is nothing to report to them.
// Marks `week` attempted for the rest of this boot, whatever the outcome.
bool resolve(const IsoWeek& week, const Hooks& hooks, ResolvedWeek& out);

// Writes a resolved week into MeetingWeekCache (atomic, budget-checked).
bool record(const IsoWeek& week, const ResolvedWeek& resolved);

// Long enough for the meetings page on a working link, short enough that a
// connection made for something else never waits on this for long.
inline constexpr unsigned long BUDGET_MS = 20000;

// Per-operation network timeout for this lookup alone. It is the only bound on
// the TCP connect and TLS handshake, where no skip is polled; SecureClient
// retries a failed handshake once with TLS 1.2, so that phase can take up to
// four of these. Name resolution keeps the network stack's own timeout.
inline constexpr uint32_t NETWORK_TIMEOUT_MS = 6000;

}  // namespace MeetingWeekPrefetch
