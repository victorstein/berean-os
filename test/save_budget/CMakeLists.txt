add_executable(SaveBudgetTest
  SaveBudgetTest.cpp
)

target_include_directories(SaveBudgetTest PRIVATE
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(SaveBudgetTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SaveBudgetTest)
