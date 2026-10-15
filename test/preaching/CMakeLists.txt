add_executable(PreachingTest
  IntroSectionTest.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/IntroSection.cpp
)

target_include_directories(PreachingTest PRIVATE
  ${REPO_ROOT}/lib/Preaching
)

target_link_libraries(PreachingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingTest)
