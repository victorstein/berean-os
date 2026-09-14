add_executable(UnitTest
  UnitTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
)

target_include_directories(UnitTest PRIVATE ${REPO_ROOT}/lib/StudyStore)

target_link_libraries(UnitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitTest)
