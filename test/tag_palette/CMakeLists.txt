add_executable(TagPaletteTest
  TagPaletteTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
)

target_include_directories(TagPaletteTest PRIVATE ${REPO_ROOT}/lib/StudyStore)

target_link_libraries(TagPaletteTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TagPaletteTest)
