#include "BibleBookNameTable.h"

#include <Epub/BibleNavScanner.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstring>
#include <vector>

#include "SpineHtmlStream.h"

namespace {

bool feedNavScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<BibleNav::Scanner*>(ctx)->feed(chunk, length, isFinal);
}

}  // namespace

void copyUtf8Truncated(char* dest, const size_t destBytes, const std::string_view source) {
  const size_t fit = source.size() < destBytes - 1 ? source.size() : destBytes - 1;
  const int safe = utf8SafeTruncateBuffer(source.data(), static_cast<int>(fit));
  memcpy(dest, source.data(), static_cast<size_t>(safe));
  dest[safe] = '\0';
}

bool BibleBookNameTable::load(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer) {
  bookCount = 0;
  if (!epub) return false;
  const int bookNavSpine = epub->getBibleBookNavSpineIndex();
  if (bookNavSpine < 0) return false;

  BibleNav::Scanner scanner;
  if (!scanner.valid()) {
    LOG_ERR("BNAME", "OOM: nav scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub, bookNavSpine, renderer, feedNavScanner, &scanner)) return false;

  std::vector<std::string> targets = scanner.take();
  if (targets.empty()) return false;
  if (targets.size() > MAX_BOOKS) targets.resize(MAX_BOOKS);
  joinToc(*epub, targets.data(), static_cast<int>(targets.size()));
  return true;
}

void BibleBookNameTable::joinToc(const Epub& epub, const std::string* targets, const int targetCount) {
  bookCount = targetCount < MAX_BOOKS ? targetCount : MAX_BOOKS;
  for (int i = 0; i < bookCount; i++) names[i][0] = '\0';

  const int tocCount = epub.getTocItemsCount();
  for (int i = 0; i < tocCount; i++) {
    const auto tocItem = epub.getTocItem(i);
    const int match = BibleNav::findTargetByHref(targets, bookCount, tocItem.href);
    if (match >= 0 && names[match][0] == '\0') copyUtf8Truncated(names[match], NAME_BYTES, tocItem.title);
  }
}

const char* BibleBookNameTable::at(const int index) const {
  if (index < 0 || index >= bookCount) return "";
  return names[index];
}
