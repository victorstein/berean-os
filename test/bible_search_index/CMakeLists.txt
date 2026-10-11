# The realistic cases build from the scanner's verbatim NWT fixtures, so expat
# is compiled here with the firmware's flags, as in bible_search_scanner.
add_executable(BibleSearchIndexTest
  IndexTest.cpp
  ResidencyTest.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexFormat.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexBuilder.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexReader.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Query.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/VerseTextScanner.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(BibleSearchIndexTest PRIVATE
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/...>, as src/study includes it
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_compile_definitions(BibleSearchIndexTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  BIBLE_SEARCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../bible_search_scanner/fixtures"
)

target_link_libraries(BibleSearchIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchIndexTest)


# The fingerprint record's codec, and in later tasks its I/O against the
# in-memory card. test/stubs comes first so <HalStorage.h>, <Logging.h> and
# <Arduino.h> resolve to the stubs, as in test/storage_io.
add_executable(BibleSearchFingerprintTest
  FingerprintFileTest.cpp
  FingerprintFileIoTest.cpp
  ${REPO_ROOT}/src/study/BibleSearchFingerprintFile.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
)

target_include_directories(BibleSearchFingerprintTest PRIVATE
  ${REPO_ROOT}/test/stubs
)

target_link_libraries(BibleSearchFingerprintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchFingerprintTest)
