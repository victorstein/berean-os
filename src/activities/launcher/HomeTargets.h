#pragma once

#include <cstddef>
#include <cstdint>

#include "activities/reader/ReaderEntryIntent.h"

// What each Home target does, free of the renderer and the activity so
// test/home_layout checks every route (issue #203).
namespace HomeTargets {

enum class Target : uint8_t {
  Continue,
  GoTo,
  Recent0,
  Recent1,
  Recent2,
  Verse,
  Meetings,
  Tags,
  Search,
  Publications,
  Settings,
  COUNT
};

enum class Action : uint8_t { None, OpenReader, DownloadBible, OpenMeetings, OpenPublications, OpenSettings };

struct State {
  bool hasBible = false;
  bool hasPlace = false;
  // Rows pickRecent filled, 0..3.
  uint8_t recentCount = 0;
  bool hasPick = false;
};

struct Route {
  Action action = Action::None;
  ReaderEntryIntent::Kind intent = ReaderEntryIntent::Kind::None;
  // Only Continue replaces the old resume strip, the wake-to-carry-on path that
  // gave the reader's first page a fast refresh.
  bool allowFastInitialRefresh = false;
};

constexpr Route reader(const ReaderEntryIntent::Kind intent, const bool fast = false) {
  return Route{Action::OpenReader, intent, fast};
}

constexpr Route route(const Target target, const State& state) {
  using Kind = ReaderEntryIntent::Kind;
  switch (target) {
    case Target::Continue:
      if (!state.hasBible) return Route{Action::DownloadBible};
      return reader(state.hasPlace ? Kind::OpenAt : Kind::None, /*fast=*/true);
    case Target::GoTo:
      return state.hasBible ? reader(Kind::GoTo) : Route{};
    case Target::Recent0:
    case Target::Recent1:
    case Target::Recent2: {
      const auto slot = static_cast<uint8_t>(static_cast<uint8_t>(target) - static_cast<uint8_t>(Target::Recent0));
      return state.hasBible && slot < state.recentCount ? reader(Kind::OpenAt) : Route{};
    }
    case Target::Verse:
      return state.hasBible && state.hasPick ? reader(Kind::OpenAt) : Route{};
    case Target::Meetings:
      return Route{Action::OpenMeetings};
    case Target::Tags:
      return state.hasBible ? reader(Kind::Tags) : Route{Action::DownloadBible};
    case Target::Search:
      return state.hasBible ? reader(Kind::Search) : Route{Action::DownloadBible};
    case Target::Publications:
      return Route{Action::OpenPublications};
    case Target::Settings:
      return Route{Action::OpenSettings};
    case Target::COUNT:
      break;
  }
  return Route{};
}

constexpr bool isActive(const Target target, const State& state) { return route(target, state).action != Action::None; }

// The button-navigation ring: active targets in reading order.
inline size_t ring(const State& state, Target* out, const size_t capacity) {
  size_t count = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(Target::COUNT) && count < capacity; ++i) {
    const auto target = static_cast<Target>(i);
    if (isActive(target, state)) out[count++] = target;
  }
  return count;
}

}  // namespace HomeTargets
