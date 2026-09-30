add_executable(NumberGridLayoutTest
  NumberGridLayoutTest.cpp
)

target_include_directories(NumberGridLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(NumberGridLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NumberGridLayoutTest)

add_executable(BookGridLayoutTest
  BookGridLayoutTest.cpp
)

target_include_directories(BookGridLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BookGridLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookGridLayoutTest)

add_executable(BibleEntryPositionTest
  BibleEntryPositionTest.cpp
)

target_include_directories(BibleEntryPositionTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BibleEntryPositionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleEntryPositionTest)

add_executable(GridMarksTest
  GridMarksTest.cpp
)

target_include_directories(GridMarksTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(GridMarksTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GridMarksTest)

add_executable(BookLabelsTest
  BookLabelsTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookLabelsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(BookLabelsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookLabelsTest)

# SpineSearch matches through BibleNav::filenameTail, so the nav scanner and
# expat come along, as in test/bible_book_join.
add_executable(SpineSearchTest
  SpineSearchTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BibleNavScanner.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(SpineSearchTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(SpineSearchTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(SpineSearchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SpineSearchTest)

add_executable(BibleNavCacheTest
  BibleNavCacheTest.cpp
)

target_include_directories(BibleNavCacheTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BibleNavCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleNavCacheTest)
