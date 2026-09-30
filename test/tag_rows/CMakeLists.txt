add_executable(TagRowsTest
  TagRowsTest.cpp
  PassageActionsTest.cpp
  TagChipRowTest.cpp
)

target_include_directories(TagRowsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/StudyStore
)

target_link_libraries(TagRowsTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TagRowsTest)
