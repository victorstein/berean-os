# The realistic cases build from the scanner's verbatim NWT fixtures, so expat
# is compiled here with the firmware's flags, as in bible_search_scanner.
add_executable(BibleSearchIndexTest
  IndexTest.cpp
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
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
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
