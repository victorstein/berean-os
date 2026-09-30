#include "BibleBookNameTable.h"

#include <Epub/BibleNavScanner.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "SpineHtmlStream.h"
#include "SpineSearch.h"

namespace {

bool feedNavScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<BibleNav::Scanner*>(ctx)->feed(chunk, length, isFinal);
}

}  // namespace

bool BibleBookNameTable::load(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer,
                              const SpineHtmlStream::WhenMissing whenMissing) {
  bookCount = 0;
  if (!epub) return false;
  const int bookNavSpine = epub->getBibleBookNavSpineIndex();
  if (bookNavSpine < 0) return false;

  BibleNav::Scanner scanner(/*collectText=*/true);
  if (!scanner.valid()) {
    LOG_ERR("BNAME", "OOM: nav scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub, bookNavSpine, renderer, feedNavScanner, &scanner, whenMissing)) return false;

  BibleNav::BookNavPage page = scanner.takeBookNav();
  std::vector<std::string>& targets = page.targets;
  if (targets.empty()) return false;
  if (targets.size() > MAX_BOOKS) targets.resize(MAX_BOOKS);
  joinToc(*epub, targets.data(), static_cast<int>(targets.size()));

  setAbbreviations(page.labels);
  return true;
}

void BibleBookNameTable::joinToc(const Epub& epub, const std::string* targets, const int targetCount, int* tocSpines) {
  bookCount = targetCount < MAX_BOOKS ? targetCount : MAX_BOOKS;
  for (int i = 0; i < bookCount; i++) {
    names[i][0] = '\0';
    abbreviations[i][0] = '\0';
  }

  const int tocCount = epub.getTocItemsCount();
  const int spineCount = epub.getSpineItemsCount();
  for (int i = 0; i < tocCount; i++) {
    const auto tocItem = epub.getTocItem(i);
    const int match = BibleNav::findTargetByHref(targets, bookCount, tocItem.href);
    if (match >= 0 && names[match][0] == '\0') copyUtf8Truncated(names[match], NAME_BYTES, tocItem.title);
    if (tocSpines)
      SpineSearch::takeTocSpine(targets, tocSpines, bookCount, tocItem.href, tocItem.spineIndex, spineCount);
  }
}

void BibleBookNameTable::setAbbreviations(const std::vector<std::string>& labels) {
  copyLabelRows(&abbreviations[0][0], ABBREV_BYTES, bookCount, labels.data(), labels.size());
}

const char* BibleBookNameTable::at(const int index) const {
  if (index < 0 || index >= bookCount) return "";
  return names[index];
}

const char* BibleBookNameTable::abbreviationFor(const uint8_t book) const {
  const int index = static_cast<int>(book) - 1;
  return index >= 0 && index < bookCount ? abbreviations[index] : "";
}

BookNameSource BibleBookNameTable::nameSource() const {
  BookNameSource source;
  source.names = &names[0][0];
  source.nameStride = NAME_BYTES;
  source.abbreviations = &abbreviations[0][0];
  source.abbreviationStride = ABBREV_BYTES;
  source.count = bookCount;
  return source;
}
