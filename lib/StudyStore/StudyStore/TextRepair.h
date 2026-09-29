#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "StudyStore/PassageSpan.h"
#include "StudyStore/TaggedPassage.h"

// Rebuilds a stored passage's text as its whole verse(s) (issue #188). Pure: the
// caller locates the document and streams its text, so test/unit_text runs this
// on the host. Modelled on planMigration's injected unitText.
namespace study {

// A passage's text as stored: every ASCII whitespace run one space, trimmed.
// Uncapped -- nothing here may cut a passage.
std::string normaliseWholeText(std::string text);

// Whether an old snippet plausibly starts inside `wholeText`. Both sides fold
// no-break and narrow spaces to U+0020, drop '*', and drop tokens made only of
// digits: old snippets carry the verse numbers and footnote markers the reader
// laid out as words, which the whole text leaves out.
bool snippetOccursIn(std::string_view snippet, std::string_view wholeText);

enum class TextRepairOutcome : uint8_t {
  AlreadyWhole,
  Rebuilt,
  RebuiltSuspectStart,  // the known limit: its stored range may have started at the wrong word
  FingerprintMismatch,  // another edition's text is at this address; never overwrite with it
  Unresolvable,
};

struct TextRepairInputs {
  const DocumentUnits* units = nullptr;  // the document the passage was located in; nullptr when not located
  // A context pointer and plain function pointers, not std::function: CLAUDE.md
  // prohibits std::function in library code.
  void* ctx = nullptr;
  std::string (*spanText)(void* ctx, const PassageSpan& span) = nullptr;  // filtered, for storing
  std::string (*unitText)(void* ctx, const Unit& unit) = nullptr;         // unfiltered, for the fingerprint
};

struct TextRepairPlan {
  TextRepairOutcome outcome = TextRepairOutcome::Unresolvable;
  std::string wholeText;  // normalised; set only for Rebuilt and RebuiltSuspectStart
};

TextRepairPlan planTextRepair(const TaggedPassage& passage, const TextRepairInputs& in);

// The order one repair pass visits rows in. A row attempted earlier in the
// session goes behind every row not yet attempted, and each pass starts after
// the last row attempted, so one slow or unresolvable row cannot starve the rows
// stored after it.
class RepairSchedule {
 public:
  std::vector<size_t> order(const std::vector<bool>& needsRepair) const;
  void markAttempted(size_t index);

 private:
  std::vector<bool> attempted_;
  size_t cursor_ = 0;
};

}  // namespace study
