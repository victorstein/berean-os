add_executable(BootDecisionsTest
  BootDecisionsTest.cpp
)

target_include_directories(BootDecisionsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BootDecisionsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BootDecisionsTest)
