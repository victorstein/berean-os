add_executable(TempAdoptionTest
  TempAdoptionTest.cpp
)

target_include_directories(TempAdoptionTest PRIVATE
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(TempAdoptionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TempAdoptionTest)
