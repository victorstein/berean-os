# Only the pure decision logic (util/BookmarkSaveAction.h) is host-buildable --
# BookmarkFile.cpp pulls Arduino.h transitively through PersistableStore.h.
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
