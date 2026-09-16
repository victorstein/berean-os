# A pure decision helper that lives beside the launcher activity, tested the way
# test/return_stack tests activities/reader/ReturnStack.h: ${REPO_ROOT}/src is
# the only extra include directory and no activity is constructed. The header
# under test has no includes of its own, so unlike test/pagination this suite
# needs no test/stubs entry.
add_executable(LauncherRefreshTest
  LauncherRefreshTest.cpp
)

target_include_directories(LauncherRefreshTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LauncherRefreshTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LauncherRefreshTest)
