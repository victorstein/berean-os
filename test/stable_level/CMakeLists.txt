add_executable(StableLevelTest
  StableLevelTest.cpp
)

target_include_directories(StableLevelTest PRIVATE ${REPO_ROOT}/lib/Input)

target_link_libraries(StableLevelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StableLevelTest)
