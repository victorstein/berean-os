# Slot accounting for FontDecompressor's page buffer, and the FontCacheManager
# composition on top of it (issue #58).
#
# Modelled on test/pagination: the real lib/ sources are compiled unmodified,
# ${REPO_ROOT}/test/stubs comes first on the include path so <Logging.h> and
# <Arduino.h> resolve to no-ops instead of dragging in HardwareSerial, and
# SdCardFontFake.cpp is a LINK-TIME substitute for a class with no virtuals.
#
# tinflate.c and UzlibChecksumStubs.c must stay .c: tinflate.c:559 assigns a
# void* to an unsigned char*, which is C and not C++. The top-level project()
# declares both languages, so CMake picks the right compiler per extension.
add_executable(FontPageSlotsTest
  FontPageSlotsTest.cpp
  SdCardFontFake.cpp
  UzlibChecksumStubs.c
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
)

target_include_directories(FontPageSlotsTest PRIVATE
  ${REPO_ROOT}/test/stubs  # no-op Logging.h / Arduino.h — must come first
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/uzlib/src
)

# The generated builtinFonts headers used as fixtures leave EpdFontData's
# trailing glyphMissHandler/coverageHandler fields out of their initializers,
# which -Wextra reports once per header. Set on the source file, not the target:
# CMake appends a linked library's INTERFACE_COMPILE_OPTIONS after the target's
# own, so crosspoint_test_common's -Wextra would re-enable the warning if this
# were a target_compile_options() call. Everything else in -Wall -Wextra
# -pedantic still applies, here and to every other file in the suite.
set_source_files_properties(FontPageSlotsTest.cpp PROPERTIES
  COMPILE_OPTIONS -Wno-missing-field-initializers)

# Expected, and deliberately not suppressed: FontDecompressor.cpp's logStats()
# leaves `total` and `label` unused once test/stubs/Logging.h expands LOG_DBG to
# nothing, so this build prints two -Wunused warnings for it. Silencing them
# would need -Wno-unused-* over a whole production source file, which would hide
# a real one later.
target_link_libraries(FontPageSlotsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontPageSlotsTest)
