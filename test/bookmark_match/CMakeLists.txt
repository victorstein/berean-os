# The pure page-match rule (util/BookmarkMatch.h). ReaderBookmarks itself needs
# Epub and Section, which no host suite builds.
add_executable(BookmarkMatchTest
  BookmarkMatchTest.cpp
)

target_include_directories(BookmarkMatchTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BookmarkMatchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookmarkMatchTest)
