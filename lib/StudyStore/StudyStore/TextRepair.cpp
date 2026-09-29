#include "StudyStore/TextRepair.h"

#include <Utf8.h>

#include <cctype>
#include <cstdint>
#include <utility>

#include "StudyStore/UnitFingerprint.h"

namespace study {
namespace {

constexpr std::string_view FOLDED_SPACES[] = {"\xC2\xA0",     "\xE2\x80\xAF", "\xE2\x80\x82", "\xE2\x80\x83",
                                              "\xE2\x80\x87", "\xE2\x80\x89", "\xE2\x80\x8A"};
constexpr size_t SNIPPET_PREFIX_BYTES = 32;

std::string foldForComparison(const std::string_view text) {
  std::string spaced;
  spaced.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    bool folded = false;
    for (const std::string_view space : FOLDED_SPACES) {
      if (text.substr(i, space.size()) == space) {
        spaced.push_back(' ');
        i += space.size();
        folded = true;
        break;
      }
    }
    if (folded) continue;
    const char c = text[i++];
    if (c == '*') continue;
    spaced.push_back(std::isspace(static_cast<unsigned char>(c)) ? ' ' : c);
  }

  std::string out;
  out.reserve(spaced.size());
  size_t start = 0;
  while (start < spaced.size()) {
    size_t end = spaced.find(' ', start);
    if (end == std::string::npos) end = spaced.size();
    const std::string_view token(spaced.data() + start, end - start);
    const bool digitsOnly = !token.empty() && token.find_first_not_of("0123456789") == std::string_view::npos;
    if (!token.empty() && !digitsOnly) {
      if (!out.empty()) out.push_back(' ');
      out.append(token);
    }
    start = end + 1;
  }
  return out;
}

}  // namespace

std::string normaliseWholeText(std::string text) {
  // utf8SafeSummary keeps the first of two adjacent whitespace characters and
  // then deletes every '\n', so "a\n b" would come out "ab". Spaces first.
  for (char& c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) c = ' ';
  }
  return utf8SafeSummary(std::move(text), SIZE_MAX);
}

bool snippetOccursIn(const std::string_view snippet, const std::string_view wholeText) {
  std::string prefix = foldForComparison(snippet);
  if (prefix.empty()) return true;
  if (prefix.size() > SNIPPET_PREFIX_BYTES) {
    prefix.resize(static_cast<size_t>(utf8SafeTruncateBuffer(prefix.data(), static_cast<int>(SNIPPET_PREFIX_BYTES))));
  }
  return foldForComparison(wholeText).find(prefix) != std::string::npos;
}

TextRepairPlan planTextRepair(const TaggedPassage& passage, const TextRepairInputs& in) {
  TextRepairPlan plan;
  if (passage.whole) {
    plan.outcome = TextRepairOutcome::AlreadyWhole;
    return plan;
  }
  if (in.units == nullptr || in.spanText == nullptr) return plan;

  // The paint path's rule (StudyStore::passagesInDocument): a fingerprint that no
  // longer matches means the text at this address is not the text that was
  // tagged -- another language's Bible under the same language-free pubkey, or a
  // re-downloaded edition.
  if (passage.fingerprint.length != 0 && in.unitText != nullptr) {
    const Fingerprint current = fingerprintOf(in.unitText(in.ctx, passage.start));
    if (current.length != 0 && !(current == passage.fingerprint)) {
      plan.outcome = TextRepairOutcome::FingerprintMismatch;
      return plan;
    }
  }

  const auto span = snapSpan(*in.units, passage.start, passage.end);
  if (!span) return plan;
  std::string text = normaliseWholeText(in.spanText(in.ctx, *span));
  if (text.empty()) return plan;

  plan.outcome =
      snippetOccursIn(passage.snippet, text) ? TextRepairOutcome::Rebuilt : TextRepairOutcome::RebuiltSuspectStart;
  plan.wholeText = std::move(text);
  return plan;
}

std::vector<size_t> RepairSchedule::order(const std::vector<bool>& needsRepair) const {
  const size_t count = needsRepair.size();
  std::vector<size_t> fresh;
  std::vector<size_t> retried;
  fresh.reserve(count);
  retried.reserve(count);
  for (size_t step = 0; step < count; ++step) {
    const size_t index = (cursor_ + step) % count;
    if (!needsRepair[index]) continue;
    const bool attempted = index < attempted_.size() && attempted_[index];
    (attempted ? retried : fresh).push_back(index);
  }
  fresh.insert(fresh.end(), retried.begin(), retried.end());
  return fresh;
}

void RepairSchedule::markAttempted(const size_t index) {
  if (index >= attempted_.size()) attempted_.resize(index + 1, false);
  attempted_[index] = true;
  cursor_ = index + 1;
}

}  // namespace study
