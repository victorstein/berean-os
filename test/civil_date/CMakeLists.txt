# Header-only, like test/study_sleep_pick: util/CivilDate.h needs no sources.
add_executable(CivilDateTest
  CivilDateTest.cpp
)

target_include_directories(CivilDateTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(CivilDateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CivilDateTest)
