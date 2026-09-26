# Only the pure decision logic (util/BookmarkSaveAction.h) is built here.
# BookmarkFile.cpp has no host suite yet; see test/storage_io/ for the pattern.
add_executable(BookmarkSaveActionTest
  BookmarkSaveActionTest.cpp
)

target_include_directories(BookmarkSaveActionTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(BookmarkSaveActionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookmarkSaveActionTest)
