#include "IndexBuilder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Fold.h"

namespace BibleSearch {
namespace {

constexpr uint32_t NONE = RecordPool::NONE;

// Postings are kept in the file's own encoding while building, so the build
// holds ~1.3 bytes per (term, verse) pair rather than a u16, and writing is a
// copy. Most terms are rare; a small block wastes little on each of them.
constexpr uint32_t BLOCK_DATA_BYTES = 12;
struct Block {
  uint32_t next;
  uint8_t data[BLOCK_DATA_BYTES];
};
static_assert(sizeof(Block) == 16);

// Chunk sizes keep every chunk well past 4 KB, so the build is a few hundred
// large blocks: ~24,000 distinct terms, ~850,000 postings, ~31,000 verses.
constexpr uint32_t VERSES_PER_CHUNK = 4096;
constexpr uint32_t TERMS_PER_CHUNK = 2048;
constexpr uint32_t STRING_BYTES_PER_CHUNK = 64 * 1024;
constexpr uint32_t BLOCKS_PER_CHUNK = 4096;

constexpr uint32_t INITIAL_SLOTS = 32768;  // power of two; grows at 70% load

uint32_t fnv1a(const std::string_view s) {
  uint32_t hash = 2166136261u;
  for (const char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

// Batches the many small writes of a serialisation into few sink calls,
// through a buffer the caller provides.
class SinkWriter {
 public:
  SinkWriter(const ByteSink sink, void* ctx, uint8_t* buffer, const size_t capacity)
      : sink_(sink), ctx_(ctx), buffer_(buffer), capacity_(capacity) {}

  void put(const void* data, size_t length) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (length > 0 && ok_) {
      const size_t n = std::min(length, capacity_ - used_);
      memcpy(buffer_ + used_, p, n);
      used_ += n;
      p += n;
      length -= n;
      if (used_ == capacity_) flush();
    }
  }

  bool flush() {
    if (ok_ && used_ > 0) ok_ = sink_(ctx_, buffer_, used_);
    used_ = 0;
    return ok_;
  }

 private:
  ByteSink sink_;
  void* ctx_;
  uint8_t* buffer_;
  size_t capacity_;
  size_t used_ = 0;
  bool ok_ = true;
};

}  // namespace

struct IndexBuilder::Term {
  uint32_t hash;
  uint32_t stringIndex;
  uint32_t headBlock;
  uint32_t tailBlock;
  uint32_t postingBytes;
  uint16_t count;
  uint16_t lastVerse;
  uint8_t length;
  uint8_t tailUsed;
};

RecordPool::RecordPool(const BuildAllocator allocator, const uint32_t recordBytes, const uint32_t recordsPerChunk)
    : allocator_(allocator), recordBytes_(recordBytes), recordsPerChunk_(recordsPerChunk) {
  chunks_.reserve(32);
}

RecordPool::~RecordPool() {
  for (uint8_t* chunk : chunks_) allocator_.release(chunk);
}

uint32_t RecordPool::append(const uint32_t count) {
  if (count == 0 || count > recordsPerChunk_) return NONE;
  const uint32_t capacity = static_cast<uint32_t>(chunks_.size()) * recordsPerChunk_;
  uint32_t first = size_;
  if (capacity - size_ < count) {
    // Records never straddle chunks, so the tail of the last chunk is skipped.
    auto* chunk = static_cast<uint8_t*>(allocator_.allocate(static_cast<size_t>(recordBytes_) * recordsPerChunk_));
    if (!chunk) return NONE;
    chunks_.push_back(chunk);
    first = capacity;
  }
  size_ = first + count;
  return first;
}

uint8_t* RecordPool::at(const uint32_t index) const {
  return chunks_[index / recordsPerChunk_] + static_cast<size_t>(index % recordsPerChunk_) * recordBytes_;
}

IndexBuilder::IndexBuilder(const BuildAllocator allocator)
    : allocator_(allocator),
      verses_(allocator, sizeof(VerseEntry), VERSES_PER_CHUNK),
      terms_(allocator, sizeof(Term), TERMS_PER_CHUNK),
      strings_(allocator, 1, STRING_BYTES_PER_CHUNK),
      blocks_(allocator, sizeof(Block), BLOCKS_PER_CHUNK) {}

IndexBuilder::~IndexBuilder() {
  if (slots_) allocator_.release(slots_);
}

IndexBuilder::Term& IndexBuilder::termAt(const uint32_t id) const { return *reinterpret_cast<Term*>(terms_.at(id)); }

std::string_view IndexBuilder::termString(const Term& term) const {
  return {reinterpret_cast<const char*>(strings_.at(term.stringIndex)), term.length};
}

uint32_t IndexBuilder::addVerse(const uint8_t book, const uint8_t chapter, const uint8_t verse, const uint16_t spine,
                                const uint32_t offset) {
  if (failed_) return INVALID_VERSE;
  if (verses_.size() >= MAX_VERSES) {
    failed_ = true;
    return INVALID_VERSE;
  }
  const uint32_t index = verses_.append();
  if (index == NONE) {
    failed_ = true;
    return INVALID_VERSE;
  }
  VerseEntry entry;
  entry.book = book;
  entry.chapter = chapter;
  entry.verse = verse;
  entry.spine = spine;
  entry.offset = offset;
  memcpy(verses_.at(index), &entry, sizeof(entry));
  return index;
}

bool IndexBuilder::verseAt(const uint32_t n, VerseEntry& out) const {
  if (n >= verses_.size()) return false;
  memcpy(&out, verses_.at(n), sizeof(out));
  return true;
}

bool IndexBuilder::addVerseText(const uint32_t verseNumber, const std::string_view rawText) {
  if (failed_) return false;
  // Only the newest verse may gain text: postings are appended in ascending
  // order, and a term can then de-duplicate against its last verse alone.
  if (verses_.size() == 0 || verseNumber != verses_.size() - 1) {
    failed_ = true;
    return false;
  }
  foldBuffer_.clear();
  foldAppend(foldBuffer_, rawText);

  struct Context {
    IndexBuilder* self;
    uint16_t verse;
  } context{this, static_cast<uint16_t>(verseNumber)};
  tokenize(
      foldBuffer_,
      [](void* ctx, const std::string_view token) {
        auto* c = static_cast<Context*>(ctx);
        if (!c->self->failed_ && !c->self->recordTerm(token, c->verse)) c->self->failed_ = true;
      },
      &context);
  return !failed_;
}

bool IndexBuilder::appendToLastVerse(const std::string_view rawText) {
  if (failed_) return false;
  // Nothing precedes the first verse of the Bible; there is no verse to extend.
  if (verses_.size() == 0) return true;
  return addVerseText(verses_.size() - 1, rawText);
}

bool IndexBuilder::recordTerm(const std::string_view token, const uint16_t verseNumber) {
  const uint32_t id = findOrInsert(token, fnv1a(token));
  if (id == NONE) return false;
  Term& term = termAt(id);
  if (term.count > 0 && term.lastVerse == verseNumber) return true;
  if (term.count > 0 && verseNumber < term.lastVerse) return false;
  const uint16_t delta = term.count > 0 ? static_cast<uint16_t>(verseNumber - term.lastVerse) : verseNumber;
  uint8_t encoded[MAX_VARINT_BYTES];
  const size_t length = writeVarint(encoded, delta);
  if (!appendPostingBytes(term, encoded, length)) return false;
  term.lastVerse = verseNumber;
  term.count++;
  return true;
}

bool IndexBuilder::appendPostingBytes(Term& term, const uint8_t* bytes, const size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (term.headBlock == NONE || term.tailUsed == BLOCK_DATA_BYTES) {
      const uint32_t index = blocks_.append();
      if (index == NONE) return false;
      reinterpret_cast<Block*>(blocks_.at(index))->next = NONE;
      if (term.headBlock == NONE) {
        term.headBlock = index;
      } else {
        reinterpret_cast<Block*>(blocks_.at(term.tailBlock))->next = index;
      }
      term.tailBlock = index;
      term.tailUsed = 0;
    }
    reinterpret_cast<Block*>(blocks_.at(term.tailBlock))->data[term.tailUsed++] = bytes[i];
    term.postingBytes++;
    postingBytes_++;
  }
  return true;
}

uint32_t IndexBuilder::findOrInsert(const std::string_view token, const uint32_t hash) {
  if ((static_cast<uint64_t>(terms_.size()) + 1) * 10 > static_cast<uint64_t>(slotCount_) * 7 && !growSlots()) {
    return NONE;
  }
  const uint32_t mask = slotCount_ - 1;
  for (uint32_t slot = hash & mask;; slot = (slot + 1) & mask) {
    const uint32_t occupant = slots_[slot];
    if (occupant == 0) break;
    const Term& existing = termAt(occupant - 1);
    if (existing.hash == hash && termString(existing) == token) return occupant - 1;
  }

  // The string first: a term record without its string would be rehashed and
  // compared later as garbage.
  const uint32_t stringIndex = strings_.append(static_cast<uint32_t>(token.size()));
  if (stringIndex == NONE) return NONE;
  const uint32_t id = terms_.append();
  if (id == NONE) return NONE;
  memcpy(strings_.at(stringIndex), token.data(), token.size());
  stringBytes_ += token.size() + 1;

  Term& term = termAt(id);
  term.hash = hash;
  term.stringIndex = stringIndex;
  term.headBlock = NONE;
  term.tailBlock = NONE;
  term.postingBytes = 0;
  term.count = 0;
  term.lastVerse = 0;
  term.length = static_cast<uint8_t>(token.size());
  term.tailUsed = 0;

  for (uint32_t slot = hash & mask;; slot = (slot + 1) & mask) {
    if (slots_[slot] == 0) {
      slots_[slot] = id + 1;
      break;
    }
  }
  return id;
}

bool IndexBuilder::growSlots() {
  const uint32_t count = slotCount_ == 0 ? INITIAL_SLOTS : slotCount_ * 2;
  auto* slots = static_cast<uint32_t*>(allocator_.allocate(static_cast<size_t>(count) * sizeof(uint32_t)));
  if (!slots) return false;
  memset(slots, 0, static_cast<size_t>(count) * sizeof(uint32_t));
  const uint32_t mask = count - 1;
  for (uint32_t id = 0; id < terms_.size(); id++) {
    uint32_t slot = termAt(id).hash & mask;
    while (slots[slot] != 0) slot = (slot + 1) & mask;
    slots[slot] = id + 1;
  }
  if (slots_) allocator_.release(slots_);
  slots_ = slots;
  slotCount_ = count;
  return true;
}

size_t IndexBuilder::estimatedBytes() const {
  return INDEX_HEADER_BYTES + static_cast<size_t>(verses_.size()) * VERSE_ENTRY_BYTES +
         static_cast<size_t>(terms_.size()) * TERM_ENTRY_BYTES + stringBytes_ + postingBytes_;
}

size_t IndexBuilder::allocatedBytes() const {
  return verses_.allocatedBytes() + terms_.allocatedBytes() + strings_.allocatedBytes() + blocks_.allocatedBytes() +
         static_cast<size_t>(slotCount_) * sizeof(uint32_t);
}

bool IndexBuilder::write(const ByteSink sink, void* ctx, const uint64_t fingerprint) const {
  return serialise(sink, ctx, fingerprint, INDEX_FLAG_COMPLETE, 0);
}

bool IndexBuilder::writeCheckpoint(const ByteSink sink, void* ctx, const uint64_t fingerprint,
                                   const uint32_t docsDone) const {
  return serialise(sink, ctx, fingerprint, 0, docsDone);
}

bool IndexBuilder::serialise(const ByteSink sink, void* ctx, const uint64_t fingerprint, const uint16_t flags,
                             const uint32_t docsDone) const {
  if (failed_) return false;
  const uint32_t termCount = terms_.size();
  AllocatedBuffer orderBuffer(allocator_, static_cast<size_t>(termCount) * sizeof(uint32_t));
  auto* order = orderBuffer.as<uint32_t>();
  if (termCount > 0 && !order) {
    failed_ = true;
    return false;
  }
  for (uint32_t i = 0; i < termCount; i++) order[i] = i;
  std::sort(order, order + termCount, [this](const uint32_t a, const uint32_t b) {
    const std::string_view left = termString(termAt(a));
    const std::string_view right = termString(termAt(b));
    const int byBytes = memcmp(left.data(), right.data(), std::min(left.size(), right.size()));
    return byBytes != 0 ? byBytes < 0 : left.size() < right.size();
  });

  IndexHeader header;
  header.flags = flags;
  header.fingerprint = fingerprint;
  header.verseCount = verses_.size();
  header.termCount = termCount;
  header.docsDone = docsDone;
  header.verseTableOffset = INDEX_HEADER_BYTES;
  header.termTableOffset = header.verseTableOffset + verses_.size() * VERSE_ENTRY_BYTES;
  header.termStringsOffset = header.termTableOffset + termCount * TERM_ENTRY_BYTES;
  header.postingsOffset = header.termStringsOffset + static_cast<uint32_t>(stringBytes_);
  header.fileSize = header.postingsOffset + static_cast<uint32_t>(postingBytes_);

  // A 64-byte stack buffer keeps the write correct, only slower, when the
  // allocator cannot spare SINK_BUFFER_BYTES.
  uint8_t fallback[64];
  AllocatedBuffer sinkBuffer(allocator_, SINK_BUFFER_BYTES);
  SinkWriter out(sink, ctx, sinkBuffer.get() ? sinkBuffer.as<uint8_t>() : fallback,
                 sinkBuffer.get() ? sinkBuffer.size() : sizeof(fallback));
  uint8_t record[INDEX_HEADER_BYTES];
  writeHeader(record, header);
  out.put(record, INDEX_HEADER_BYTES);

  for (uint32_t i = 0; i < verses_.size(); i++) {
    VerseEntry entry;
    memcpy(&entry, verses_.at(i), sizeof(entry));
    writeVerseEntry(record, entry);
    out.put(record, VERSE_ENTRY_BYTES);
  }

  TermEntry entry;
  for (uint32_t i = 0; i < termCount; i++) {
    const Term& term = termAt(order[i]);
    entry.postingCount = term.count;
    writeTermEntry(record, entry);
    out.put(record, TERM_ENTRY_BYTES);
    entry.stringOffset += term.length + 1;
    entry.postingsOffset += term.postingBytes;
  }

  constexpr char TERMINATOR = '\0';
  for (uint32_t i = 0; i < termCount; i++) {
    const std::string_view text = termString(termAt(order[i]));
    out.put(text.data(), text.size());
    out.put(&TERMINATOR, 1);
  }

  for (uint32_t i = 0; i < termCount; i++) {
    const Term& term = termAt(order[i]);
    uint32_t remaining = term.postingBytes;
    for (uint32_t index = term.headBlock; index != NONE && remaining > 0;) {
      const auto* block = reinterpret_cast<const Block*>(blocks_.at(index));
      const uint32_t n = std::min(remaining, BLOCK_DATA_BYTES);
      out.put(block->data, n);
      remaining -= n;
      index = block->next;
    }
  }
  return out.flush();
}

bool IndexBuilder::loadCheckpoint(const ByteSource& source, const uint64_t expectedFingerprint, uint32_t& docsDoneOut) {
  if (failed_ || verses_.size() > 0 || terms_.size() > 0) return false;
  IndexReader reader(allocator_);
  if (reader.open(source, expectedFingerprint) != IndexReader::Status::Incomplete) return false;
  reader.cacheTermIndex();

  for (uint32_t i = 0; i < reader.verseCount(); i++) {
    VerseEntry v;
    if (!reader.verse(i, v) || addVerse(v.book, v.chapter, v.verse, v.spine, v.offset) != i) {
      failed_ = true;
      return false;
    }
  }

  std::vector<uint16_t> postings;
  postings.reserve(1024);
  char text[MAX_TOKEN_BYTES + 1];
  for (uint32_t i = 0; i < reader.termCount(); i++) {
    TermEntry entry;
    size_t length = 0;
    postings.clear();
    if (!reader.term(i, entry) || !reader.termString(entry, text, length) ||
        !reader.postings(entry, postings, entry.postingCount) || postings.size() != entry.postingCount) {
      failed_ = true;
      return false;
    }
    const std::string_view token(text, length);
    for (const uint16_t verse : postings) {
      if (!recordTerm(token, verse)) {
        failed_ = true;
        return false;
      }
    }
  }
  docsDoneOut = reader.header().docsDone;
  return true;
}

}  // namespace BibleSearch
