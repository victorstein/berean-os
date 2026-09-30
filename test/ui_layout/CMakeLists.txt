add_executable(ListRowHeightTest
  ListRowHeightTest.cpp
)

target_include_directories(ListRowHeightTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListRowHeightTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListRowHeightTest)

add_executable(BibleReferenceTest
  BibleReferenceTest.cpp
)

target_include_directories(BibleReferenceTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BibleReferenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleReferenceTest)
