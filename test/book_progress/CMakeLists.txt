# A header-only pure unit, tested the way test/launcher_refresh tests
# LauncherRefresh.h: ${REPO_ROOT}/src is the only include directory.
add_executable(BookProgressTest
  BookProgressTest.cpp
)

target_include_directories(BookProgressTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BookProgressTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookProgressTest)
