#pragma once

// Whether one launcher paint has to be non-differential.
//
// cleanInitialRefresh is set by the wake path when the panel is still showing a
// frame the launcher did not draw: the sleep screen, after a wake that found no
// Quick Resume frame on the card. A differential refresh cannot clear that, so
// the first paint of such an entry must not be one. Every later paint in the
// entry diffs against a baseline the launcher itself drew.
//
// firstRenderDone is "this entry has already painted", so the first paint passes
// false. Lives here, free of firmware includes, so that polarity can be tested
// on the host -- inverting it costs the wake paint its clean, and nothing the
// device shows afterwards says which way round it went.
constexpr bool launcherNeedsCleanPaint(const bool cleanInitialRefresh, const bool firstRenderDone) {
  return cleanInitialRefresh && !firstRenderDone;
}
