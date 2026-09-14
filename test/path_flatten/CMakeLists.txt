add_executable(PathFlattenTest
  PathFlattenTest.cpp
  ${REPO_ROOT}/lib/PathFlatten/PathFlatten.cpp
)

target_include_directories(PathFlattenTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/PathFlatten
)

target_link_libraries(PathFlattenTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PathFlattenTest)
