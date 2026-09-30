#pragma once

#include <Utf8.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

// Free of Epub and Arduino so the host suite can exercise it (test/number_grid).

// Copies `source` into `dest`, cut on a UTF-8 boundary so drawText never sees
// an incomplete sequence (which renders as a replacement character).
inline void copyUtf8Truncated(char* dest, const size_t destBytes, const std::string_view source) {
  const size_t fit = source.size() < destBytes - 1 ? source.size() : destBytes - 1;
  const int safe = utf8SafeTruncateBuffer(source.data(), static_cast<int>(fit));
  memcpy(dest, source.data(), static_cast<size_t>(safe));
  dest[safe] = '\0';
}

// Fills `rowCount` fixed-stride rows from `labels` in order. Rows past the last label are emptied,
// so a refill never leaves an earlier label behind.
inline void copyLabelRows(char* rows, const size_t rowBytes, const int rowCount, const std::string* labels,
                          const size_t labelCount) {
  for (int i = 0; i < rowCount; i++) {
    char* row = rows + static_cast<size_t>(i) * rowBytes;
    if (static_cast<size_t>(i) < labelCount) {
      copyUtf8Truncated(row, rowBytes, labels[i]);
    } else {
      row[0] = '\0';
    }
  }
}
