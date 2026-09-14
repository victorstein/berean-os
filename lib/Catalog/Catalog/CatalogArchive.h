#pragma once

#include <cstddef>
#include <cstdint>

// The gzip container the published catalog index arrives in.
//
// uzlib declares uzlib_gzip_parse_header() but this vendored copy ships only
// tinflate.c, so the symbol does not link. The header is a handful of fixed
// bytes plus four optional variable-length fields; parsing it here keeps the
// index fetch on the deflate core that is already in the build and makes the
// container handling a host-testable unit rather than device-only code.
namespace catalog {

// 10-byte header, at least one deflate byte, 8-byte trailer.
inline constexpr size_t GZIP_MIN_BYTES = 19;

// True when the buffer opens with the gzip magic. The published index may be
// served compressed or plain, so the loader decides from the bytes rather than
// from the file name.
bool looksGzipped(const uint8_t* data, size_t len);

// Offset of the raw deflate payload: past the fixed header and whichever of
// FEXTRA, FNAME, FCOMMENT and FHCRC the FLG byte announced. False when the
// member is truncated, uses a compression method other than deflate, or sets a
// reserved FLG bit.
bool gzipPayloadOffset(const uint8_t* data, size_t len, size_t& out);

// The ISIZE trailer: uncompressed length modulo 2^32, as the producer recorded
// it. Trusted only as an allocation hint -- the inflate is what proves it.
bool gzipDeclaredSize(const uint8_t* data, size_t len, uint32_t& out);

}  // namespace catalog
