add_executable(AutoPageTurnTest
  AutoPageTurnTest.cpp
  ForwardTurnTest.cpp
)

target_include_directories(AutoPageTurnTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(AutoPageTurnTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AutoPageTurnTest)
