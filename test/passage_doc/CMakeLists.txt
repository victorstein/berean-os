add_executable(PassageDocTest
  PassageDocTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PassageDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(PassageDocTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(PassageDocTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(PassageDocTest)
