add_executable(UnitIndexFormatTest
  UnitIndexFormatTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitIndexFormat.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
)

target_include_directories(UnitIndexFormatTest PRIVATE ${REPO_ROOT}/lib/StudyStore)

target_link_libraries(UnitIndexFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitIndexFormatTest)
