add_executable(FormatVersionTest
  FormatVersionTest.cpp
)

target_include_directories(FormatVersionTest PRIVATE
  ${REPO_ROOT}/lib
)

target_link_libraries(FormatVersionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FormatVersionTest)
