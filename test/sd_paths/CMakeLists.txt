add_executable(SdPathsTest
  SdPathsTest.cpp
)

target_include_directories(SdPathsTest PRIVATE
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(SdPathsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SdPathsTest)
