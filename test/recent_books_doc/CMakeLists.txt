add_executable(RecentBooksDocTest
  RecentBooksDocTest.cpp
  ${REPO_ROOT}/src/util/RecentBooksDoc.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(RecentBooksDocTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(RecentBooksDocTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(RecentBooksDocTest)
