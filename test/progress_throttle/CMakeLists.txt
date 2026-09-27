# Header-only, like test/launcher_refresh: util/ProgressThrottle.h needs no sources.
add_executable(ProgressThrottleTest
  ProgressThrottleTest.cpp
)

target_include_directories(ProgressThrottleTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ProgressThrottleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProgressThrottleTest)
