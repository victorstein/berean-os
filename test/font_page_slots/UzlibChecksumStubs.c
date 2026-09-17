// uzlib_uncompress_chksum() (lib/uzlib/src/tinflate.c:630) calls uzlib_adler32
// (:642) and uzlib_crc32 (:646), declared at lib/uzlib/src/uzlib.h:165,167.
// lib/uzlib/src vendors only tinflate.c — upstream's adler32.c and crc32.c are
// absent. The firmware links because nothing calls uzlib_uncompress_chksum and
// the linker drops it: xtensa-esp32s3-elf-nm on .pio/build/x4pro/firmware.elf
// finds none of the three symbols. A host link has no such escape, so these two
// bodies exist only to resolve references from dead code. InflateReader calls
// uzlib_uncompress and nothing else (InflateReader.cpp:68,82), so no test can
// reach them — hence the trivial pass-through bodies.

#include <uzlib.h>

uint32_t TINFCC uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  (void)data;
  (void)length;
  return prev_sum;
}

uint32_t TINFCC uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  (void)data;
  (void)length;
  return crc;
}
