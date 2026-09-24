# expat is compiled here from the repo's own sources with the same flags
# platformio.ini passes, so the verse text is tokenised by the identical parser
# the firmware uses. xmltok_impl.c and xmltok_ns.c are #included by xmltok.c.
add_executable(BibleSearchScannerTest
  VerseTextScannerTest.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/VerseTextScanner.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(BibleSearchScannerTest PRIVATE
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_compile_definitions(BibleSearchScannerTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  BIBLE_SEARCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(BibleSearchScannerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchScannerTest)
