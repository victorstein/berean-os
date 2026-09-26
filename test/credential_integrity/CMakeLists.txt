add_executable(CredentialIntegrityTest
  CredentialIntegrityTest.cpp
)

target_link_libraries(CredentialIntegrityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CredentialIntegrityTest)

# WifiCredentialStore.cpp is not host-built; only the pure edit and rollback
# rule (util/WifiCredentialEdit.h) is.
add_executable(WifiCredentialEditTest
  WifiCredentialEditTest.cpp
)

target_include_directories(WifiCredentialEditTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WifiCredentialEditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiCredentialEditTest)
