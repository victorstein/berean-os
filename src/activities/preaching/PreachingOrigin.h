#pragma once

#include <cstdint>

// Where in Predicación a screen, or the reader, came from: a question or a tag topic, and the
// Verse card within it. RAM only, and free of Arduino and StudyStore so test/ui_layout builds it.
struct PreachingOrigin {
  enum class Kind : uint8_t { None, Question, Tag };

  Kind kind = Kind::None;
  uint8_t question = 0;  // 1..IntroSection::QUESTION_COUNT when kind == Question
  uint16_t tag = 0;      // the raw study::TagId when kind == Tag
  uint16_t verse = 0;    // ordinal among the topic's Verse cards

  static constexpr PreachingOrigin ofQuestion(const int question, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Question;
    origin.question = static_cast<uint8_t>(question);
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  static constexpr PreachingOrigin ofTag(const uint16_t tag, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Tag;
    origin.tag = tag;
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  constexpr bool isSet() const { return kind != Kind::None; }
};
