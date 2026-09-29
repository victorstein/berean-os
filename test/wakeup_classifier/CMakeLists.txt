add_executable(WakeupClassifierTest
  WakeupClassifierTest.cpp
)

target_include_directories(WakeupClassifierTest PRIVATE ${REPO_ROOT}/lib/Input)

target_link_libraries(WakeupClassifierTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeupClassifierTest)
