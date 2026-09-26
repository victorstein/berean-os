#pragma once

#include <string_view>

// Every fixed path this firmware reads or writes on the SD card. Full paths are
// written out whole because C++20 cannot join named constants at compile time;
// the static_asserts below keep each one under its root.
namespace sdpaths {

inline constexpr char CROSSPOINT_DIR[] = "/.crosspoint";
inline constexpr char BEREAN_DIR[] = "/.berean";

inline constexpr char SETTINGS_FILE[] = "/.crosspoint/settings.json";
inline constexpr char STATE_FILE[] = "/.crosspoint/state.json";
inline constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent.json";
inline constexpr char WIFI_FILE[] = "/.crosspoint/wifi.json";
inline constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";
inline constexpr char HIGHLIGHTS_DIR[] = "/.crosspoint/highlights";
inline constexpr char BOOKMARKS_DIR[] = "/.crosspoint/bookmarks";

inline constexpr char PUBKEYS_FILE[] = "/.berean/pubkeys.json";
inline constexpr char TAGS_FILE[] = "/.berean/tags.json";
inline constexpr char MEETING_WEEKS_FILE[] = "/.berean/meeting-weeks.json";
inline constexpr char MIGRATION_REPORT_FILE[] = "/.berean/migration-report.json";
inline constexpr char MIGRATION_LEDGER_FILE[] = "/.berean/migration-ledger.json";
inline constexpr char SEARCH_DIR[] = "/.berean/search";
inline constexpr char SEARCH_INDEX_FILE[] = "/.berean/search/bible.idx";
inline constexpr char SEARCH_CHECKPOINT_FILE[] = "/.berean/search/bible.partial";
inline constexpr char PASSAGES_DIR[] = "/.berean/passages";
inline constexpr char UNITS_DIR[] = "/.berean/units";
inline constexpr char COMPLETION_DIR[] = "/.berean/completion";

constexpr bool isUnder(std::string_view path, std::string_view dir) {
  return path.size() > dir.size() + 1 && path.starts_with(dir) && path[dir.size()] == '/';
}

static_assert(isUnder(SETTINGS_FILE, CROSSPOINT_DIR));
static_assert(isUnder(STATE_FILE, CROSSPOINT_DIR));
static_assert(isUnder(RECENT_BOOKS_FILE, CROSSPOINT_DIR));
static_assert(isUnder(WIFI_FILE, CROSSPOINT_DIR));
static_assert(isUnder(SLEEP_FRAME_FILE, CROSSPOINT_DIR));
static_assert(isUnder(HIGHLIGHTS_DIR, CROSSPOINT_DIR));
static_assert(isUnder(BOOKMARKS_DIR, CROSSPOINT_DIR));

static_assert(isUnder(PUBKEYS_FILE, BEREAN_DIR));
static_assert(isUnder(TAGS_FILE, BEREAN_DIR));
static_assert(isUnder(MEETING_WEEKS_FILE, BEREAN_DIR));
static_assert(isUnder(MIGRATION_REPORT_FILE, BEREAN_DIR));
static_assert(isUnder(MIGRATION_LEDGER_FILE, BEREAN_DIR));
static_assert(isUnder(SEARCH_DIR, BEREAN_DIR));
static_assert(isUnder(SEARCH_INDEX_FILE, SEARCH_DIR));
static_assert(isUnder(SEARCH_CHECKPOINT_FILE, SEARCH_DIR));
static_assert(isUnder(PASSAGES_DIR, BEREAN_DIR));
static_assert(isUnder(UNITS_DIR, BEREAN_DIR));
static_assert(isUnder(COMPLETION_DIR, BEREAN_DIR));

}  // namespace sdpaths
