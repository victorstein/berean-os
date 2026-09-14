add_executable(NavKeyGesturesTest
  NavKeyGesturesTest.cpp
  ${REPO_ROOT}/lib/Input/Input/NavKeyGestures.cpp
)

target_include_directories(NavKeyGesturesTest PRIVATE ${REPO_ROOT}/lib/Input)

target_link_libraries(NavKeyGesturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NavKeyGesturesTest)
