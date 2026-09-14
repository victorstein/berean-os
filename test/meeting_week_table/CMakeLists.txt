add_executable(MeetingWeekTableTest
  MeetingWeekTableTest.cpp
  ${REPO_ROOT}/src/network/MeetingWeekTable.cpp
  ${REPO_ROOT}/src/network/WolWeekScan.cpp
)

target_include_directories(MeetingWeekTableTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(MeetingWeekTableTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeetingWeekTableTest)
