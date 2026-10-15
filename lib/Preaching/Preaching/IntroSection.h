#pragma once

#include <cstdint>
#include <string_view>

// The NWT's front-matter section of twenty preaching questions,
// "Introducción a la Palabra de Dios".
namespace preaching::IntroSection {

inline constexpr int QUESTION_COUNT = 20;
inline constexpr int SLOT_COUNT = QUESTION_COUNT + 1;
inline constexpr int INDEX_SLOT = 0;

// Slot 0 is the section's index page; slot n is question n. Found by filename
// rather than by title: the titles are translated, the filenames are the same
// in every edition checked.
inline constexpr const char* BASENAMES[SLOT_COUNT] = {
    "1001061250.xhtml", "1001061251.xhtml", "1001061252.xhtml", "1001061253.xhtml", "1001061254.xhtml",
    "1001061255.xhtml", "1001061256.xhtml", "1001061257.xhtml", "1001061258.xhtml", "1001061259.xhtml",
    "1001061260.xhtml", "1001061261.xhtml", "1001061262.xhtml", "1001061263.xhtml", "1001061264.xhtml",
    "1001061265.xhtml", "1001061266.xhtml", "1001061267.xhtml", "1001061268.xhtml", "1001061269.xhtml",
    "1001061270.xhtml",
};

// The question to offer on a return visit, by question number; index 0 is unused.
inline constexpr uint8_t NEXT_QUESTION[SLOT_COUNT] = {0,  2,  3,  4,  5,  6,  7,  10, 9,  10, 11,
                                                      12, 18, 14, 15, 16, 18, 18, 20, 20, 0};

// 0 when there is no follow-up or `question` is outside 1..QUESTION_COUNT.
constexpr int nextQuestion(const int question) {
  if (question < 1 || question > QUESTION_COUNT) return 0;
  return NEXT_QUESTION[question];
}

// "Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?" -> the question alone.
// A label that does not have that shape, or would strip to nothing, comes back
// whole so a row is never blank. The result views into `label`.
std::string_view stripTocPrefix(std::string_view label);

}  // namespace preaching::IntroSection
