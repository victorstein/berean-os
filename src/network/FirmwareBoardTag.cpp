#include "FirmwareBoardTag.h"

#include <cstring>

// bereanOS targets one board, so there is one name. It matches the release asset
// suffix (firmware-x4pro.bin), which OtaUpdater derives from this tag.
//
// The magic string stays CROSSPOINT-BOARD-V1 deliberately. It appears twice below
// as independent literals, so a partial rename compiles and silently returns a
// garbage board slice into the OTA asset name -- and the scanner's single-byte
// lookback requires the magic's first character never to recur inside it, which
// "BEREAN-BOARD-V1:" violates at BOARD.
#if FREEINK_DEVICE_X4PRO
#define BEREAN_BOARD_NAME "x4pro"
#else
#error "FirmwareBoardTag: no FREEINK_DEVICE_X4PRO flag set; cannot derive board name"
#endif

namespace board_tag {

namespace {
// The magic's first character must not recur inside it — the scanner restarts
// a failed match with a single-byte lookback, which is only exact then.
constexpr size_t MAGIC_LEN = sizeof("CROSSPOINT-BOARD-V1:") - 1;
}  // namespace

const char TAG[] = "CROSSPOINT-BOARD-V1:" BEREAN_BOARD_NAME ";";

const char* boardName() { return TAG + MAGIC_LEN; }
size_t boardNameLen() { return sizeof(TAG) - 1 - MAGIC_LEN - 1; }  // strip magic and ';'

void Scanner::feed(const uint8_t* data, size_t len) {
  if (mismatchFound) return;
  for (size_t i = 0; i < len; i++) {
    const char c = static_cast<char>(data[i]);
    if (capturing) {
      if (c == ';') {
        capturing = false;
        captured[nameLen] = '\0';
        if (nameLen != boardNameLen() || memcmp(captured, boardName(), nameLen) != 0) {
          mismatchFound = true;
          return;
        }
      } else if (nameLen < MAX_NAME && c > 0x20 && c < 0x7F) {
        captured[nameLen++] = c;
      } else {
        // Overlong or non-printable: a chance byte-collision, not a real tag.
        capturing = false;
      }
      continue;
    }
    if (c == TAG[magicMatched]) {
      if (++magicMatched == MAGIC_LEN) {
        magicMatched = 0;
        capturing = true;
        nameLen = 0;
      }
    } else {
      magicMatched = (c == TAG[0]) ? 1 : 0;
    }
  }
}

}  // namespace board_tag
