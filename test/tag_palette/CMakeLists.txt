# test/stubs supplies the no-op <Logging.h>. lib/Memory must be on the path, or
# <Memory.h> resolves to the C library's <memory.h>. Modelled on
# test/bible_search_index.
add_executable(TagPaletteTest
  TagPaletteTest.cpp
  FailingNothrowNew.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
)

target_include_directories(TagPaletteTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/Fold.h>, as TagPalette.cpp includes it
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

# Clang may omit a replaceable operator new called from a new-expression when
# the result is only null-checked, zeroed and freed, so an injected failure
# would never be observed. -fno-builtin keeps every call real.
target_compile_options(TagPaletteTest PRIVATE -fno-builtin)

target_link_libraries(TagPaletteTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TagPaletteTest)
