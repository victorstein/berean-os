#include "UnitIndexCache.h"

#include <Epub/BibleNavScanner.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <vector>

#include "StudyStore/UnitText.h"
#include "activities/reader/SpineHtmlStream.h"

namespace {

constexpr const char* MODULE = "UNITIDX";
constexpr const char* UNITS_DIR = "/.berean/units";

struct ScanContext {
  study::UnitScanner scanner;
};

bool feedScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<ScanContext*>(ctx)->scanner.feed(chunk, length, isFinal);
}

struct TextContext {
  study::UnitTextScanner scanner;
};

bool feedText(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<TextContext*>(ctx)->scanner.feed(chunk, length, isFinal);
}

struct NavContext {
  BibleNav::Scanner scanner;
};

bool feedNav(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<NavContext*>(ctx)->scanner.feed(chunk, length, isFinal);
}

}  // namespace

UnitIndexCache::UnitIndexCache(std::shared_ptr<Epub> epub, std::string pubKey, GfxRenderer& renderer)
    : epub_(std::move(epub)), pubKey_(std::move(pubKey)), renderer_(renderer) {}

std::string UnitIndexCache::indexPath() const { return std::string(UNITS_DIR) + "/" + pubKey_ + ".bin"; }

bool UnitIndexCache::begin() {
  ready_ = false;
  if (!epub_) return false;

  const int spineCount = epub_->getSpineItemsCount();
  if (spineCount <= 0 || spineCount > UINT16_MAX) return false;

  size_t sourceSize = 0;
  {
    HalFile source;
    if (Storage.openFileForRead(MODULE, epub_->getPath(), source)) sourceSize = source.size();
  }

  const uint32_t documentCount = static_cast<uint32_t>(spineCount);
  study::UnitIndexHeader wanted;
  wanted.documentCount = static_cast<uint16_t>(documentCount);
  wanted.sourceSize = static_cast<uint32_t>(sourceSize);
  wanted.tableOffset = study::UNIT_INDEX_HEADER_BYTES;
  wanted.bookMapOffset = wanted.tableOffset + documentCount * study::UNIT_INDEX_ENTRY_BYTES;
  wanted.anchorsOffset = wanted.bookMapOffset + documentCount;

  const std::string path = indexPath();
  bool rebuild = true;

  HalFile existing;
  if (Storage.openFileForRead(MODULE, path, existing)) {
    uint8_t head[study::UNIT_INDEX_HEADER_BYTES];
    if (existing.read(head, sizeof(head)) == static_cast<int>(sizeof(head))) {
      const auto parsed = study::readHeader(head, sizeof(head));
      if (parsed && parsed->documentCount == wanted.documentCount &&
          !study::headerIsStale(*parsed, wanted.sourceSize)) {
        header_ = *parsed;
        bookMapBuilt_ = header_.bookMapOffset != 0 && existing.size() >= header_.anchorsOffset;
        rebuild = false;
      }
    }
  }

  if (rebuild) {
    // The index is a pure cache, so a stale or corrupt one is discarded whole
    // rather than repaired. Losing it costs a rescan of the documents actually
    // visited, not of the publication.
    Storage.mkdir(UNITS_DIR);
    HalFile fresh;
    if (!Storage.openFileForWrite(MODULE, path, fresh)) {
      LOG_ERR(MODULE, "Cannot create %s", path.c_str());
      return false;
    }
    uint8_t head[study::UNIT_INDEX_HEADER_BYTES];
    study::writeHeader(head, wanted);
    if (fresh.write(head, sizeof(head)) != sizeof(head)) return false;

    // Fixed-size table and book map, zeroed: every document reads as
    // "not yet indexed" and every book as 0 until built.
    const size_t blankBytes = documentCount * study::UNIT_INDEX_ENTRY_BYTES + documentCount;
    auto blank = makeUniqueNoThrow<uint8_t[]>(1024);
    if (!blank) {
      LOG_ERR(MODULE, "OOM: index scratch");
      return false;
    }
    memset(blank.get(), 0, 1024);
    for (size_t written = 0; written < blankBytes;) {
      const size_t chunk = blankBytes - written < 1024 ? blankBytes - written : 1024;
      if (fresh.write(blank.get(), chunk) != chunk) return false;
      written += chunk;
    }
    header_ = wanted;
    bookMapBuilt_ = false;
  }

  ready_ = true;
  return true;
}

bool UnitIndexCache::readEntry(const uint16_t spineIndex, study::UnitIndexEntry& out) const {
  if (spineIndex >= header_.documentCount) return false;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, indexPath(), file)) return false;
  if (!file.seek(header_.tableOffset + static_cast<size_t>(spineIndex) * study::UNIT_INDEX_ENTRY_BYTES)) return false;

  uint8_t buf[study::UNIT_INDEX_ENTRY_BYTES];
  if (file.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) return false;
  out = study::readEntry(buf);
  return true;
}

bool UnitIndexCache::writeEntry(const uint16_t spineIndex, const study::UnitIndexEntry& entry) const {
  if (spineIndex >= header_.documentCount) return false;
  HalFile file = Storage.open(indexPath().c_str(), O_RDWR);
  if (!file) return false;
  if (!file.seek(header_.tableOffset + static_cast<size_t>(spineIndex) * study::UNIT_INDEX_ENTRY_BYTES)) return false;

  uint8_t buf[study::UNIT_INDEX_ENTRY_BYTES];
  study::writeEntry(buf, entry);
  return file.write(buf, sizeof(buf)) == sizeof(buf);
}

bool UnitIndexCache::loadAnchors(const study::UnitIndexEntry& entry, study::DocumentUnits& out) const {
  out.anchors.clear();
  out.kind = entry.kind;
  out.book = entry.book;
  if (entry.anchorCount == 0) return true;

  const size_t bytes = static_cast<size_t>(entry.anchorCount) * study::UNIT_INDEX_ANCHOR_BYTES;
  auto buf = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!buf) {
    LOG_ERR(MODULE, "OOM: %u anchors", entry.anchorCount);
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead(MODULE, indexPath(), file)) return false;
  if (!file.seek(entry.dataOffset)) return false;
  if (file.read(buf.get(), bytes) != static_cast<int>(bytes)) return false;

  // A torn write leaves an entry whose CRC does not match its anchors. Treating
  // that as "not indexed" rebuilds the document rather than painting marks at
  // offsets that are now garbage.
  if (study::anchorChecksum(buf.get(), bytes) != entry.anchorCrc) {
    LOG_ERR(MODULE, "Anchor CRC mismatch; rebuilding document");
    return false;
  }

  out.anchors = study::readAnchors(buf.get(), entry.anchorCount);
  return true;
}

bool UnitIndexCache::buildDocument(const uint16_t spineIndex) {
  ScanContext ctx;
  if (!ctx.scanner.valid()) {
    LOG_ERR(MODULE, "OOM: unit scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub_, spineIndex, renderer_, feedScanner, &ctx)) return false;

  study::DocumentUnits units = ctx.scanner.take();
  units.book = units.kind == study::UnitKind::Verse ? bookFor(spineIndex) : 0;

  study::UnitIndexEntry entry;
  entry.kind = units.kind;
  entry.book = units.book;
  entry.anchorCount = static_cast<uint16_t>(units.anchors.size());

  if (!units.anchors.empty()) {
    const size_t bytes = units.anchors.size() * study::UNIT_INDEX_ANCHOR_BYTES;
    auto buf = makeUniqueNoThrow<uint8_t[]>(bytes);
    if (!buf) {
      LOG_ERR(MODULE, "OOM: %u anchors", entry.anchorCount);
      return false;
    }
    study::writeAnchors(buf.get(), units.anchors);
    entry.anchorCrc = study::anchorChecksum(buf.get(), bytes);

    HalFile file = Storage.open(indexPath().c_str(), O_RDWR | O_APPEND);
    if (!file) return false;
    const size_t at = file.size();
    if (file.write(buf.get(), bytes) != bytes) return false;
    entry.dataOffset = static_cast<uint32_t>(at);
  } else {
    // A document with no units is still INDEXED -- recording that fact is what
    // stops it being rescanned on every visit. dataOffset must be non-zero for
    // indexed() to hold, so it points at the (empty) end of the anchor region.
    entry.dataOffset = header_.anchorsOffset;
  }

  // The entry is written last. Until it lands the appended anchors are
  // unreferenced bytes, which the next attempt simply appends past.
  if (!writeEntry(spineIndex, entry)) return false;

  cachedSpine_ = spineIndex;
  cached_ = std::move(units);
  cachedBuildFailed_ = false;
  return true;
}

const study::DocumentUnits& UnitIndexCache::unitsFor(const uint16_t spineIndex) {
  static const study::DocumentUnits empty;
  if (cachedSpine_ == spineIndex) return cached_;
  if (!ready_) return empty;

  study::UnitIndexEntry entry;
  if (readEntry(spineIndex, entry) && entry.indexed()) {
    study::DocumentUnits loaded;
    if (loadAnchors(entry, loaded)) {
      cachedSpine_ = spineIndex;
      cached_ = std::move(loaded);
      cachedBuildFailed_ = false;
      return cached_;
    }
  }

  if (!buildDocument(spineIndex)) {
    LOG_ERR(MODULE, "Index build failed for spine %u; addressing degraded", spineIndex);
    cachedSpine_ = spineIndex;
    cached_ = study::DocumentUnits{};
    cachedBuildFailed_ = true;
  }
  return cached_;
}

bool UnitIndexCache::buildBookMap() {
  if (!epub_) return false;
  const int navSpine = epub_->getBibleBookNavSpineIndex();
  if (navSpine < 0) return false;

  NavContext nav;
  if (!nav.scanner.valid()) return false;
  if (!SpineHtmlStream::stream(epub_, navSpine, renderer_, feedNav, &nav)) return false;

  std::vector<std::string> books = nav.scanner.take();
  if (books.empty()) return false;
  if (books.size() > MAX_BIBLE_BOOKS) books.resize(MAX_BIBLE_BOOKS);

  auto map = makeUniqueNoThrow<uint8_t[]>(header_.documentCount);
  if (!map) {
    LOG_ERR(MODULE, "OOM: book map");
    return false;
  }
  memset(map.get(), 0, header_.documentCount);

  // Every filename resolved here goes through ONE forward sweep of the spine.
  // A sweep is getSpineEntry per item -- two SD seeks, a read and a heap string
  // each (BookMetadataCache.cpp:520-524) -- so on the NWT that is 3,937 of them.
  //
  // Resolving one filename at a time cost a sweep EACH: 66 books plus a second
  // sweep per book's chapter list is ~127 sweeps, which is minutes of SD I/O
  // behind a blank screen. Batching keeps it to a handful, because a sweep
  // matches every pending filename as it goes and stops early once all are
  // found.
  std::vector<std::string> pendingNames;
  std::vector<uint8_t> pendingBooks;
  pendingNames.reserve(RESOLVE_BATCH);
  pendingBooks.reserve(RESOLVE_BATCH);

  const auto flush = [&]() {
    if (pendingNames.empty()) return;
    auto spines = makeUniqueNoThrow<int[]>(pendingNames.size());
    if (spines) {
      epub_->resolveFilenamesToSpineIndices(pendingNames.data(), spines.get(), static_cast<int>(pendingNames.size()));
      for (size_t i = 0; i < pendingNames.size(); ++i) {
        if (spines[i] >= 0 && spines[i] < header_.documentCount) map[spines[i]] = pendingBooks[i];
      }
    }
    pendingNames.clear();
    pendingBooks.clear();
    progress_.tick();
    vTaskDelay(1);
  };

  // Pass 1: the book-nav page's own targets, all in one batch. For a
  // single-chapter book that target IS the chapter; for the rest it is the
  // chapter-nav page, whose spine index pass 2 needs.
  std::vector<int> bookTargetSpine(books.size(), -1);
  {
    auto spines = makeUniqueNoThrow<int[]>(books.size());
    if (!spines) {
      LOG_ERR(MODULE, "OOM: book targets");
      return false;
    }
    epub_->resolveFilenamesToSpineIndices(books.data(), spines.get(), static_cast<int>(books.size()));
    for (size_t i = 0; i < books.size(); ++i) {
      bookTargetSpine[i] = spines[i];
      // The five single-chapter books (Obadiah, Philemon, 2 John, 3 John, Jude)
      // have no nav page of their own and point straight at their chapter.
      if (!BibleNav::isChapterNav(books[i]) && spines[i] >= 0 && spines[i] < header_.documentCount) {
        map[spines[i]] = static_cast<uint8_t>(i + 1);
      }
    }
    progress_.tick();
    vTaskDelay(1);
  }

  // Pass 2: every chapter link from every nav page, accumulated across books so
  // one sweep resolves many.
  for (size_t i = 0; i < books.size(); ++i) {
    const uint8_t bookNumber = static_cast<uint8_t>(i + 1);
    if (!BibleNav::isChapterNav(books[i]) || bookTargetSpine[i] < 0) continue;

    NavContext chapters;
    if (!chapters.scanner.valid()) continue;
    if (!SpineHtmlStream::stream(epub_, static_cast<int>(bookTargetSpine[i]), renderer_, feedNav, &chapters)) continue;

    std::vector<std::string> links = chapters.scanner.take();
    BibleNav::dropBookNavLinks(links);
    for (auto& link : links) {
      pendingNames.push_back(std::move(link));
      pendingBooks.push_back(bookNumber);
      if (pendingNames.size() >= RESOLVE_BATCH) flush();
    }

    // Yielded every book, not only on the paths that reach the end of the loop
    // body: the `continue`s above used to skip it entirely.
    vTaskDelay(1);
  }
  flush();

  HalFile file = Storage.open(indexPath().c_str(), O_RDWR);
  if (!file) return false;
  if (!file.seek(header_.bookMapOffset)) return false;
  if (file.write(map.get(), header_.documentCount) != header_.documentCount) return false;

  bookMapBuilt_ = true;
  return true;
}

uint8_t UnitIndexCache::bookFor(const uint16_t spineIndex) {
  if (!ready_ || spineIndex >= header_.documentCount) return 0;
  if (!bookMapBuilt_ && !buildBookMap()) return 0;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, indexPath(), file)) return 0;
  if (!file.seek(header_.bookMapOffset + spineIndex)) return 0;
  const int value = file.read();
  return value < 0 ? 0 : static_cast<uint8_t>(value);
}

std::vector<uint16_t> UnitIndexCache::spineIndicesForBook(const uint8_t book) {
  std::vector<uint16_t> out;
  if (!ready_ || book == 0) return out;
  if (!bookMapBuilt_ && !buildBookMap()) return out;

  auto map = makeUniqueNoThrow<uint8_t[]>(header_.documentCount);
  if (!map) {
    LOG_ERR(MODULE, "OOM: book map");
    return out;
  }

  HalFile file;
  if (!Storage.openFileForRead(MODULE, indexPath(), file)) return out;
  if (!file.seek(header_.bookMapOffset)) return out;
  if (file.read(map.get(), header_.documentCount) != header_.documentCount) return out;

  for (uint16_t i = 0; i < header_.documentCount; ++i) {
    if (map[i] == book) out.push_back(i);
  }
  return out;
}

std::string UnitIndexCache::unitText(const uint16_t spineIndex, const study::Unit& unit) {
  const study::DocumentUnits& units = unitsFor(spineIndex);
  const size_t index = study::anchorIndexOf(units, unit);
  if (index == SIZE_MAX) return {};

  TextContext ctx;
  ctx.scanner.setRange(units.anchors[index].offset, study::unitEndOffset(units, index));
  if (!ctx.scanner.valid()) return {};
  if (!SpineHtmlStream::stream(epub_, spineIndex, renderer_, feedText, &ctx)) return {};
  return ctx.scanner.take();
}
