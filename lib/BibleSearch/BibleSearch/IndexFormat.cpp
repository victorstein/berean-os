#include "IndexFormat.h"

#include <cstring>

namespace BibleSearch {
namespace {

void put16(uint8_t* out, const uint16_t v) { memcpy(out, &v, sizeof(v)); }
void put32(uint8_t* out, const uint32_t v) { memcpy(out, &v, sizeof(v)); }
void put64(uint8_t* out, const uint64_t v) { memcpy(out, &v, sizeof(v)); }

uint16_t get16(const uint8_t* in) {
  uint16_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint32_t get32(const uint8_t* in) {
  uint32_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint64_t get64(const uint8_t* in) {
  uint64_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

}  // namespace

void writeHeader(uint8_t* out, const IndexHeader& h) {
  memset(out, 0, INDEX_HEADER_BYTES);
  memcpy(out, INDEX_MAGIC, sizeof(INDEX_MAGIC));
  put16(out + 4, h.formatVersion);
  put16(out + 6, h.flags);
  put64(out + 8, h.fingerprint);
  put32(out + 16, h.verseCount);
  put32(out + 20, h.termCount);
  put32(out + 24, h.docsDone);
  put32(out + 28, h.verseTableOffset);
  put32(out + 32, h.termTableOffset);
  put32(out + 36, h.termStringsOffset);
  put32(out + 40, h.postingsOffset);
  put32(out + 44, h.fileSize);
}

std::optional<IndexHeader> readHeader(const uint8_t* in, const size_t length) {
  if (length < INDEX_HEADER_BYTES) return std::nullopt;
  if (memcmp(in, INDEX_MAGIC, sizeof(INDEX_MAGIC)) != 0) return std::nullopt;
  IndexHeader h;
  h.formatVersion = get16(in + 4);
  h.flags = get16(in + 6);
  h.fingerprint = get64(in + 8);
  h.verseCount = get32(in + 16);
  h.termCount = get32(in + 20);
  h.docsDone = get32(in + 24);
  h.verseTableOffset = get32(in + 28);
  h.termTableOffset = get32(in + 32);
  h.termStringsOffset = get32(in + 36);
  h.postingsOffset = get32(in + 40);
  h.fileSize = get32(in + 44);
  return h;
}

void writeVerseEntry(uint8_t* out, const VerseEntry& e) {
  out[0] = e.book;
  out[1] = e.chapter;
  out[2] = e.verse;
  put16(out + 3, e.spine);
  put32(out + 5, e.offset);
}

VerseEntry readVerseEntry(const uint8_t* in) {
  VerseEntry e;
  e.book = in[0];
  e.chapter = in[1];
  e.verse = in[2];
  e.spine = get16(in + 3);
  e.offset = get32(in + 5);
  return e;
}

void writeTermEntry(uint8_t* out, const TermEntry& e) {
  put32(out + 0, e.stringOffset);
  put32(out + 4, e.postingsOffset);
  put16(out + 8, e.postingCount);
}

TermEntry readTermEntry(const uint8_t* in) {
  TermEntry e;
  e.stringOffset = get32(in + 0);
  e.postingsOffset = get32(in + 4);
  e.postingCount = get16(in + 8);
  return e;
}

size_t writeVarint(uint8_t* out, uint16_t value) {
  size_t n = 0;
  while (value >= 0x80) {
    out[n++] = static_cast<uint8_t>(value | 0x80);
    value >>= 7;
  }
  out[n++] = static_cast<uint8_t>(value);
  return n;
}

}  // namespace BibleSearch
