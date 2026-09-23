add_executable(ChapterCompletionTest
  ChapterCompletionTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/ChapterCompletion.cpp
)

target_include_directories(ChapterCompletionTest PRIVATE ${REPO_ROOT}/lib/StudyStore)

target_link_libraries(ChapterCompletionTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(ChapterCompletionTest)
