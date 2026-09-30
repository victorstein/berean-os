#include "BibleReference.h"

#include <Utf8.h>

#include <cstdio>

namespace BibleReference {
namespace {

size_t formatNumbers(char (&numbers)[MAX_NUMBERS_BYTES], const Verses& verses) {
  int written = 0;
  if (verses.chapter == 0) {
    numbers[0] = '\0';
  } else if (verses.verse == 0) {
    written = snprintf(numbers, sizeof(numbers), "%u", static_cast<unsigned>(verses.chapter));
  } else if (verses.verseEnd == 0) {
    written = snprintf(numbers, sizeof(numbers), "%u:%u", static_cast<unsigned>(verses.chapter),
                       static_cast<unsigned>(verses.verse));
  } else {
    written = snprintf(numbers, sizeof(numbers), "%u:%u-%u", static_cast<unsigned>(verses.chapter),
                       static_cast<unsigned>(verses.verse), static_cast<unsigned>(verses.verseEnd));
  }
  return written > 0 ? static_cast<size_t>(written) : 0;
}

bool needsSeparator(const std::string_view book, const size_t numbersLength) {
  return !book.empty() && numbersLength > 0;
}

// utf8SafeTruncateBuffer keeps an incomplete sequence that starts at index 0 (Utf8.cpp:161).
bool isIncompleteLeadSequence(const char* text, const size_t length) {
  const auto lead = static_cast<unsigned char>(text[0]);
  size_t expected = 1;
  if ((lead >> 5) == 0x6) {
    expected = 2;
  } else if ((lead >> 4) == 0xE) {
    expected = 3;
  } else if ((lead >> 3) == 0x1E) {
    expected = 4;
  }
  return length < expected;
}

}  // namespace

void format(char* out, const size_t outBytes, const std::string_view book, const Verses& verses) {
  if (outBytes == 0) return;
  char numbers[MAX_NUMBERS_BYTES];
  const size_t numbersLength = formatNumbers(numbers, verses);
  const int written = snprintf(out, outBytes, "%.*s%s%s", static_cast<int>(book.size()), book.data(),
                               needsSeparator(book, numbersLength) ? " " : "", numbers);
  if (written < 0) {
    out[0] = '\0';
    return;
  }
  if (static_cast<size_t>(written) < outBytes) return;

  size_t kept = static_cast<size_t>(utf8SafeTruncateBuffer(out, static_cast<int>(outBytes - 1)));
  if (kept > 0 && isIncompleteLeadSequence(out, kept)) kept = 0;
  out[kept] = '\0';
}

std::string format(const std::string_view book, const Verses& verses) {
  char numbers[MAX_NUMBERS_BYTES];
  const size_t numbersLength = formatNumbers(numbers, verses);
  std::string reference;
  reference.reserve(book.size() + 1 + numbersLength);
  reference.append(book);
  if (needsSeparator(book, numbersLength)) reference += ' ';
  reference.append(numbers, numbersLength);
  return reference;
}

}  // namespace BibleReference
