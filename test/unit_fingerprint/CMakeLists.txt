add_executable(UnitFingerprintTest
  UnitFingerprintTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
)

target_include_directories(UnitFingerprintTest PRIVATE ${REPO_ROOT}/lib/StudyStore)

target_link_libraries(UnitFingerprintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitFingerprintTest)
