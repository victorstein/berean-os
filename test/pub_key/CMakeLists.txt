add_executable(PubKeyTest
  PubKeyTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PubKey.cpp
  ${REPO_ROOT}/lib/PathFlatten/PathFlatten.cpp
)

target_include_directories(PubKeyTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/PathFlatten
)

target_link_libraries(PubKeyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PubKeyTest)
