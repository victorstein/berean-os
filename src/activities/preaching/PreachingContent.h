#pragma once

#include <Preaching/QuestionParser.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PreachingCardLayout.h"
#include "PreachingOrigin.h"
#include "study/PreachingPassages.h"

class GfxRenderer;
class PreachingSession;

// One topic's cards: a question read from the Bible, or the Bible passages carrying a tag.
// `cards` views into `parsed`, `passages` and the session's question labels, so a TopicCards is
// never copied or moved -- moving a short std::string relocates its bytes -- and lives behind a
// std::unique_ptr; replacing a topic swaps pointers.
struct TopicCards {
  PreachingOrigin::Kind kind = PreachingOrigin::Kind::None;
  int question = 0;
  study::TagId tag{};
  std::string header;
  preaching::Question parsed;
  std::vector<PreachingPassages::Passage> passages;
  std::vector<preaching::CardInput> cards;
  // Per Verse ordinal: its block in `parsed` (a question) or its passage (a tag).
  std::vector<uint16_t> verseSource;

  TopicCards() = default;
  TopicCards(const TopicCards&) = delete;
  TopicCards& operator=(const TopicCards&) = delete;
  TopicCards(TopicCards&&) = delete;
  TopicCards& operator=(TopicCards&&) = delete;

  int verseCount() const { return static_cast<int>(verseSource.size()); }
};

namespace PreachingContent {

enum class LoadResult : uint8_t { Ok, Unreadable, OutOfMemory, VersesUnavailable };

// Loop task only, never under a RenderLock: the stream takes the render lock itself when the
// question's HTML has to be inflated. `out` must be freshly constructed.
LoadResult loadQuestion(const PreachingSession& session, GfxRenderer& renderer, int question, TopicCards& out);
LoadResult loadTag(const PreachingSession& session, study::TagId tag, TopicCards& out);

// The toast for a failed load.
const char* failureMessage(LoadResult result);

}  // namespace PreachingContent
