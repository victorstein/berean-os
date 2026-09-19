#pragma once

#include <cstdint>

// Why a document read did not yield a usable document. Callers that own user
// data MUST distinguish Missing (safe to treat as empty and overwrite) from
// Unreadable / ParseError (never overwrite — the data may still be there).
enum class DocReadStatus : uint8_t {
  Ok,
  Missing,
  Unreadable,
  ParseError,
};

// Classifies a read attempt from its three observable outcomes. Deliberately
// free of Arduino and storage dependencies so it can be unit tested on the host.
constexpr DocReadStatus classifyDocRead(const bool exists, const bool contentEmpty, const bool parseFailed) {
  if (!exists) return DocReadStatus::Missing;
  if (contentEmpty) return DocReadStatus::Unreadable;
  if (parseFailed) return DocReadStatus::ParseError;
  return DocReadStatus::Ok;
}

// Whether a read-modify-write caller may go on to write, given the status its
// read returned. Ok and Missing are both safe -- Missing legitimately means
// "start a new document". Unreadable and ParseError are not: the bytes are
// still on the card, and an atomic write replaces them cleanly, leaving
// nothing torn to notice.
//
// A comparison rather than a switch on purpose: a status added later is false
// -- refuse -- which is the safe direction, and the asserts below force that
// choice to be made deliberately rather than inherited.
constexpr bool mayOverwriteAfterRead(const DocReadStatus status) {
  return status == DocReadStatus::Ok || status == DocReadStatus::Missing;
}

static_assert(mayOverwriteAfterRead(DocReadStatus::Ok));
static_assert(mayOverwriteAfterRead(DocReadStatus::Missing));
static_assert(!mayOverwriteAfterRead(DocReadStatus::Unreadable));
static_assert(!mayOverwriteAfterRead(DocReadStatus::ParseError));
