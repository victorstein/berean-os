# Tested the way test/launcher_bible tests LauncherBible.h: a header-only helper
# beside the sleep activity, ${REPO_ROOT}/src as the only extra include
# directory, and no activity constructed.
add_executable(StudySleepPickTest
  StudySleepPickTest.cpp
)

target_include_directories(StudySleepPickTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(StudySleepPickTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StudySleepPickTest)
