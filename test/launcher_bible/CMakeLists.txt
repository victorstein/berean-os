# Tested the way test/launcher_refresh tests LauncherRefresh.h: a header-only
# helper beside the launcher activity, ${REPO_ROOT}/src as the only extra
# include directory, and no activity constructed.
add_executable(LauncherBibleTest
  LauncherBibleTest.cpp
)

target_include_directories(LauncherBibleTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LauncherBibleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LauncherBibleTest)
