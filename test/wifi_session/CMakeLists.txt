# WifiSession reads the Wi-Fi driver; only its exit decision
# (network/WifiSessionPolicy.h) is host-built.
add_executable(WifiSessionPolicyTest
  WifiSessionPolicyTest.cpp
)

target_include_directories(WifiSessionPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WifiSessionPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiSessionPolicyTest)
