add_executable(BookmarkDocTest
  BookmarkDocTest.cpp
  ${REPO_ROOT}/src/util/BookmarkDoc.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookmarkDocTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(BookmarkDocTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(BookmarkDocTest)
